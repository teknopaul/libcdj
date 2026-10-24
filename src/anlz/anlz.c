#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "anlz.h"
#include "utf16.h"

//SNIP_anlz_core

#define ANLZ_FILE_HEADER_MIN 28            /* "PMAI" + len_header + len_file + 16 unknown bytes */
#define ANLZ_TAG_HEADER_MIN  12            /* fourcc + len_header + len_tag */
#define ANLZ_MAX_FILE        (64u << 20)

struct anlz {
    const uint8_t *data;
    uint32_t       len;
    uint8_t       *owned;        /* non-NULL when anlz_open_file allocated the buffer */
    anlz_tag_t    *tags;
    uint32_t       ntags;
    int            damaged;
    int            len_file_ok;
    char          *path;         /* decoded PPTH, NULL if absent */
    anlz_err_t     path_err;
};

static uint32_t be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static uint16_t be16(const uint8_t *p)
{
    return (uint16_t)(((uint32_t)p[0] << 8) | p[1]);
}

const char *anlz_strerror(anlz_err_t err)
{
    switch (err) {
        case ANLZ_OK:          return "ok";
        case ANLZ_ERR_IO:      return "cannot read file";
        case ANLZ_ERR_MAGIC:   return "not an ANLZ file (bad magic)";
        case ANLZ_ERR_TRUNC:   return "file is truncated";
        case ANLZ_ERR_BAD_LEN: return "invalid length field";
        case ANLZ_ERR_NOMEM:   return "out of memory";
        case ANLZ_ERR_ABSENT:  return "tag not present";
    }
    return "unknown error";
}

static const anlz_tag_t *find_tag(const anlz_t *a, const char *fourcc, uint32_t nth);

/* PPTH: 0x0c u32 len_path (bytes incl. NUL), 0x10 UTF-16BE path. */
static void decode_path(anlz_t *a)
{
    const anlz_tag_t *t = find_tag(a, "PPTH", 0);
    if (!t) { a->path_err = ANLZ_ERR_ABSENT; return; }
    if (t->len_tag < 0x10) { a->path_err = ANLZ_ERR_BAD_LEN; return; }
    uint32_t avail = t->len_tag - 0x10;
    uint32_t len_path = be32(t->data + 0x0c);
    if (len_path > avail) len_path = avail;           /* lying length: use what is there */
    if (len_path > 4096) len_path = 4096;             /* paths are short; also keeps the size arithmetic safe */
    char *s = malloc((size_t)len_path * 3 / 2 + 4);
    if (!s) { a->path_err = ANLZ_ERR_NOMEM; return; }
    if (utf16be_decode(t->data + 0x10, len_path, s, len_path * 3 / 2 + 4) != 0) {
        free(s);
        a->path_err = ANLZ_ERR_BAD_LEN;
        return;
    }
    a->path = s;
    a->path_err = ANLZ_OK;
}

anlz_err_t anlz_open_mem(const uint8_t *data, uint32_t len, anlz_t **out)
{
    if (!out) return ANLZ_ERR_BAD_LEN;
    *out = NULL;
    if (!data || len < ANLZ_FILE_HEADER_MIN) return ANLZ_ERR_TRUNC;
    if (memcmp(data, "PMAI", 4) != 0) return ANLZ_ERR_MAGIC;

    uint32_t len_header = be32(data + 4);
    uint32_t len_file   = be32(data + 8);
    if (len_header < ANLZ_FILE_HEADER_MIN || len_header > len) return ANLZ_ERR_BAD_LEN;

    anlz_t *a = calloc(1, sizeof *a);
    if (!a) return ANLZ_ERR_NOMEM;
    a->data = data;
    a->len = len;
    a->len_file_ok = (len_file == len);

    /* a tag needs at least 12 bytes, so the count is bounded by the bytes available */
    uint32_t max_tags = (len - len_header) / ANLZ_TAG_HEADER_MIN;
    if (max_tags) {
        a->tags = calloc(max_tags, sizeof *a->tags);
        if (!a->tags) { free(a); return ANLZ_ERR_NOMEM; }
    }

    anlz_err_t first_error = ANLZ_OK;
    uint64_t p = len_header;
    while (p < len) {
        if (len - p < ANLZ_TAG_HEADER_MIN) { first_error = ANLZ_ERR_TRUNC; break; }
        uint32_t len_tag = be32(data + p + 8);
        if (len_tag < ANLZ_TAG_HEADER_MIN) { first_error = ANLZ_ERR_BAD_LEN; break; }
        if (len_tag > len - p) { first_error = ANLZ_ERR_TRUNC; break; }

        anlz_tag_t *t = &a->tags[a->ntags++];
        memcpy(t->fourcc, data + p, 4);
        t->fourcc[4] = '\0';
        t->offset = (uint32_t)p;
        t->len_header = be32(data + p + 4);
        t->len_tag = len_tag;
        t->data = data + p;
        p += len_tag;
    }

    if (first_error != ANLZ_OK) {
        a->damaged = 1;
        if (a->ntags == 0) {            /* nothing usable before the damage */
            free(a->tags);
            free(a);
            return first_error;
        }
    }
    decode_path(a);
    *out = a;
    return ANLZ_OK;
}

