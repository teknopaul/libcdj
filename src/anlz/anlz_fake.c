#include <stdlib.h>
#include <string.h>
#include "anlz_fake.h"

//SNIP_fake_anlz

struct anlz_fake {
    uint8_t *buf;
    uint32_t len;
    uint32_t cap;
};

static void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v;
}

static int reserve(anlz_fake_t *b, uint32_t extra)
{
    if (b->len + extra <= b->cap) return 1;
    uint32_t cap = b->cap ? b->cap : 256;
    while (cap < b->len + extra) cap *= 2;
    uint8_t *n = realloc(b->buf, cap);
    if (!n) return 0;
    b->buf = n;
    b->cap = cap;
    return 1;
}

anlz_fake_t *anlz_fake_begin(void)
{
    anlz_fake_t *b = calloc(1, sizeof *b);
    if (!b || !reserve(b, 28)) { free(b); return NULL; }
    memset(b->buf, 0, 28);
    memcpy(b->buf, "PMAI", 4);
    put32(b->buf + 4, 0x1c);
    put32(b->buf + 12, 1);          /* the unknown words seen in real files */
    put32(b->buf + 16, 0x00010000);
    put32(b->buf + 20, 0x00010000);
    b->len = 28;
    return b;
}

void anlz_fake_add_tag(anlz_fake_t *b, const char fourcc[4], uint32_t len_header,
                       const uint8_t *body, uint32_t body_len)
{
    if (!b || !reserve(b, 12 + body_len)) return;
    uint8_t *p = b->buf + b->len;
    memcpy(p, fourcc, 4);
    put32(p + 4, len_header);
    put32(p + 8, 12 + body_len);
    if (body_len) memcpy(p + 12, body, body_len);
    b->len += 12 + body_len;
}

/* UTF-8 -> UTF-16BE with a trailing NUL; returns the byte count. dst must hold 4 * strlen + 2. */
static uint32_t utf8_to_utf16be(const char *s, uint8_t *dst)
{
    uint32_t n = 0;
    const uint8_t *p = (const uint8_t *)s;
    while (*p) {
        uint32_t c, extra;
        if (*p < 0x80) { c = *p; extra = 0; }
        else if ((*p & 0xE0) == 0xC0) { c = *p & 0x1F; extra = 1; }
        else if ((*p & 0xF0) == 0xE0) { c = *p & 0x0F; extra = 2; }
        else { c = *p & 0x07; extra = 3; }
        p++;
        while (extra-- && (*p & 0xC0) == 0x80) c = (c << 6) | (*p++ & 0x3F);
        if (c >= 0x10000) {
            uint32_t v = c - 0x10000;
            uint32_t hi = 0xD800 + (v >> 10), lo = 0xDC00 + (v & 0x3FF);
            dst[n++] = (uint8_t)(hi >> 8); dst[n++] = (uint8_t)hi;
            dst[n++] = (uint8_t)(lo >> 8); dst[n++] = (uint8_t)lo;
        } else {
            dst[n++] = (uint8_t)(c >> 8); dst[n++] = (uint8_t)c;
        }
    }
    dst[n++] = 0; dst[n++] = 0;
    return n;
}

void anlz_fake_add_ppth(anlz_fake_t *b, const char *utf8_path)
{
    size_t max = strlen(utf8_path) * 4 + 2;
    uint8_t *body = malloc(4 + max);
    if (!body) return;
    uint32_t n = utf8_to_utf16be(utf8_path, body + 4);
    put32(body, n);                                   /* len_path incl. NUL */
    anlz_fake_add_tag(b, "PPTH", 0x10, body, 4 + n);
    free(body);
}

static void put16(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v;
}

void anlz_fake_add_pqtz(anlz_fake_t *b, const anlz_fake_beat_t *beats, uint32_t n, uint32_t len_beats_field)
{
    uint32_t body_len = 12 + n * 8;                   /* from 0x0c: unk1, unk2, len_beats, entries */
    uint8_t *body = calloc(1, body_len ? body_len : 1);
    if (!body) return;
    put32(body + 4, 0x00080000);                      /* unknown2, as seen in real files */
    put32(body + 8, len_beats_field);
    for (uint32_t i = 0; i < n; i++) {
        uint8_t *e = body + 12 + i * 8;
        put16(e, beats[i].number);
        put16(e + 2, beats[i].bpm_x100);
        put32(e + 4, beats[i].time_ms);
    }
    anlz_fake_add_tag(b, "PQTZ", 0x18, body, body_len);
    free(body);
}

void anlz_fake_add_pqt2(anlz_fake_t *b, anlz_fake_beat_t first, anlz_fake_beat_t last,
                        uint32_t count_field, const uint16_t *sub_us, uint32_t n)
{
    uint32_t body_len = 44 + n * 2;                   /* 0x0c..0x37 header words, then fractions */
    uint8_t *body = calloc(1, body_len);
    if (!body) return;
    put32(body + 4, 0x01000002);                      /* 0x10: constant in real files */
    put16(body + 0x18 - 12, first.number); put16(body + 0x1a - 12, first.bpm_x100); put32(body + 0x1c - 12, first.time_ms);
    put16(body + 0x20 - 12, last.number);  put16(body + 0x22 - 12, last.bpm_x100);  put32(body + 0x24 - 12, last.time_ms);
    put32(body + 0x28 - 12, count_field);
    for (uint32_t i = 0; i < n; i++) put16(body + 44 + i * 2, sub_us[i]);
    anlz_fake_add_tag(b, "PQT2", 0x38, body, body_len);
    free(body);
}

