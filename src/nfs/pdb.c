#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "utf16.h"
#include "pdb.h"

//SNIP_pdb

static uint16_t ru16(const uint8_t *p)
{
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t ru32(const uint8_t *p)
{
    return (uint32_t)(p[0] | ((uint32_t)p[1] << 8)
                    | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24));
}

/*
 * Decode a DeviceSQL string at ptr (up to avail bytes available).
 * Returns heap-allocated UTF-8 or NULL for empty / unrecognised kind.
 * Caller owns the result.
 *
 * Three wire encodings:
 *   0x40  long ASCII:    [0x40][u16le total][0x00][ASCII bytes]
 *   0x90  long UTF-16LE: [0x90][u16le total][0x00][UTF-16LE bytes]
 *   odd   short ASCII:   [kind] where text_len = (kind>>1) - 1
 */
static char *devicesql_to_utf8(const uint8_t *ptr, uint32_t avail)
{
    if (avail < 1) return NULL;
    uint8_t kind = ptr[0];

    if (kind == 0x40) {
        if (avail < 4) return NULL;
        uint16_t total = ru16(ptr + 1);   /* includes 4-byte header */
        if (total < 4 || (uint32_t)total > avail) return NULL;
        int tlen = total - 4;
        if (tlen <= 0) return NULL;
        char *s = malloc((size_t)tlen + 1);
        if (!s) return NULL;
        memcpy(s, ptr + 4, (size_t)tlen);
        s[tlen] = '\0';
        return s;

    } else if (kind == 0x90) {
        if (avail < 4) return NULL;
        uint16_t total = ru16(ptr + 1);   /* includes 4-byte header */
        if (total < 4 || (uint32_t)total > avail) return NULL;
        int blen = total - 4;
        if (blen <= 0) return NULL;
        /* UTF-16LE → UTF-8: worst case 3× expansion */
        char *s = malloc((size_t)blen * 3 + 1);
        if (!s) return NULL;
        if (utf16le_decode(ptr + 4, (uint32_t)blen, s, (uint32_t)(blen * 3 + 1)) < 0) {
            free(s);
            return NULL;
        }
        return s;

    } else {
        /* Short ASCII: total byte count (including kind byte) = kind >> 1 */
        int total = kind >> 1;
        if (total < 1 || (uint32_t)total > avail) return NULL;
        int tlen = total - 1;
        if (tlen <= 0) return NULL;
        char *s = malloc((size_t)tlen + 1);
        if (!s) return NULL;
        memcpy(s, ptr + 1, (size_t)tlen);
        s[tlen] = '\0';
        return s;
    }
}

/*
 * Parse a single track row.
 *
 * Fixed fields occupy the first 94 bytes, followed by 21 * u16 string offsets
 * (42 bytes), totalling 136 bytes minimum before the heap strings.
 *
 * String slot indices (from rekordbox-db-format spec):
 *   [10] date_added  [11] release_date  [12] mix_name
 *   [14] analyze_path                   [16] comment
 *   [17] title       [19] filename      [20] file_path
 */
#define TR_FIXED       94
#define TR_N_STRINGS   21
#define TR_ROW_MIN     (TR_FIXED + TR_N_STRINGS * 2)   /* 136 bytes */