anlz_err_t anlz_open_file(const char *path, anlz_t **out)
{
    if (!out) return ANLZ_ERR_BAD_LEN;
    *out = NULL;
    FILE *f = path ? fopen(path, "rb") : NULL;
    if (!f) return ANLZ_ERR_IO;

    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return ANLZ_ERR_IO; }
    long sz = ftell(f);
    if (sz < 0 || (unsigned long)sz > ANLZ_MAX_FILE) { fclose(f); return sz < 0 ? ANLZ_ERR_IO : ANLZ_ERR_BAD_LEN; }
    rewind(f);

    uint8_t *buf = malloc(sz ? (size_t)sz : 1);
    if (!buf) { fclose(f); return ANLZ_ERR_NOMEM; }
    if (fread(buf, 1, (size_t)sz, f) != (size_t)sz) { free(buf); fclose(f); return ANLZ_ERR_IO; }
    fclose(f);

    anlz_err_t err = anlz_open_mem(buf, (uint32_t)sz, out);
    if (err != ANLZ_OK) { free(buf); return err; }
    (*out)->owned = buf;
    return ANLZ_OK;
}

void anlz_close(anlz_t *a)
{
    if (!a) return;
    free(a->tags);
    free(a->path);
    free(a->owned);
    free(a);
}

uint32_t anlz_tag_count(const anlz_t *a)
{
    return a ? a->ntags : 0;
}

const anlz_tag_t *anlz_tag_at(const anlz_t *a, uint32_t index)
{
    return (a && index < a->ntags) ? &a->tags[index] : NULL;
}

static const anlz_tag_t *find_tag(const anlz_t *a, const char *fourcc, uint32_t nth)
{
    if (!a || !fourcc) return NULL;
    for (uint32_t i = 0; i < a->ntags; i++) {
        if (memcmp(a->tags[i].fourcc, fourcc, 4) == 0 && nth-- == 0)
            return &a->tags[i];
    }
    return NULL;
}

const anlz_tag_t *anlz_find_tag(const anlz_t *a, const char fourcc[4], uint32_t nth)
{
    return find_tag(a, fourcc, nth);
}

anlz_err_t anlz_path(const anlz_t *a, const char **utf8)
{
    if (!a || !utf8) return ANLZ_ERR_BAD_LEN;
    *utf8 = a->path;
    return a->path ? ANLZ_OK : a->path_err;
}

/* ---- beat grid ----
 * PQTZ (.DAT, header 0x18): 0x14 u32 len_beats, then 8-byte entries u16 beat, u16 tempo, u32 ms.
 * PQT2 (.EXT, header 0x38): 0x18 first beat (u16 number, u16 tempo, u32 ms), 0x20 last beat (same),
 *   0x28 u32 beat count, 0x38 one u16 per beat: microseconds after the millisecond (0..999). */
#define PQTZ_HEADER 0x18
#define PQT2_HEADER 0x38

static int grid_monotonic(const anlz_beatgrid_t *g)
{
    uint32_t prev = 0;
    for (uint32_t i = 0; i < g->count; i++) {
        anlz_beat_t b;
        anlz_beat_at(g, i, &b);
        if (i && b.time_ms < prev) return 0;
        prev = b.time_ms;
    }
    return 1;
}

