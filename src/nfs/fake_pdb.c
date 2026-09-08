/*
 * Minimal rekordbox export.pdb generator.
 *
 * Produces a 2-page (8192-byte) PDB file with one track row.
 * Format documented in references/crate-digger/src/main/kaitai/rekordbox_pdb.ksy
 *
 * All integers are little-endian as required by the DeviceSQL format.
 */

#include <stdlib.h>
#include <string.h>
#include "fake_pdb.h"

//SNIP_fake_pdb

#define PAGE_SIZE    4096
#define FILE_PAGES   2     /* page 0 = file header, page 1 = tracks */
#define FILE_SIZE    (PAGE_SIZE * FILE_PAGES)

#define PAGE_TYPE_TRACKS  0

/* Offsets within the file header page (page 0). */
#define HDR_UNKNOWN       0x00   /* u4 = 0 */
#define HDR_LEN_PAGE      0x04   /* u4 = PAGE_SIZE */
#define HDR_NUM_TABLES    0x08   /* u4 */
#define HDR_NEXT_UNUSED   0x0C   /* u4 = FILE_PAGES */
#define HDR_UNKNOWN2      0x10   /* u4 = 0 */
#define HDR_SEQUENCE      0x14   /* u4 = 0 */
#define HDR_GAP           0x18   /* 4 bytes = 0 */
#define HDR_TABLE0        0x1C   /* 16 bytes: type + empty_candidate + first_page + last_page */

/* Offsets within data pages. */
#define PG_GAP            0x00   /* 4 bytes = 0 */
#define PG_PAGE_INDEX     0x04   /* u4 */
#define PG_TYPE           0x08   /* u4 */
#define PG_NEXT_PAGE      0x0C   /* u4 */
#define PG_SEQUENCE       0x10   /* u4 = 0 */
#define PG_UNKNOWN        0x14   /* 4 bytes = 0 */
/* bit-packed at 0x18: b13 num_row_offsets | b11 num_rows | u8 page_flags (4 bytes total) */
#define PG_BITPACK        0x18
#define PG_FREE_SIZE      0x1C   /* u2 */
#define PG_USED_SIZE      0x1E   /* u2 */
#define PG_TXN_ROW_COUNT  0x20   /* u2 */
#define PG_TXN_ROW_IDX    0x22   /* u2 */
#define PG_UNKNOWN2       0x24   /* u2 = 0 */
#define PG_UNKNOWN3       0x26   /* u2 = 0 */
#define PG_HEAP           0x28   /* heap starts here */

/* Row index at the end of a page (one row group, group_index=0):
 *   base = PAGE_SIZE - (0 * 0x24) = PAGE_SIZE
 *   transaction_row_flags at base-2  (crate-digger reads at base, likely ±2 difference)
 *   row_present_flags      at base-4
 *   row 0 offset           at base-6
 */
#define ROW_IDX_OFS_ROW0  (PAGE_SIZE - 6)   /* u2 LE: row offset from heap start */
#define ROW_IDX_PRESENT   (PAGE_SIZE - 4)   /* u2 LE: row_present_flags */
#define ROW_IDX_TXN       (PAGE_SIZE - 2)   /* u2 LE: transaction_row_flags */