static int parse_track_row_cb(const uint8_t *row_base, uint32_t row_avail,
                              pdb_track_cb cb, void *ud)
{
    if (row_avail < TR_ROW_MIN) return 0;

    pdb_track_t t = {0};
    t.sample_rate   = ru32(row_base + 8);
    t.file_size     = ru32(row_base + 16);
    t.artwork_id    = ru32(row_base + 28);
    t.key_id        = ru32(row_base + 32);
    t.label_id      = ru32(row_base + 40);
    t.remixer_id    = ru32(row_base + 44);
    t.bitrate       = ru32(row_base + 48);
    t.track_number  = ru32(row_base + 52);
    t.bpm_x100      = ru32(row_base + 56);
    t.genre_id      = ru32(row_base + 60);
    t.album_id      = ru32(row_base + 64);
    t.artist_id     = ru32(row_base + 68);
    t.id            = ru32(row_base + 72);
    t.disc_number   = ru16(row_base + 76);
    t.play_count    = ru16(row_base + 78);
    t.year          = ru16(row_base + 80);
    t.sample_depth  = ru16(row_base + 82);
    t.duration_secs = ru16(row_base + 84);
    t.color_id      = row_base[88];
    t.rating        = row_base[89];

    for (int i = 0; i < TR_N_STRINGS; i++) {
        uint16_t ofs = ru16(row_base + TR_FIXED + i * 2);
        if ((uint32_t)ofs >= row_avail) continue;
        uint32_t avail = row_avail - (uint32_t)ofs;
        const uint8_t *sp = row_base + ofs;
        char **dst = NULL;
        switch (i) {
            case 10: dst = &t.date_added;    break;
            case 11: dst = &t.release_date;  break;
            case 12: dst = &t.mix_name;      break;
            case 14: dst = &t.analyze_path;  break;
            case 16: dst = &t.comment;       break;
            case 17: dst = &t.title;         break;
            case 19: dst = &t.filename;      break;
            case 20: dst = &t.file_path;     break;
            default: break;
        }
        if (dst) *dst = devicesql_to_utf8(sp, avail);
    }

    int rc = cb(&t, ud);

    free(t.title);        free(t.filename);     free(t.file_path);
    free(t.comment);      free(t.date_added);   free(t.release_date);
    free(t.mix_name);     free(t.analyze_path);
    return rc;
}

/* Internal per-row callback: row points at the row start, avail = bytes to end of page. */
typedef int (*row_fn)(const uint8_t *row, uint32_t avail, void *ctx);

/*
 * Parse all present rows from a single data page.
 * Heap starts at offset 40 within the page.
 * Row index is built backwards from the end of the page in groups of 16.
 */
static int parse_page_rows(const uint8_t *page, uint32_t len_page, row_fn fn, void *ctx)
{
    const uint32_t heap_off = 40;

    uint32_t packed = page[24] | ((uint32_t)page[25] << 8) | ((uint32_t)page[26] << 16);
    uint16_t num_row_offsets = packed & 0x1FFF;
    if (num_row_offsets == 0) return 0;

    uint32_t num_row_groups = ((uint32_t)num_row_offsets - 1) / 16 + 1;

    for (uint32_t g = 0; g < num_row_groups; g++) {
        uint32_t base = len_page - g * 0x24;
        if (base < 6) break;

        uint16_t present = ru16(page + base - 4);
        uint32_t max_i = (g == num_row_groups - 1)
                         ? (uint32_t)(num_row_offsets - g * 16)
                         : 16;

        for (uint32_t i = 0; i < max_i; i++) {
            if (!(present & (1u << i))) continue;
            if (base < 6 + i * 2) break;
            uint16_t ofs = ru16(page + base - 6 - i * 2);
            uint32_t row_start = heap_off + (uint32_t)ofs;
            if (row_start >= len_page) continue;
            int rc = fn(page + row_start, len_page - row_start, ctx);
            if (rc != 0) return rc;
        }
    }
    return 0;
}

/*
 * Walk the page chain of a table starting at first_page.
 *
 * The first page of any chain has page_flags 0x40 set (non-data page) and
 * contains no rows. Subsequent data pages have 0x40 clear.
 * Iteration stops after last_page, when next_page is out of range or the type changes.
 */
static int walk_table(const uint8_t *data, uint32_t file_len, uint32_t len_page,
                      uint32_t first_page, uint32_t last_page, uint32_t type,
                      row_fn fn, void *ctx)
{
    uint32_t max_pages = file_len / len_page;
    uint32_t page_index = first_page;
    uint32_t iterations = 0;

    while ((uint64_t)page_index * len_page + 40 <= (uint64_t)file_len
           && (uint64_t)(page_index + 1) * len_page <= (uint64_t)file_len
           && iterations++ < max_pages)
    {
        const uint8_t *page = data + (size_t)page_index * len_page;

        uint32_t pg_type = ru32(page + 8);
        if (pg_type != type) break;

        /* 0x40 set = non-data page (first/empty); 0x40 clear = data page */
        if (!(page[27] & 0x40)) {
            int rc = parse_page_rows(page, len_page, fn, ctx);
            if (rc != 0) return rc;
        }

        if (page_index == last_page) break;
        uint32_t next = ru32(page + 12);
        if (next == page_index) break;   /* self-loop sentinel */
        page_index = next;
    }
    return 0;
}