anlz_err_t anlz_beatgrid(const anlz_t *a, anlz_beatgrid_t *out)
{
    if (!a || !out) return ANLZ_ERR_BAD_LEN;
    memset(out, 0, sizeof *out);

    const anlz_tag_t *t = find_tag(a, "PQTZ", 0);
    if (t) {
        if (t->len_tag < PQTZ_HEADER || t->len_header < PQTZ_HEADER) return ANLZ_ERR_BAD_LEN;
        uint32_t claimed = be32(t->data + 0x14);
        uint32_t avail = (t->len_tag - PQTZ_HEADER) / 8;
        out->source = ANLZ_GRID_PQTZ;
        out->count = claimed < avail ? claimed : avail;
        out->clamped = claimed > avail;
        out->pqtz = t->data + PQTZ_HEADER;
        out->monotonic = grid_monotonic(out);
        return ANLZ_OK;
    }

    t = find_tag(a, "PQT2", 0);
    if (!t) return ANLZ_ERR_ABSENT;
    if (t->len_tag < PQT2_HEADER || t->len_header < PQT2_HEADER) return ANLZ_ERR_BAD_LEN;
    uint32_t claimed = be32(t->data + 0x28);
    uint32_t avail = (t->len_tag - PQT2_HEADER) / 2;
    out->source = ANLZ_GRID_PQT2;
    out->header_count = claimed;
    out->count = claimed < avail ? claimed : avail;
    out->clamped = claimed > avail;
    out->has_sub_ms = 1;
    out->sub = t->data + PQT2_HEADER;
    out->first_number = be16(t->data + 0x18);
    out->tempo = be16(t->data + 0x1a);
    out->first_ms = be32(t->data + 0x1c);
    out->last_ms = be32(t->data + 0x24);
    out->monotonic = out->last_ms >= out->first_ms;
    return ANLZ_OK;
}

anlz_err_t anlz_beatgrid_attach_sub_ms(anlz_beatgrid_t *g, const anlz_t *ext)
{
    if (!g || !ext) return ANLZ_ERR_BAD_LEN;
    const anlz_tag_t *t = find_tag(ext, "PQT2", 0);
    if (!t) return ANLZ_ERR_ABSENT;
    if (t->len_tag < PQT2_HEADER || t->len_header < PQT2_HEADER) return ANLZ_ERR_BAD_LEN;
    uint32_t claimed = be32(t->data + 0x28);
    if (claimed != g->count || (t->len_tag - PQT2_HEADER) / 2 < g->count) return ANLZ_ERR_BAD_LEN;
    g->sub = t->data + PQT2_HEADER;
    g->has_sub_ms = 1;
    return ANLZ_OK;
}

int anlz_beat_at(const anlz_beatgrid_t *g, uint32_t i, anlz_beat_t *out)
{
    if (!g || !out || i >= g->count) return 0;
    memset(out, 0, sizeof *out);
    if (g->source == ANLZ_GRID_PQTZ) {
        const uint8_t *e = g->pqtz + (size_t)i * 8;
        out->beat_number = be16(e);
        out->bpm_x100 = be16(e + 2);
        out->time_ms = be32(e + 4);
        if (g->sub) out->sub_ms_us = be16(g->sub + (size_t)i * 2);
        return 1;
    }
    /* PQT2 only: the beats lie on a straight line between the two anchors (checked on real data:
     * within 0.11 ms of the true times, so the reconstructed millisecond can be 1 ms off). */
    int64_t x0 = (int64_t)g->first_ms * 1000 + be16(g->sub);
    int64_t xl = (int64_t)g->last_ms * 1000 + be16(g->sub + (size_t)(g->count - 1) * 2);
    int64_t x = x0;
    if (g->header_count > 1) x = x0 + (xl - x0) * (int64_t)i / (int64_t)(g->header_count - 1);
    out->beat_number = (uint16_t)((g->first_number + i - 1) % 4 + 1);
    out->bpm_x100 = g->tempo;
    out->time_ms = (uint32_t)(x / 1000);
    out->sub_ms_us = (uint16_t)(x % 1000);
    return 1;
}

int32_t anlz_beat_for_time(const anlz_beatgrid_t *g, uint32_t time_ms)
{
    if (!g || g->count == 0) return -1;
    anlz_beat_t b;
    if (!g->monotonic) {                       /* no ordering to exploit */
        int32_t best = -1;
        for (uint32_t i = 0; i < g->count; i++) {
            anlz_beat_at(g, i, &b);
            if (b.time_ms <= time_ms && (best < 0 || i > (uint32_t)best)) best = (int32_t)i;
        }
        return best;
    }
    uint32_t lo = 0, hi = g->count;            /* first index with time > time_ms */
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2;
        anlz_beat_at(g, mid, &b);
        if (b.time_ms <= time_ms) lo = mid + 1; else hi = mid;
    }
    return (int32_t)lo - 1;
}

double anlz_avg_bpm(const anlz_beatgrid_t *g)
{
    if (!g || g->count < 2) return 0.0;
    anlz_beat_t first, last;
    anlz_beat_at(g, 0, &first);
    anlz_beat_at(g, g->count - 1, &last);
    if (last.time_ms <= first.time_ms) return 0.0;
    return 60000.0 * (double)(g->count - 1) / (double)(last.time_ms - first.time_ms);
}