void anlz_fake_add_pcob(anlz_fake_t *b, uint32_t list_type, const anlz_fake_cue_t *cues, uint32_t n, uint32_t len_cues_field)
{
    uint32_t body_len = 12 + n * 0x38;                /* from 0x0c: type, unk+len_cues, memory_count, entries */
    uint8_t *body = calloc(1, body_len);
    if (!body) return;
    put32(body, list_type);
    put16(body + 6, len_cues_field);                  /* 0x12 */
    put32(body + 8, 0xffffffff);                      /* 0x14 */
    for (uint32_t i = 0; i < n; i++) {
        uint8_t *e = body + 12 + i * 0x38;            /* entry starts at tag offset 0x18 */
        memcpy(e, "PCPT", 4);
        put32(e + 4, 0x1c);
        put32(e + 8, 0x38);
        put32(e + 0x0c, cues[i].hot_cue);
        put32(e + 0x10, cues[i].status);
        put32(e + 0x18, 0xffffffff);
        e[0x1c] = cues[i].is_loop ? 2 : 1;
        { uint32_t m = cues[i].marker ? cues[i].marker : 1000; e[0x1d] = (uint8_t)(m >> 16); e[0x1e] = (uint8_t)(m >> 8); e[0x1f] = (uint8_t)m; }
        put32(e + 0x20, cues[i].time_ms);
        put32(e + 0x24, cues[i].loop_time_ms);
    }
    anlz_fake_add_tag(b, "PCOB", 0x18, body, body_len);
    free(body);
}

void anlz_fake_add_pco2(anlz_fake_t *b, uint32_t list_type, const anlz_fake_cue_t *cues, uint32_t n, uint32_t len_cues_field)
{
    uint32_t body_len = 8;                            /* from 0x0c: type, len_cues + unknown */
    for (uint32_t i = 0; i < n; i++)
        body_len += 0x2c + (cues[i].comment ? (uint32_t)strlen(cues[i].comment) * 4 + 2 : 0) + (cues[i].has_rgb ? 4 : 0);
    uint8_t *body = calloc(1, body_len);
    if (!body) return;
    put32(body, list_type);
    put16(body + 4, len_cues_field);                  /* 0x10 */
    uint32_t pos = 8;
    for (uint32_t i = 0; i < n; i++) {
        uint8_t *e = body + pos;
        uint32_t comment_bytes = 0;
        uint8_t utf16[1024];
        if (cues[i].comment) comment_bytes = utf8_to_utf16be(cues[i].comment, utf16);
        uint32_t len = 0x2c + comment_bytes + (cues[i].has_rgb ? 4 : 0);
        memcpy(e, "PCP2", 4);
        put32(e + 4, 0x10);
        put32(e + 8, len);
        put32(e + 0x0c, cues[i].hot_cue);
        e[0x10] = cues[i].is_loop ? 2 : 1;
        { uint32_t m = cues[i].marker ? cues[i].marker : 1000; e[0x11] = (uint8_t)(m >> 16); e[0x12] = (uint8_t)(m >> 8); e[0x13] = (uint8_t)m; }
        put32(e + 0x14, cues[i].time_ms);
        put32(e + 0x18, cues[i].loop_time_ms);
        e[0x1c] = cues[i].color_id;
        memcpy(e + 0x1d, cues[i].raw_1d, 3);
        put16(e + 0x24, cues[i].loop_num);
        put16(e + 0x26, cues[i].loop_den);
        put32(e + 0x28, comment_bytes);
        if (comment_bytes) memcpy(e + 0x2c, utf16, comment_bytes);
        if (cues[i].has_rgb) {
            uint8_t *c = e + 0x2c + comment_bytes;
            c[0] = cues[i].color_code; c[1] = cues[i].red; c[2] = cues[i].green; c[3] = cues[i].blue;
        }
        pos += len;
    }
    /* entry positions above are relative to the tag start + 0x14: body starts at 0x0c, entries at 0x14 = body + 8 */
    anlz_fake_add_tag(b, "PCO2", 0x14, body, body_len);
    free(body);
}

void anlz_fake_add_wave(anlz_fake_t *b, const char fourcc[4], uint32_t entry_bytes, uint32_t count_field,
                        const uint8_t *data, uint32_t data_len)
{
    int short_header = memcmp(fourcc, "PWAV", 4) == 0 || memcmp(fourcc, "PWV2", 4) == 0 || memcmp(fourcc, "PWV6", 4) == 0;
    int preview_form = memcmp(fourcc, "PWAV", 4) == 0 || memcmp(fourcc, "PWV2", 4) == 0;
    uint32_t hdr = short_header ? 0x14 : 0x18;
    uint32_t body_len = (hdr - 12) + data_len;
    uint8_t *body = calloc(1, body_len ? body_len : 1);
    if (!body) return;
    if (preview_form) {
        put32(body, count_field);                      /* 0x0c len_preview */
        put32(body + 4, 0x00100000);                   /* 0x10 unknown, as in real files */
    } else {
        put32(body, entry_bytes);                      /* 0x0c */
        put32(body + 4, count_field);                  /* 0x10 */
        if (!short_header) put32(body + 8, 0x00960000);   /* 0x14 unknown */
    }
    if (data_len) memcpy(body + (hdr - 12), data, data_len);
    anlz_fake_add_tag(b, fourcc, hdr, body, body_len);
    free(body);
}

uint8_t *anlz_fake_finish(anlz_fake_t *b, uint32_t *len_out)
{
    if (!b) return NULL;
    put32(b->buf + 8, b->len);      /* len_file */
    uint8_t *out = b->buf;
    if (len_out) *len_out = b->len;
    free(b);
    return out;
}

//SNIP_fake_anlz