/* Track row fixed-size fields (all LE). */
#define TR_SUBTYPE        0x00   /* u2 = 0x0024 */
#define TR_INDEX_SHIFT    0x02   /* u2 = 0 */
#define TR_BITMASK        0x04   /* u4 = 0 */
#define TR_SAMPLE_RATE    0x08   /* u4 */
#define TR_COMPOSER_ID    0x0C   /* u4 = 0 */
#define TR_FILE_SIZE      0x10   /* u4 */
#define TR_UNKNOWN1       0x14   /* u4 = 0 */
#define TR_UNKNOWN2       0x18   /* u2 = 19048 */
#define TR_UNKNOWN3       0x1A   /* u2 = 30967 */
#define TR_ARTWORK_ID     0x1C   /* u4 = 0 */
#define TR_KEY_ID         0x20   /* u4 = 0 */
#define TR_ORIG_ARTIST    0x24   /* u4 = 0 */
#define TR_LABEL_ID       0x28   /* u4 = 0 */
#define TR_REMIXER_ID     0x2C   /* u4 = 0 */
#define TR_BITRATE        0x30   /* u4 = 0 */
#define TR_TRACK_NUM      0x34   /* u4 = 0 */
#define TR_TEMPO          0x38   /* u4 = bpm_x100 */
#define TR_GENRE_ID       0x3C   /* u4 = 0 */
#define TR_ALBUM_ID       0x40   /* u4 = 0 */
#define TR_ARTIST_ID      0x44   /* u4 = 0 */
#define TR_ID             0x48   /* u4 = 1 */
#define TR_DISC_NUM       0x4C   /* u2 = 0 */
#define TR_PLAY_COUNT     0x4E   /* u2 = 0 */
#define TR_YEAR           0x50   /* u2 = 0 */
#define TR_SAMPLE_DEPTH   0x52   /* u2 = 0 */
#define TR_DURATION       0x54   /* u2 = duration_secs */
#define TR_UNKNOWN4       0x56   /* u2 = 41 */
#define TR_COLOR_ID       0x58   /* u1 = 0 */
#define TR_RATING         0x59   /* u1 = 0 */
#define TR_UNKNOWN5       0x5A   /* u2 = 1 */
#define TR_UNKNOWN6       0x5C   /* u2 = 2 */
#define TR_OFS_STRINGS    0x5E   /* u2[21] = 42 bytes */
#define TR_FIXED_SIZE     (TR_OFS_STRINGS + 42)  /* = 0x88 = 136 bytes */

/* LE write helpers */
static void pu16(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)(v);
    p[1] = (uint8_t)(v >> 8);
}
static void pu32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v);
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

/*
 * Write a DeviceSQL string into buf[*pos].
 * Empty strings use the 1-byte short-ASCII sentinel 0x03.
 * Non-empty strings use long-ASCII: [0x40][len_u16_le][0x00][text].
 * Returns the number of bytes written.
 */
static uint16_t write_devicesql(uint8_t *buf, uint32_t *pos, const char *s) {
    if (!s || *s == '\0') {
        buf[(*pos)++] = 0x03;
        return 1;
    }
    uint16_t slen  = (uint16_t)strlen(s);
    uint16_t total = (uint16_t)(slen + 4);  /* total DeviceSQL bytes incl. flag */
    buf[(*pos)++] = 0x40;
    buf[(*pos)++] = (uint8_t)(total);
    buf[(*pos)++] = (uint8_t)(total >> 8);
    buf[(*pos)++] = 0x00;
    memcpy(buf + *pos, s, slen);
    *pos += slen;
    return total;
}