int anlz_is_damaged(const anlz_t *a)
{
    return a ? a->damaged : 0;
}

int anlz_file_len_ok(const anlz_t *a)
{
    return a ? a->len_file_ok : 0;
}

/* ---- cues ----
 * PCOB (header 0x18): 0x0c u32 list type (0 memory, 1 hot), 0x12 u16 len_cues, entries PCPT (0x38 bytes):
 *   0x0c hot_cue u32, 0x10 status u32, 0x1c type u8 (1 point, 2 loop), 0x20 time u32, 0x24 loop_time u32.
 * PCO2 (header 0x14): 0x0c u32 list type, 0x10 u16 len_cues, variable PCP2 entries (own len_entry @0x08):
 *   0x0c hot_cue u32, 0x10 type u8, 0x14 time u32, 0x18 loop_time u32, 0x1c color_id u8, 0x1d..0x1f raw,
 *   0x24 u16 loop numerator, 0x26 u16 denominator, 0x28 u32 len_comment, 0x2c comment UTF-16BE, then
 *   (when the entry is long enough) 4 colour bytes: code, red, green, blue. */
#define PCOB_HEADER 0x18
#define PCO2_HEADER 0x14
#define COMMENT_MAX_BYTES 170          /* 85 UTF-16 units: fits comment[256] as UTF-8 */

static int parse_pcpt(const uint8_t *e, uint32_t len, uint32_t list_type, anlz_cue_t *c)
{
    if (len < 0x28) return 0;
    memset(c, 0, sizeof *c);
    c->hot_cue = (uint8_t)be32(e + 0x0c);
    c->status = be32(e + 0x10);
    c->is_loop = e[0x1c] == 2;
    c->marker = ((uint32_t)e[0x1d] << 16) | ((uint32_t)e[0x1e] << 8) | e[0x1f];
    c->time_ms = be32(e + 0x20);
    c->loop_time_ms = c->is_loop ? be32(e + 0x24) : 0;
    (void)list_type;
    return 1;
}

static int parse_pcp2(const uint8_t *e, uint32_t len, anlz_cue_t *c)
{
    if (len < 0x1c) return 0;
    memset(c, 0, sizeof *c);
    c->from_pco2 = 1;
    c->hot_cue = (uint8_t)be32(e + 0x0c);
    c->is_loop = e[0x10] == 2;
    c->marker = ((uint32_t)e[0x11] << 16) | ((uint32_t)e[0x12] << 8) | e[0x13];
    c->time_ms = be32(e + 0x14);
    c->loop_time_ms = c->is_loop ? be32(e + 0x18) : 0;
    if (len >= 0x20) {
        c->color_id = e[0x1c];
        memcpy(c->raw_1d, e + 0x1d, 3);
    } else if (len >= 0x1d) {
        c->color_id = e[0x1c];
    }
    if (len >= 0x28) {
        c->loop_num = be16(e + 0x24);
        c->loop_den = be16(e + 0x26);
    }
    if (len >= 0x2c) {
        uint32_t len_comment = be32(e + 0x28);
        uint32_t room = len - 0x2c;
        if (len_comment > room) len_comment = room;
        uint32_t decode = len_comment < COMMENT_MAX_BYTES ? len_comment : COMMENT_MAX_BYTES;
        if (decode >= 2 && utf16be_decode(e + 0x2c, decode, c->comment, sizeof c->comment) != 0)
            c->comment[0] = '\0';
        uint32_t colour_at = 0x2c + len_comment;
        if (len >= colour_at + 4) {
            c->has_rgb = 1;
            c->color_code = e[colour_at];
            c->red = e[colour_at + 1];
            c->green = e[colour_at + 2];
            c->blue = e[colour_at + 3];
        }
    }
    return 1;
}