int pdb_foreach_row(const uint8_t *data, uint32_t len, uint32_t table_type,
                    pdb_row_cb cb, void *ud)
{
    if (!data || len < 32 || !cb) return -1;

    uint32_t len_page   = ru32(data + 4);
    uint32_t num_tables = ru32(data + 8);
    if (len_page < 64 || num_tables == 0) return -1;
    if (28 + (uint64_t)num_tables * 16 > (uint64_t)len) return -1;

    for (uint32_t i = 0; i < num_tables; i++) {
        const uint8_t *tbl = data + 28 + i * 16;
        if (ru32(tbl + 0) != table_type) continue;
        return walk_table(data, len, len_page, ru32(tbl + 8), ru32(tbl + 12),
                          table_type, (row_fn)cb, ud);
    }
    return 0;   /* table absent — not an error */
}

/* ---- typed tables ---- */

typedef struct { pdb_track_cb cb; void *ud; } track_ctx_t;

static int track_row(const uint8_t *row, uint32_t avail, void *ctx)
{
    track_ctx_t *c = ctx;
    return parse_track_row_cb(row, avail, c->cb, c->ud);
}

int pdb_parse(const uint8_t *data, uint32_t len, pdb_track_cb cb, void *ud)
{
    track_ctx_t c = { cb, ud };
    return pdb_foreach_row(data, len, PDB_PAGE_TRACKS, track_row, &c);
}

/* Bounds-checked string at row + ofs; NULL when out of range. */
static char *row_string(const uint8_t *row, uint32_t avail, uint32_t ofs)
{
    if (ofs >= avail) return NULL;
    return devicesql_to_utf8(row + ofs, avail - ofs);
}

typedef struct { void *cb; void *ud; } named_ctx_t;

/* genres(1), labels(4): id u32, name @4 */
static int genre_row(const uint8_t *row, uint32_t avail, void *ctx)
{
    named_ctx_t *c = ctx;
    if (avail < 6) return 0;
    pdb_named_t n = { ru32(row), NULL };
    n.name = row_string(row, avail, 4);
    int rc = ((pdb_named_cb)c->cb)(&n, c->ud);
    free(n.name);
    return rc;
}

/* keys(5): id u32, id2 u32, name @8 */
static int key_row(const uint8_t *row, uint32_t avail, void *ctx)
{
    named_ctx_t *c = ctx;
    if (avail < 10) return 0;
    pdb_named_t n = { ru32(row), NULL };
    n.name = row_string(row, avail, 8);
    int rc = ((pdb_named_cb)c->cb)(&n, c->ud);
    free(n.name);
    return rc;
}

/* colors(6): u32 + u8 unknown, id u16 @5, u8 unknown, name @8 */
static int color_row(const uint8_t *row, uint32_t avail, void *ctx)
{
    named_ctx_t *c = ctx;
    if (avail < 10) return 0;
    pdb_named_t n = { ru16(row + 5), NULL };
    n.name = row_string(row, avail, 8);
    int rc = ((pdb_named_cb)c->cb)(&n, c->ud);
    free(n.name);
    return rc;
}

/* artists(2): subtype u16, index_shift u16, id u32, 0x03, ofs_near u8 @9, ofs_far u16 @10 */
static int artist_row(const uint8_t *row, uint32_t avail, void *ctx)
{
    named_ctx_t *c = ctx;
    if (avail < 10) return 0;
    uint16_t subtype = ru16(row);
    uint32_t ofs = (subtype & 0x04) && avail >= 12 ? ru16(row + 10) : row[9];
    pdb_named_t n = { ru32(row + 4), NULL };
    n.name = row_string(row, avail, ofs);
    int rc = ((pdb_named_cb)c->cb)(&n, c->ud);
    free(n.name);
    return rc;
}