uint8_t *fake_pdb_generate(const fake_track_t *track, uint32_t *len_out) {
    uint8_t *buf = calloc(FILE_SIZE, 1);
    if (!buf) return NULL;

    /* ---- Page 0: File header ---- */
    uint8_t *p0 = buf;
    pu32(p0 + HDR_UNKNOWN,     0);
    pu32(p0 + HDR_LEN_PAGE,    PAGE_SIZE);
    pu32(p0 + HDR_NUM_TABLES,  1);
    pu32(p0 + HDR_NEXT_UNUSED, FILE_PAGES);
    /* gap + sequence = 0 (already zeroed by calloc) */

    /* Table 0 entry: tracks, first_page=1, last_page=1 */
    uint8_t *tbl = p0 + HDR_TABLE0;
    pu32(tbl + 0x00, PAGE_TYPE_TRACKS);
    pu32(tbl + 0x04, 2);              /* empty_candidate = page after last */
    pu32(tbl + 0x08, 1);              /* first_page index */
    pu32(tbl + 0x0C, 1);              /* last_page index */

    /* ---- Page 1: Tracks data page ---- */
    uint8_t *p1 = buf + PAGE_SIZE;

    pu32(p1 + PG_PAGE_INDEX, 1);
    pu32(p1 + PG_TYPE,       PAGE_TYPE_TRACKS);
    pu32(p1 + PG_NEXT_PAGE,  FILE_PAGES);  /* points past end = no more pages */

    /* Bit-packed fields at PG_BITPACK:
     *   bits [12:0]  = num_row_offsets = 1
     *   bits [23:13] = num_rows        = 1
     *   bits [31:24] = page_flags      = 0x24 (data page, normal)
     *
     * 24-bit value = (num_rows=1 << 13) | (num_row_offsets=1 << 0)
     *              = 0x002001
     * In LE bytes: 0x01, 0x20, 0x00, then page_flags = 0x24
     */
    p1[PG_BITPACK + 0] = 0x01;
    p1[PG_BITPACK + 1] = 0x20;
    p1[PG_BITPACK + 2] = 0x00;
    p1[PG_BITPACK + 3] = 0x24;  /* page_flags */

    /* The track row starts at the heap (PG_HEAP = 0x28 = 40). */
    uint8_t *row = p1 + PG_HEAP;
    uint32_t heap_pos = PG_HEAP;  /* absolute page offset of heap start */

    /* Fixed fields */
    pu16(row + TR_SUBTYPE,      0x0024);
    pu16(row + TR_INDEX_SHIFT,  0);
    pu32(row + TR_BITMASK,      0);
    pu32(row + TR_SAMPLE_RATE,  44100);
    pu32(row + TR_COMPOSER_ID,  0);
    pu32(row + TR_FILE_SIZE,    0);
    pu16(row + TR_UNKNOWN2,     19048);
    pu16(row + TR_UNKNOWN3,     30967);
    pu32(row + TR_TEMPO,        track->bpm_x100);
    pu32(row + TR_ID,           1);
    pu16(row + TR_DURATION,     (uint16_t)(track->duration_secs));
    pu16(row + TR_UNKNOWN4,     41);
    pu16(row + TR_UNKNOWN5,     1);
    pu16(row + TR_UNKNOWN6,     2);

    /* Variable-length strings start right after fixed part.
     * ofs_strings[N] = offset of string N relative to row_base.
     * row_base = heap_pos + ofs_row = PG_HEAP + 0 = PG_HEAP.
     */
    uint32_t str_pos = heap_pos + TR_FIXED_SIZE;  /* absolute page offset */
    uint16_t ofs[21];
    memset(ofs, 0, sizeof(ofs));

    /* Strings 0-16, 18: empty */
    for (int i = 0; i <= 18; i++) {
        if (i == 17) continue;  /* title placed at [17] below */
        ofs[i] = (uint16_t)(str_pos - heap_pos);
        write_devicesql(p1, &str_pos, NULL);
    }

    /* String 17: title */
    ofs[17] = (uint16_t)(str_pos - heap_pos);
    write_devicesql(p1, &str_pos, track->title);

    /* String 19: filename */
    ofs[19] = (uint16_t)(str_pos - heap_pos);
    write_devicesql(p1, &str_pos, track->filename);

    /* String 20: file_path */
    ofs[20] = (uint16_t)(str_pos - heap_pos);
    write_devicesql(p1, &str_pos, track->file_path);

    /* Write ofs_strings into the fixed row area */
    for (int i = 0; i < 21; i++)
        pu16(row + TR_OFS_STRINGS + i * 2, ofs[i]);

    /* free_size and used_size in page header */
    uint16_t used   = (uint16_t)(str_pos - heap_pos);
    uint16_t free_s = (uint16_t)(ROW_IDX_OFS_ROW0 - str_pos);
    pu16(p1 + PG_FREE_SIZE, free_s);
    pu16(p1 + PG_USED_SIZE, used);
    pu16(p1 + PG_TXN_ROW_COUNT, 1);
    pu16(p1 + PG_TXN_ROW_IDX,   0);

    /* Row index at end of page:
     *   row 0 offset = 0 (row starts at heap_pos+0 = heap_pos, so offset from heap = 0)
     *   row_present_flags = 0x0001 (row 0 is present)
     *   transaction_row_flags = 0x0001
     */
    pu16(p1 + ROW_IDX_OFS_ROW0, 0);      /* ofs_row for row 0 = 0 from heap */
    pu16(p1 + ROW_IDX_PRESENT,  0x0001);
    pu16(p1 + ROW_IDX_TXN,      0x0001);

    *len_out = FILE_SIZE;
    return buf;
}

//SNIP_fake_pdb