/* Append the entries of one PCOB/PCO2 tag. Returns 0 if an entry was malformed (earlier ones are kept). */
static int read_cue_tag(const anlz_tag_t *t, int is_pco2, anlz_cue_kind_t kind,
                        anlz_cue_t *dst, uint32_t *n, uint32_t cap)
{
    uint32_t header = is_pco2 ? PCO2_HEADER : PCOB_HEADER;
    if (t->len_tag < header) return 0;
    uint32_t list_type = be32(t->data + 0x0c);
    if (kind != ANLZ_CUES_ALL && list_type != (uint32_t)kind) return 1;
    uint32_t len_cues = is_pco2 ? be16(t->data + 0x10) : be16(t->data + 0x12);

    uint32_t pos = header;
    for (uint32_t k = 0; k < len_cues; k++) {
        if (t->len_tag - pos < 12) return 0;
        const uint8_t *e = t->data + pos;
        if (memcmp(e, is_pco2 ? "PCP2" : "PCPT", 4) != 0) return 0;
        uint32_t len_entry = be32(e + 8);
        if (len_entry < 12 || len_entry > t->len_tag - pos) return 0;
        if (*n >= cap) return 0;
        int ok = is_pco2 ? parse_pcp2(e, len_entry, &dst[*n]) : parse_pcpt(e, len_entry, list_type, &dst[*n]);
        if (!ok) return 0;
        (*n)++;
        pos += len_entry;
    }
    return 1;
}

static int cue_cmp(const void *x, const void *y)
{
    const anlz_cue_t *a = x, *b = y;
    if ((a->hot_cue == 0) != (b->hot_cue == 0)) return a->hot_cue == 0 ? -1 : 1;   /* memory points first */
    if (a->hot_cue == 0) return (a->time_ms > b->time_ms) - (a->time_ms < b->time_ms);
    if (a->hot_cue != b->hot_cue) return a->hot_cue < b->hot_cue ? -1 : 1;
    return (a->time_ms > b->time_ms) - (a->time_ms < b->time_ms);
}

anlz_err_t anlz_cues_from(const anlz_t *a, anlz_cue_kind_t kind, int use_pco2, anlz_cue_list_t *out)
{
    if (!a || !out) return ANLZ_ERR_BAD_LEN;
    memset(out, 0, sizeof *out);
    if (kind != ANLZ_CUES_MEMORY && kind != ANLZ_CUES_HOT && kind != ANLZ_CUES_ALL) return ANLZ_ERR_BAD_LEN;

    use_pco2 = use_pco2 != 0;
    const char *fourcc = use_pco2 ? "PCO2" : "PCOB";
    if (!find_tag(a, fourcc, 0)) return ANLZ_ERR_ABSENT;

    /* every entry is at least 12 bytes, so this bounds the allocation by the file size */
    uint32_t cap = 0;
    for (uint32_t i = 0; find_tag(a, fourcc, i); i++) cap += find_tag(a, fourcc, i)->len_tag / 12;
    if (cap) {
        out->cues = calloc(cap, sizeof *out->cues);
        if (!out->cues) return ANLZ_ERR_NOMEM;
    }
    for (uint32_t i = 0; find_tag(a, fourcc, i); i++)
        if (!read_cue_tag(find_tag(a, fourcc, i), use_pco2, kind, out->cues, &out->count, cap))
            out->damaged = 1;
    if (out->count > 1) qsort(out->cues, out->count, sizeof *out->cues, cue_cmp);
    return ANLZ_OK;
}

anlz_err_t anlz_cues(const anlz_t *a, anlz_cue_kind_t kind, anlz_cue_list_t *out)
{
    return anlz_cues_from(a, kind, a && find_tag(a, "PCO2", 0) != NULL, out);
}

void anlz_cue_list_free(anlz_cue_list_t *list)
{
    if (!list) return;
    free(list->cues);
    memset(list, 0, sizeof *list);
}

/* ---- waveforms ----
 * PWAV/PWV2 (header 0x14): 0x0c u32 len_preview (= entries, 1 byte each), 0x10 unknown, data at 0x14.
 * PWV3/PWV4/PWV5/PWV7 (header 0x18): 0x0c u32 len_entry_bytes, 0x10 u32 len_entries, 0x14 unknown, data at 0x18.
 * PWV6 (header 0x14): 0x0c len_entry_bytes, 0x10 len_entries, data at 0x14. */
static const struct {
    anlz_wave_kind_t kind;
    const char      *fourcc;
    uint32_t         header;
    uint32_t         entry_bytes;     /* expected */
    int              fixed_count;     /* PWAV/PWV2: the count field is a byte length at 0x0c */
    double           per_sec;
} WAVES[] = {
    { ANLZ_WAVE_PREVIEW,        "PWAV", 0x14, 1, 1, 0.0 },
    { ANLZ_WAVE_TINY,           "PWV2", 0x14, 1, 1, 0.0 },
    { ANLZ_WAVE_DETAIL,         "PWV3", 0x18, 1, 0, 150.0 },
    { ANLZ_WAVE_COLOR_PREVIEW,  "PWV4", 0x18, 6, 0, 0.0 },
    { ANLZ_WAVE_COLOR_DETAIL,   "PWV5", 0x18, 2, 0, 150.0 },
    { ANLZ_WAVE_3BAND_PREVIEW,  "PWV6", 0x14, 3, 0, 0.0 },
    { ANLZ_WAVE_3BAND_DETAIL,   "PWV7", 0x18, 3, 0, 150.0 },
};