/* albums(3): magic u16, index_shift u16, u32, artist_id @8, id @12, u32, 0x03, ofs_name u8 @21 */
static int album_row(const uint8_t *row, uint32_t avail, void *ctx)
{
    named_ctx_t *c = ctx;
    if (avail < 22) return 0;
    pdb_album_t a = { ru32(row + 12), ru32(row + 8), NULL };
    a.name = row_string(row, avail, row[21]);
    int rc = ((pdb_album_cb)c->cb)(&a, c->ud);
    free(a.name);
    return rc;
}

/* playlist tree(7): parent_id, u32, sort_order, id, is_folder (all u32), name @20 */
static int playlist_row(const uint8_t *row, uint32_t avail, void *ctx)
{
    named_ctx_t *c = ctx;
    if (avail < 21) return 0;
    pdb_playlist_t p = { ru32(row + 12), ru32(row), ru32(row + 8), ru32(row + 16) != 0, NULL };
    p.name = row_string(row, avail, 20);
    int rc = ((pdb_playlist_cb)c->cb)(&p, c->ud);
    free(p.name);
    return rc;
}

/* playlist entries(8): entry_index, track_id, playlist_id (u32) */
static int playlist_entry_row(const uint8_t *row, uint32_t avail, void *ctx)
{
    named_ctx_t *c = ctx;
    if (avail < 12) return 0;
    pdb_playlist_entry_t e = { ru32(row), ru32(row + 4), ru32(row + 8) };
    return ((pdb_playlist_entry_cb)c->cb)(&e, c->ud);
}

/* artwork(13): id u32, path @4 */
static int artwork_row(const uint8_t *row, uint32_t avail, void *ctx)
{
    return genre_row(row, avail, ctx);
}

#define PDB_TYPED(fname, cbtype, table, rowfn) \
int fname(const uint8_t *data, uint32_t len, cbtype cb, void *ud) \
{ \
    named_ctx_t c = { (void *)cb, ud }; \
    return pdb_foreach_row(data, len, table, (pdb_row_cb)rowfn, &c); \
}

PDB_TYPED(pdb_parse_genres,   pdb_named_cb,          PDB_PAGE_GENRES,           genre_row)
PDB_TYPED(pdb_parse_artists,  pdb_named_cb,          PDB_PAGE_ARTISTS,          artist_row)
PDB_TYPED(pdb_parse_albums,   pdb_album_cb,          PDB_PAGE_ALBUMS,           album_row)
PDB_TYPED(pdb_parse_labels,   pdb_named_cb,          PDB_PAGE_LABELS,           genre_row)
PDB_TYPED(pdb_parse_keys,     pdb_named_cb,          PDB_PAGE_KEYS,             key_row)
PDB_TYPED(pdb_parse_colors,   pdb_named_cb,          PDB_PAGE_COLORS,           color_row)
PDB_TYPED(pdb_parse_playlists,pdb_playlist_cb,       PDB_PAGE_PLAYLIST_TREE,    playlist_row)
PDB_TYPED(pdb_parse_playlist_entries, pdb_playlist_entry_cb, PDB_PAGE_PLAYLIST_ENTRIES, playlist_entry_row)
PDB_TYPED(pdb_parse_artwork,  pdb_named_cb,          PDB_PAGE_ARTWORK,          artwork_row)

//SNIP_pdb

uint8_t *pdb_read_file(const char *path, uint32_t *len_out)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;

    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    rewind(f);
    if (sz <= 0) { fclose(f); return NULL; }

    uint8_t *buf = malloc((size_t)sz);
    if (!buf) { fclose(f); return NULL; }

    if ((long)fread(buf, 1, (size_t)sz, f) != sz) {
        free(buf);
        fclose(f);
        return NULL;
    }
    fclose(f);
    if (len_out) *len_out = (uint32_t)sz;
    return buf;
}

int pdb_parse_file(const char *path, pdb_track_cb cb, void *ud)
{
    FILE *f = fopen(path, "rb");
    if (!f) return -1;

    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    rewind(f);
    if (sz <= 0) { fclose(f); return -1; }

    uint8_t *buf = malloc((size_t)sz);
    if (!buf) { fclose(f); return -1; }

    if ((long)fread(buf, 1, (size_t)sz, f) != sz) {
        free(buf);
        fclose(f);
        return -1;
    }
    fclose(f);

    int rc = pdb_parse(buf, (uint32_t)sz, cb, ud);
    free(buf);
    return rc;
}