anlz_err_t anlz_waveform(const anlz_t *a, anlz_wave_kind_t kind, anlz_waveform_t *out)
{
    if (!a || !out) return ANLZ_ERR_BAD_LEN;
    memset(out, 0, sizeof *out);
    for (unsigned i = 0; i < sizeof WAVES / sizeof WAVES[0]; i++) {
        if (WAVES[i].kind != kind) continue;
        const anlz_tag_t *t = find_tag(a, WAVES[i].fourcc, 0);
        if (!t) return ANLZ_ERR_ABSENT;
        uint32_t header = WAVES[i].header;
        if (t->len_tag < header || t->len_header < header) return ANLZ_ERR_BAD_LEN;

        uint32_t entry_bytes = WAVES[i].entry_bytes, claimed;
        if (WAVES[i].fixed_count) {
            claimed = be32(t->data + 0x0c);
        } else {
            if (be32(t->data + 0x0c) != entry_bytes) return ANLZ_ERR_BAD_LEN;
            claimed = be32(t->data + 0x10);
        }
        uint32_t avail = (t->len_tag - header) / entry_bytes;
        out->kind = kind;
        out->entry_bytes = entry_bytes;
        out->count = claimed < avail ? claimed : avail;
        out->clamped = claimed > avail;
        out->data = t->data + header;
        out->entries_per_sec = WAVES[i].per_sec;
        return ANLZ_OK;
    }
    return ANLZ_ERR_BAD_LEN;
}

int anlz_wave_column(const anlz_waveform_t *w, uint32_t i, anlz_wave_column_t *out)
{
    if (!w || !out || i >= w->count) return 0;
    memset(out, 0, sizeof *out);
    const uint8_t *e = w->data + (size_t)i * w->entry_bytes;
    memcpy(out->raw, e, w->entry_bytes);
    switch (w->kind) {
        case ANLZ_WAVE_PREVIEW:
        case ANLZ_WAVE_DETAIL:
            out->height = e[0] & 0x1f;
            out->whiteness = e[0] >> 5;
            break;
        case ANLZ_WAVE_TINY:
            out->height = e[0] & 0x0f;
            break;
        case ANLZ_WAVE_COLOR_DETAIL: {
            uint16_t v = be16(e);
            out->red = (uint8_t)(((v >> 13) & 7) * 255 / 7);
            out->green = (uint8_t)(((v >> 10) & 7) * 255 / 7);
            out->blue = (uint8_t)(((v >> 7) & 7) * 255 / 7);
            out->height = (v >> 2) & 0x1f;
            break;
        }
        case ANLZ_WAVE_3BAND_PREVIEW:
        case ANLZ_WAVE_3BAND_DETAIL:
            out->mid = e[0];
            out->high = e[1];
            out->low = e[2];
            out->height = out->mid > out->high ? (out->mid > out->low ? out->mid : out->low)
                                               : (out->high > out->low ? out->high : out->low);
            break;
        case ANLZ_WAVE_COLOR_PREVIEW:      /* only the raw bytes are known */
            break;
    }
    return 1;
}

int32_t anlz_wave_index_for_time(const anlz_waveform_t *w, uint32_t time_ms, uint32_t track_ms)
{
    if (!w || w->count == 0) return -1;
    uint64_t idx;
    if (w->entries_per_sec > 0.0) {
        idx = (uint64_t)((double)time_ms * w->entries_per_sec / 1000.0);
    } else {
        if (track_ms == 0) return -1;
        idx = (uint64_t)time_ms * w->count / track_ms;
    }
    return idx >= w->count ? (int32_t)(w->count - 1) : (int32_t)idx;
}

/* ---- whole track ---- */

struct anlz_track {
    anlz_t *file[3];
};

static int has_dotdot_component(const char *p)
{
    for (const char *c = p; *c; ) {
        const char *end = strchr(c, '/');
        size_t n = end ? (size_t)(end - c) : strlen(c);
        if (n == 2 && c[0] == '.' && c[1] == '.') return 1;
        if (!end) break;
        c = end + 1;
    }
    return 0;
}

static int ends_with_dat(const char *p)
{
    size_t n = strlen(p);
    if (n < 5 || p[n - 4] != '.') return 0;
    const char *e = p + n - 3;
    return (e[0] == 'D' || e[0] == 'd') && (e[1] == 'A' || e[1] == 'a') && (e[2] == 'T' || e[2] == 't');
}

static int resolve_path(const char *root, const char *analyze_path, const char *ext, char *out, size_t outlen)
{
    if (!root || !analyze_path || !out || outlen == 0) return -1;
    if (analyze_path[0] != '/' || strchr(analyze_path, '\\') || has_dotdot_component(analyze_path)) return -1;
    if (!ends_with_dat(analyze_path)) return -1;
    size_t rl = strlen(root), al = strlen(analyze_path);
    while (rl > 0 && root[rl - 1] == '/') rl--;                    /* "/mnt/usb/" + "/PIONEER/..." */
    if (rl + al + 1 > outlen) return -1;
    memcpy(out, root, rl);
    memcpy(out + rl, analyze_path, al - 3);
    memcpy(out + rl + al - 3, ext, 3);
    out[rl + al] = '\0';
    return 0;
}

int anlz_resolve_path(const char *root, const char *analyze_path, anlz_file_t which, char *out, size_t outlen)
{
    static const char *const EXTS[3] = { "DAT", "EXT", "2EX" };
    if ((int)which < 0 || (int)which > 2) return -1;
    return resolve_path(root, analyze_path, EXTS[which], out, outlen);
}

static anlz_t *open_variant(const char *root, const char *analyze_path, int which, anlz_err_t *err)
{
    static const char *const UP[3] = { "DAT", "EXT", "2EX" };
    static const char *const LOW[3] = { "dat", "ext", "2ex" };
    char path[4096];
    anlz_t *a = NULL;
    for (int v = 0; v < 2; v++) {
        if (resolve_path(root, analyze_path, v ? LOW[which] : UP[which], path, sizeof path) != 0) return NULL;
        anlz_err_t e = anlz_open_file(path, &a);
        if (e == ANLZ_OK) return a;
        if (e != ANLZ_ERR_IO && err && *err == ANLZ_ERR_IO) *err = e;   /* remember a parse error over "missing" */
    }
    return NULL;
}

anlz_err_t anlz_track_open(const char *root, const char *analyze_path, anlz_track_t **out)
{
    if (!out) return ANLZ_ERR_BAD_LEN;
    *out = NULL;
    char probe[4096];
    if (anlz_resolve_path(root, analyze_path, ANLZ_FILE_DAT, probe, sizeof probe) != 0) return ANLZ_ERR_BAD_LEN;

    anlz_track_t *t = calloc(1, sizeof *t);
    if (!t) return ANLZ_ERR_NOMEM;
    anlz_err_t first = ANLZ_ERR_IO;
    int any = 0;
    for (int w = 0; w < 3; w++) {
        anlz_err_t err = ANLZ_ERR_IO;
        t->file[w] = open_variant(root, analyze_path, w, &err);
        if (t->file[w]) any = 1;
        else if (w == 0) first = err;
    }
    if (!any) { free(t); return first; }
    *out = t;
    return ANLZ_OK;
}

void anlz_track_close(anlz_track_t *t)
{
    if (!t) return;
    for (int i = 0; i < 3; i++) anlz_close(t->file[i]);
    free(t);
}

const anlz_t *anlz_track_file(const anlz_track_t *t, anlz_file_t which)
{
    return (t && (int)which >= 0 && (int)which <= 2) ? t->file[which] : NULL;
}

anlz_err_t anlz_track_path(const anlz_track_t *t, const char **utf8)
{
    if (!t || !utf8) return ANLZ_ERR_BAD_LEN;
    anlz_err_t err = ANLZ_ERR_ABSENT;
    for (int i = 0; i < 3; i++) {
        if (!t->file[i]) continue;
        err = anlz_path(t->file[i], utf8);
        if (err == ANLZ_OK) return ANLZ_OK;
    }
    *utf8 = NULL;
    return err;
}

anlz_err_t anlz_track_beatgrid(const anlz_track_t *t, anlz_beatgrid_t *out)
{
    if (!t || !out) return ANLZ_ERR_BAD_LEN;
    memset(out, 0, sizeof *out);
    anlz_beatgrid_t dat_grid;
    anlz_err_t dat_err = ANLZ_ERR_ABSENT;
    memset(&dat_grid, 0, sizeof dat_grid);
    if (t->file[ANLZ_FILE_DAT]) {
        dat_err = anlz_beatgrid(t->file[ANLZ_FILE_DAT], &dat_grid);
        if (dat_err == ANLZ_OK && dat_grid.source == ANLZ_GRID_PQTZ) {
            if (t->file[ANLZ_FILE_EXT]) anlz_beatgrid_attach_sub_ms(&dat_grid, t->file[ANLZ_FILE_EXT]);   /* optional */
            *out = dat_grid;
            return ANLZ_OK;
        }
    }
    if (t->file[ANLZ_FILE_EXT]) {
        anlz_err_t e = anlz_beatgrid(t->file[ANLZ_FILE_EXT], out);
        if (e == ANLZ_OK) return ANLZ_OK;
        if (dat_err != ANLZ_OK) return e;
    }
    if (dat_err == ANLZ_OK) { *out = dat_grid; return ANLZ_OK; }
    memset(out, 0, sizeof *out);
    return dat_err;
}

anlz_err_t anlz_track_cues(const anlz_track_t *t, anlz_cue_kind_t kind, anlz_cue_list_t *out)
{
    if (!t || !out) return ANLZ_ERR_BAD_LEN;
    memset(out, 0, sizeof *out);
    const anlz_t *ext = t->file[ANLZ_FILE_EXT], *dat = t->file[ANLZ_FILE_DAT];
    if (ext && find_tag(ext, "PCO2", 0)) return anlz_cues_from(ext, kind, 1, out);

    anlz_cue_list_t l[2] = { { 0, NULL, 0 }, { 0, NULL, 0 } };
    int got = 0;
    anlz_err_t err = ANLZ_ERR_ABSENT, first_hard = ANLZ_OK;
    const anlz_t *src[2] = { dat, ext };
    for (int i = 0; i < 2; i++) {
        if (!src[i]) continue;
        err = anlz_cues_from(src[i], kind, 0, &l[i]);
        if (err == ANLZ_OK) got = 1;
        else if (err != ANLZ_ERR_ABSENT && first_hard == ANLZ_OK) first_hard = err;
    }
    if (!got) return first_hard != ANLZ_OK ? first_hard : ANLZ_ERR_ABSENT;

    uint32_t cap = l[0].count + l[1].count;
    if (cap) {
        out->cues = calloc(cap, sizeof *out->cues);
        if (!out->cues) { anlz_cue_list_free(&l[0]); anlz_cue_list_free(&l[1]); return ANLZ_ERR_NOMEM; }
    }
    for (int i = 0; i < 2; i++) {
        for (uint32_t k = 0; k < l[i].count; k++) {
            int dup = 0;
            for (uint32_t j = 0; j < out->count; j++)
                if (out->cues[j].hot_cue == l[i].cues[k].hot_cue && out->cues[j].time_ms == l[i].cues[k].time_ms
                    && out->cues[j].is_loop == l[i].cues[k].is_loop) dup = 1;
            if (!dup) out->cues[out->count++] = l[i].cues[k];
        }
        if (l[i].damaged) out->damaged = 1;
        anlz_cue_list_free(&l[i]);
    }
    if (out->count > 1) qsort(out->cues, out->count, sizeof *out->cues, cue_cmp);
    return ANLZ_OK;
}

anlz_err_t anlz_track_waveform(const anlz_track_t *t, anlz_wave_kind_t kind, anlz_waveform_t *out)
{
    if (!t || !out) return ANLZ_ERR_BAD_LEN;
    memset(out, 0, sizeof *out);
    anlz_file_t order[2];
    int n = 1;
    switch (kind) {
        case ANLZ_WAVE_PREVIEW:
        case ANLZ_WAVE_TINY:          order[0] = ANLZ_FILE_DAT; break;
        case ANLZ_WAVE_DETAIL:
        case ANLZ_WAVE_COLOR_PREVIEW:
        case ANLZ_WAVE_COLOR_DETAIL:  order[0] = ANLZ_FILE_EXT; break;
        case ANLZ_WAVE_3BAND_PREVIEW: order[0] = ANLZ_FILE_2EX; break;
        case ANLZ_WAVE_3BAND_DETAIL:  order[0] = ANLZ_FILE_2EX; order[1] = ANLZ_FILE_EXT; n = 2; break;
        default: return ANLZ_ERR_BAD_LEN;
    }
    anlz_err_t err = ANLZ_ERR_ABSENT;
    for (int i = 0; i < n; i++) {
        if (!t->file[order[i]]) continue;
        err = anlz_waveform(t->file[order[i]], kind, out);
        if (err == ANLZ_OK) return ANLZ_OK;
    }
    return err;
}

//SNIP_anlz_core
