# Rekordbox PDB Parser — Implementation Plan

## Goal

A fully unit-tested C library (`src/nfs/pdb.h` / `src/nfs/pdb.c`) that parses Pioneer
rekordbox `export.pdb` files fetched from XDJ/CDJ decks via NFS, extracting track
metadata (id, title, filename, file\_path, BPM, duration, artist\_id, album\_id,
genre\_id, comment) with no external dependencies.

## Constraints

- Plain C99, no external libraries
- musl-gcc compatible (snip tests compile with musl-gcc)
- Reuse existing `src/nfs/utf16.h` for UTF-16LE decoding
- Snip tests compile standalone — no NFS stack needed for unit tests
- Integration tests for live decks use the full NFS stack

## Reference

- Format spec: `references/crate-digger/src/main/kaitai/rekordbox_pdb.ksy`
- Skill doc: `.claude/skills/rekordbox-db-format/SKILL.md`
- Existing NFS fetch: `src/bin/xdj_explore.c` `fetch_pdb()` function
- Synthetic PDB: `src/nfs/fake_pdb.c` / `fake_pdb.h`
- UTF-16 codec: `src/nfs/utf16.h` / `utf16.c`

---

## Phase 1 — Fetch real fixture from XDJ-700

**Goal**: Save a genuine `export.pdb` as `src/test/fixtures/export.pdb` for use in tests.

### Steps

1. Build `xdj-explore` if not already built:
   ```sh
   make target/bin/xdj-explore
   ```

2. Fetch export.pdb from Player 4 (XDJ-700 at 169.254.177.253):
   ```sh
   ./target/bin/xdj-explore 169.254.177.253 --fetch-pdb src/test/fixtures/export.pdb
   ```
   Expected file size: 1,638,400 bytes.

3. Optionally fetch exportExt.pdb (73,728 bytes):
   ```sh
   ./target/bin/xdj-explore 169.254.177.253 --fetch-pdb src/test/fixtures/exportExt.pdb
   ```

4. Verify:
   ```sh
   ls -la src/test/fixtures/
   ```

### Notes

- Deck must be powered on and connected via `enx001060ce107c`
- Fall back to Player 1 at 169.254.231.210 if Player 4 is unavailable
- `xdj-explore --fetch-pdb` already implemented in `fetch_pdb()` in `xdj_explore.c`
- The fixture is binary; add `src/test/fixtures/export.pdb` to `.gitignore` /
  `.sstignore` if the repo does not track binaries

### Files changed

- `src/test/fixtures/export.pdb` (new, binary)
- `src/test/fixtures/exportExt.pdb` (new, binary, optional)

```sh
geany-progress done 1 \
  -r src/test/fixtures/export.pdb \
  -w "Deck must be live on CDJ link network — NIC enx001060ce107c must be up"
```

---

## Phase 2 — Write `src/nfs/pdb.h` public API

**Goal**: Define the stable public interface for the PDB parser.

### API

```c
#ifndef PDB_H
#define PDB_H

#include <stdint.h>

/* Page type constants (table header type field). */
#define PDB_PAGE_TRACKS            0
#define PDB_PAGE_GENRES            1
#define PDB_PAGE_ARTISTS           2
#define PDB_PAGE_ALBUMS            3
#define PDB_PAGE_LABELS            4
#define PDB_PAGE_KEYS              5
#define PDB_PAGE_COLORS            6
#define PDB_PAGE_PLAYLIST_TREE     7
#define PDB_PAGE_PLAYLIST_ENTRIES  8
#define PDB_PAGE_HISTORY_PLAYLISTS 11
#define PDB_PAGE_HISTORY_ENTRIES   12
#define PDB_PAGE_ARTWORK           13

/* Track record populated from a tracks-page row. */
typedef struct pdb_track {
    uint32_t id;            /* matches CDJ_STATUS track_id field */
    uint32_t sample_rate;
    uint32_t file_size;
    uint32_t artwork_id;
    uint32_t key_id;
    uint32_t label_id;
    uint32_t remixer_id;
    uint32_t bitrate;
    uint32_t track_number;
    uint32_t bpm_x100;      /* BPM × 100, e.g. 12345 = 123.45 BPM */
    uint32_t genre_id;
    uint32_t album_id;
    uint32_t artist_id;
    uint16_t disc_number;
    uint16_t play_count;
    uint16_t year;
    uint16_t sample_depth;
    uint16_t duration_secs;
    uint8_t  color_id;
    uint8_t  rating;
    /* Heap-allocated UTF-8 strings; NULL if absent or empty. */
    char    *title;
    char    *filename;
    char    *file_path;
    char    *comment;
    char    *date_added;
    char    *release_date;
    char    *mix_name;
    char    *analyze_path;
} pdb_track_t;

/*
 * Callback invoked once per track row.
 * Return 0 to continue, non-zero to stop iteration early.
 * Strings inside *track are valid only for the duration of the call.
 */
typedef int (*pdb_track_cb)(const pdb_track_t *track, void *userdata);

/*
 * Parse a PDB file from memory.
 * Calls cb for each track row in the tracks table.
 * Returns 0 on success, -1 on fatal parse error.
 */
int pdb_parse(const uint8_t *data, uint32_t len,
              pdb_track_cb cb, void *userdata);

/*
 * Parse a PDB file by path.
 * Loads the file into memory then calls pdb_parse().
 * Returns 0 on success, -1 on error.
 */
int pdb_parse_file(const char *path, pdb_track_cb cb, void *userdata);

#endif /* PDB_H */
```

### SNIP strategy

`pdb.c` wraps all parsing logic in `//SNIP_pdb` markers so the snip test can pull
in the parser body without the file-I/O entry point. The `pdb_parse_file()` function
is outside the SNIP block (it uses `<stdio.h>` and the snip test loads fixtures itself).

### Files changed

- `src/nfs/pdb.h` (new)

```sh
geany-progress done 2 \
  -r src/nfs/pdb.h \
  -w "API is callback-based; strings are valid only inside the callback — callers must strdup if they need to keep them"
```

---

## Phase 3 — Write `src/nfs/pdb.c` parser

**Goal**: Full implementation of the PDB parser per the format spec.

### File structure

```c
#include <stdlib.h>
#include <string.h>
#include "utf16.h"
#include "pdb.h"

//SNIP_pdb

/* --- LE read helpers --- */
static uint16_t ru16(const uint8_t *p) { ... }
static uint32_t ru32(const uint8_t *p) { ... }

/* --- DeviceSQL string decoder --- */
/*
 * Decode a DeviceSQL string at ptr (within a page boundary of avail bytes).
 * Returns heap-allocated UTF-8 or NULL for empty/error.
 * Caller owns the returned string.
 */
static char *devicesql_to_utf8(const uint8_t *ptr, uint32_t avail) {
    if (avail < 1) return NULL;
    uint8_t kind = ptr[0];

    if (kind == 0x40) {
        /* Long ASCII: [0x40][len_u16_le][0x00][text] */
        if (avail < 4) return NULL;
        uint16_t total = ru16(ptr + 1);
        if (total < 4 || avail < total) return NULL;
        int tlen = total - 4;
        if (tlen <= 0) return NULL;
        char *s = malloc(tlen + 1);
        if (!s) return NULL;
        memcpy(s, ptr + 4, tlen);
        s[tlen] = '\0';
        return s;

    } else if (kind == 0x90) {
        /* Long UTF-16LE: [0x90][len_u16_le][0x00][utf16le bytes] */
        if (avail < 4) return NULL;
        uint16_t total = ru16(ptr + 1);
        if (total < 4 || avail < total) return NULL;
        int blen = total - 4;
        if (blen <= 0) return NULL;
        /* utf16le_to_utf8 needs a destination; max expansion 3× */
        char *s = malloc(blen * 3 + 1);
        if (!s) return NULL;
        if (utf16le_to_utf8(ptr + 4, blen, s, blen * 3 + 1) < 0) {
            free(s); return NULL;
        }
        return s;

    } else {
        /* Short ASCII: kind is odd; total size = kind >> 1 */
        int total = kind >> 1;
        if (total < 1) return NULL;
        int tlen = total - 1;
        if (tlen <= 0) return NULL;
        if ((uint32_t)total > avail) return NULL;
        char *s = malloc(tlen + 1);
        if (!s) return NULL;
        memcpy(s, ptr + 1, tlen);
        s[tlen] = '\0';
        return s;
    }
}

/* --- Track row parser --- */
/*
 * Track row fixed fields span 94 bytes, followed by 21 × u16 string offsets.
 * Total fixed part = 94 + 42 = 136 bytes.
 *
 * String slot indices (from rekordbox-db-format skill):
 *   [10] date_added   [11] release_date  [12] mix_name
 *   [14] analyze_path [16] comment       [17] title
 *   [19] filename     [20] file_path
 */
#define TR_FIXED      94
#define TR_N_STRINGS  21
#define TR_ROW_MIN    (TR_FIXED + TR_N_STRINGS * 2)  /* 136 bytes */

static int parse_track_row(const uint8_t *row_base, uint32_t row_avail,
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

    /* Decode string slots. */
    for (int i = 0; i < TR_N_STRINGS; i++) {
        uint16_t ofs = ru16(row_base + TR_FIXED + i * 2);
        if (ofs >= row_avail) continue;
        uint32_t avail = row_avail - ofs;
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

    free(t.title);  free(t.filename);  free(t.file_path);
    free(t.comment); free(t.date_added); free(t.release_date);
    free(t.mix_name); free(t.analyze_path);
    return rc;
}

/* --- Page iterator --- */
static int parse_tracks_page(const uint8_t *page, uint32_t len_page,
                              pdb_track_cb cb, void *ud)
{
    /* Heap starts at offset 40. */
    const uint32_t heap_off = 40;

    /* Unpack 3 bytes at offset 24: num_row_offsets (b13) | num_rows (b11) */
    uint32_t packed = page[24] | ((uint32_t)page[25] << 8) | ((uint32_t)page[26] << 16);
    uint16_t num_row_offsets = packed & 0x1FFF;
    /* num_rows = (packed >> 13) & 0x7FF; — not needed for iteration */

    if (num_row_offsets == 0) return 0;
    uint32_t num_row_groups = (num_row_offsets - 1) / 16 + 1;

    for (uint32_t g = 0; g < num_row_groups; g++) {
        uint32_t base = len_page - g * 0x24;
        if (base < 6) break;

        uint16_t present = ru16(page + base - 4);
        int max_i = (g == num_row_groups - 1)
                    ? (int)(num_row_offsets - g * 16)
                    : 16;

        for (int i = 0; i < max_i; i++) {
            if (!(present & (1 << i))) continue;
            if (base < (uint32_t)(6 + i * 2)) break;
            uint16_t ofs = ru16(page + base - 6 - i * 2);
            uint32_t row_start = heap_off + ofs;
            if (row_start >= len_page) continue;
            uint32_t row_avail = len_page - row_start;
            int rc = parse_track_row(page + row_start, row_avail, cb, ud);
            if (rc != 0) return rc;
        }
    }
    return 0;
}

/* --- Table page-chain walker --- */
static int walk_table(const uint8_t *data, uint32_t file_len, uint32_t len_page,
                      uint32_t first_page, pdb_track_cb cb, void *ud)
{
    /* Skip the first page (it's garbage per spec) — start from its next_page. */
    if ((uint64_t)first_page * len_page + 12 > file_len) return -1;
    uint32_t page_index = ru32(data + first_page * len_page + 12); /* next_page */

    while ((uint64_t)page_index * len_page + 40 <= file_len) {
        const uint8_t *page = data + page_index * len_page;

        uint32_t pg_type = ru32(page + 8);
        if (pg_type != PDB_PAGE_TRACKS) break;

        uint8_t page_flags = page[27];
        if (!(page_flags & 0x40)) {
            /* Data page — parse rows. */
            int rc = parse_tracks_page(page, len_page, cb, ud);
            if (rc != 0) return rc;
        }

        uint32_t next = ru32(page + 12);
        if (next == page_index) break;  /* safety: no infinite loop */
        page_index = next;
    }
    return 0;
}

/* --- Public entry points --- */

int pdb_parse(const uint8_t *data, uint32_t len, pdb_track_cb cb, void *ud)
{
    if (!data || len < 32) return -1;

    uint32_t len_page   = ru32(data + 4);
    uint32_t num_tables = ru32(data + 8);
    if (len_page < 64 || num_tables == 0) return -1;
    if ((uint64_t)28 + num_tables * 16 > len) return -1;

    for (uint32_t i = 0; i < num_tables; i++) {
        const uint8_t *tbl = data + 28 + i * 16;
        uint32_t type       = ru32(tbl + 0);
        uint32_t first_page = ru32(tbl + 8);
        if (type == PDB_PAGE_TRACKS) {
            return walk_table(data, len, len_page, first_page, cb, ud);
        }
    }
    return 0;  /* no tracks table found */
}

//SNIP_pdb

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
        free(buf); fclose(f); return -1;
    }
    fclose(f);
    int rc = pdb_parse(buf, (uint32_t)sz, cb, ud);
    free(buf);
    return rc;
}
```

> **Note on page\_flags bit**: The skill doc says `0x40 bit set = NOT a data page`.
> The condition in the code above inverts this correctly:
> `if (!(page_flags & 0x40))` → this is a data page, parse it.

### Files changed

- `src/nfs/pdb.c` (new)

```sh
geany-progress done 3 \
  -r src/nfs/pdb.c \
  -w "Row-index group iteration: verify group-boundary arithmetic against real fixture in Phase 4" \
  -w "devicesql_to_utf8: avail is page-relative — double-check offset arithmetic in parse_track_row" \
  -w "page_flags: 0x40 = NOT a data page (inverted from intuition)"
```

---

## Phase 4 — Snip unit tests

**Goal**: Snip tests that exercise the parser against both synthetic (fake\_pdb) and real
(fixture file) data. All tests must pass with musl-gcc.

### File: `src/test/pdb_parse_test.c.snip`

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

//SNIP_FILE SNIP_utf16_h   ../../nfs/utf16.h
//SNIP_FILE SNIP_utf16_c   ../../nfs/utf16.c
//SNIP_FILE SNIP_fake_pdb_h ../../nfs/fake_pdb.h
//SNIP_FILE SNIP_fake_pdb_c ../../nfs/fake_pdb.c
//SNIP_FILE SNIP_pdb_h     ../../nfs/pdb.h
//SNIP_FILE SNIP_pdb       ../../nfs/pdb.c

#include "snip_core.h"

/* --- Test state --- */
static int g_track_count;
static char g_title[256];
static char g_filename[256];
static char g_file_path[512];
static uint32_t g_bpm_x100;
static uint32_t g_duration_secs;
static uint32_t g_track_id;

static int collect_cb(const pdb_track_t *t, void *ud) {
    (void)ud;
    g_track_count++;
    snprintf(g_title,     sizeof(g_title),     "%s", t->title     ? t->title     : "");
    snprintf(g_filename,  sizeof(g_filename),  "%s", t->filename  ? t->filename  : "");
    snprintf(g_file_path, sizeof(g_file_path), "%s", t->file_path ? t->file_path : "");
    g_bpm_x100      = t->bpm_x100;
    g_duration_secs = t->duration_secs;
    g_track_id      = t->id;
    return 0;
}

/* Test 1: Parse fake_pdb — verify basic fields */
static void test_fake_pdb_basic(void) {
    fake_track_t ft = {
        .title          = "Test Track",
        .filename       = "test.mp3",
        .file_path      = "/C/Music/test.mp3",
        .bpm_x100       = 12345,
        .duration_secs  = 180,
    };
    uint32_t len;
    uint8_t *buf = fake_pdb_generate(&ft, &len);
    g_track_count = 0;
    int rc = pdb_parse(buf, len, collect_cb, NULL);
    snip_equals("fake: pdb_parse ok", 0, rc);
    snip_equals("fake: one track", 1, g_track_count);
    snip_assert("fake: title", strcmp(g_title, "Test Track") == 0);
    snip_assert("fake: filename", strcmp(g_filename, "test.mp3") == 0);
    snip_assert("fake: file_path", strcmp(g_file_path, "/C/Music/test.mp3") == 0);
    snip_equals("fake: bpm_x100", 12345, (int)g_bpm_x100);
    snip_equals("fake: duration", 180, (int)g_duration_secs);
    free(buf);
}

/* Test 2: Parse fake_pdb — track id */
static void test_fake_pdb_id(void) {
    fake_track_t ft = {
        .title = "ID Test", .filename = "id.mp3", .file_path = "/C/id.mp3",
        .bpm_x100 = 10000, .duration_secs = 60,
    };
    uint32_t len;
    uint8_t *buf = fake_pdb_generate(&ft, &len);
    g_track_count = 0;
    pdb_parse(buf, len, collect_cb, NULL);
    snip_equals("fake: track_id == 1", 1, (int)g_track_id);
    free(buf);
}

/* Test 3: NULL / zero-length buffer returns error */
static void test_bad_input(void) {
    uint8_t dummy[4] = {0};
    snip_equals("null data returns -1", -1, pdb_parse(NULL, 0, collect_cb, NULL));
    snip_equals("short data returns -1", -1, pdb_parse(dummy, 4, collect_cb, NULL));
}

/* Test 4: Real fixture — count tracks and spot-check */
static void test_real_fixture(void) {
    FILE *f = fopen("fixtures/export.pdb", "rb");
    if (!f) {
        printf("  [skip] fixtures/export.pdb not found — run Phase 1 first\n");
        return;
    }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    rewind(f);
    uint8_t *buf = malloc((size_t)sz);
    fread(buf, 1, (size_t)sz, f);
    fclose(f);

    g_track_count = 0;
    int rc = pdb_parse(buf, (uint32_t)sz, collect_cb, NULL);
    snip_equals("real: pdb_parse ok", 0, rc);
    snip_assert("real: at least one track", g_track_count > 0);
    printf("  real fixture: %d tracks found\n", g_track_count);
    printf("  last title:     %s\n", g_title);
    printf("  last file_path: %s\n", g_file_path);
    printf("  last bpm_x100:  %u  duration: %u s\n", g_bpm_x100, g_duration_secs);
    free(buf);
}

/* Test 5: BPM boundary — zero BPM */
static void test_bpm_zero(void) {
    fake_track_t ft = {
        .title = "Silent", .filename = "s.mp3", .file_path = "/C/s.mp3",
        .bpm_x100 = 0, .duration_secs = 1,
    };
    uint32_t len;
    uint8_t *buf = fake_pdb_generate(&ft, &len);
    g_track_count = 0;
    pdb_parse(buf, len, collect_cb, NULL);
    snip_equals("bpm zero: track found", 1, g_track_count);
    snip_equals("bpm zero: bpm_x100 == 0", 0, (int)g_bpm_x100);
    free(buf);
}

int main(int argc, const char *argv[]) {
    test_fake_pdb_basic();
    test_fake_pdb_id();
    test_bad_input();
    test_real_fixture();
    test_bpm_zero();
    print_results(argv[0]);
    return 0;
}
```

### File: `src/test/pdb_parse_test.c.make`

```bash
#!/bin/bash
set -euo pipefail

cd $(dirname $0)

snip_test=pdb_parse_test

musl-gcc -Wall -Werror -Wno-unused-function -g -O0 \
    $snip_test.c \
    -o $snip_test \
    -lm \
    && ./$snip_test \
    && rm -f ./$snip_test ./${snip_test}.c
```

`chmod +x src/test/pdb_parse_test.c.make`

### SNIP markers in pdb.c

The `//SNIP_pdb` block must enclose everything up to but not including
`pdb_parse_file()`. The snip test includes `pdb.c` directly and provides its own
`FILE *`-based fixture loading in Test 4. Verify that `pdb_parse_file()` is **outside**
the SNIP block (the snip preprocessor only pulls content between matching markers).

### snip\_core.h note

`snip_core.h` provides `snip_assert()`, `snip_equals()`, and `print_results()`.
The `print_results()` call at the end exits with non-zero when `errors > 0`, making
`make` fail on test failures.

### Files changed

- `src/test/pdb_parse_test.c.snip` (new)
- `src/test/pdb_parse_test.c.make` (new, chmod +x)

```sh
geany-progress done 4 \
  -r src/test/pdb_parse_test.c.snip \
  -r src/test/pdb_parse_test.c.make \
  -w "Test 4 silently skips if fixtures/export.pdb absent — run Phase 1 to populate it" \
  -w "sniprun must be on PATH; run from src/test/ or adjust paths in .make file"
```

---

## Phase 5 — Integration binary for live XDJ decks

**Goal**: `xdj-pdb` binary that fetches and parses the PDB from a live deck, acting as
both a debug tool and an end-to-end integration test.

### New file: `src/bin/xdj_pdb.c`

```
Usage: xdj-pdb <ip> [--count] [--dump-paths] [--dump-ids]
```

Behaviour:
1. Portmapper → mountd port, NFS port (same pattern as `xdj_tracks.c`).
2. Mount `/C/` export (or first export returned by EXPORT RPC).
3. `nfs_path_lookup` → `PIONEER/rekordbox/export.pdb`.
4. `nfs_getattr` → verify file size.
5. `nfs_read` loop in 2048-byte chunks into a `malloc`'d buffer.
6. `pdb_parse(buf, len, cb, NULL)` — callback prints each track.
7. Print summary line: `N tracks, total Xs`.

Output format (default — TSV, one track per line):
```
<id>\t<bpm_x100>\t<duration_secs>\t<file_path>
```

`--dump-paths`: one `/C/Music/track.mp3` per line (grep-friendly).

`--count`: print only the integer track count (scriptable).

`--dump-ids`: print `<id> <title>` pairs — useful for correlating with CDJ\_STATUS.

### Live verification test (optional, behind `--verify-live`)

1. Join ProLink as virtual CDJ (reuse `vdj_init()` / `vdj_on_status()` from `vdj.h`).
2. Parse PDB into an in-memory `id → file_path` hash table (flat array is fine for < 10k
   tracks; linear scan is acceptable).
3. For each received `CDJ_STATUS` packet with `track_id != 0`, look up `track_id` in
   the table and print match/no-match.

This validates that `pdb_track_t.id` matches CDJ\_STATUS `track_id` as expected.

### Makefile additions

Add after the existing NFS rules:

```makefile
PDB_OBJS = $(NFS_OBJS) target/nfs/pdb.o

target/nfs/pdb.o: src/nfs/pdb.c src/nfs/pdb.h src/nfs/utf16.h | target/nfs
	$(CC) $(CFLAGS) -Isrc/nfs -c src/nfs/pdb.c -o $@

target/bin/xdj_pdb.o: src/bin/xdj_pdb.c src/nfs/pdb.h | target/bin
	$(CC) $(CFLAGS) -Isrc/nfs -c src/bin/xdj_pdb.c -o $@

target/xdj-pdb: $(PDB_OBJS) target/bin/xdj_pdb.o
	$(CC) $(LDFLAGS) -o $@ $^

xdj-pdb: target/xdj-pdb
```

Also add `target/nfs/pdb.o` to the `NFS_OBJS` list if pdb should be available to other
targets (e.g. `vdj-nfs`).

### Manual integration test steps

With Player 4 live at 169.254.177.253:

```sh
make xdj-pdb
./target/xdj-pdb 169.254.177.253 --count
./target/xdj-pdb 169.254.177.253 --dump-paths | head -20
./target/xdj-pdb 169.254.177.253
```

Expected: same track count as reported by `xdj-explore`, paths matching those visible
in rekordbox export.

### Files changed

- `src/bin/xdj_pdb.c` (new)
- `Makefile` (add `pdb.o`, `xdj_pdb.o`, `xdj-pdb` target)

```sh
geany-progress done 5 \
  -r src/bin/xdj_pdb.c \
  -r Makefile \
  -w "Live test requires deck on CDJ link network; --verify-live additionally requires vdj-nfs running simultaneously"
```

---

## Phase 6 — Wire into Makefile test target and final verification

**Goal**: `make test` (or equivalent) runs snip tests including pdb\_parse\_test; all
tests green.

### Steps

1. Find the existing snip test runner invocation in Makefile. Current examples:
   `bpm_madness_test.c.make`, `libcdj_pkts_test.c.make`, `time_diff_test.c.make`.

2. Add `pdb_parse_test.c.make` to the same test batch. Pattern:
   ```makefile
   test-snip: ...
       cd src/test && ./pdb_parse_test.c.make
   ```
   Or add it to whatever target (`check`, `test`) already runs the others.

3. Add `src/test/fixtures/export.pdb` to `.sstignore` (already present in repo root)
   if not already listed — binary fixtures should not be committed.

### Verification checklist

- [ ] `make nfs` compiles `pdb.o` without warnings (`-Wall -Werror`)
- [ ] `make xdj-pdb` links cleanly
- [ ] `src/test/pdb_parse_test.c.make` runs green:
  - Test 1–3, 5: always pass (synthetic data)
  - Test 4: passes when `fixtures/export.pdb` is present (Phase 1)
- [ ] `./target/xdj-pdb 169.254.177.253 --count` prints a positive integer
- [ ] `./target/xdj-pdb 169.254.177.253 --dump-paths` lists `.mp3` / `.wav` paths
- [ ] Track IDs from `--dump-ids` match `track_id` in CDJ\_STATUS packets observed
  while the deck is playing those tracks

### Files changed

- `Makefile` (add `pdb_parse_test.c.make` to test target)
- `.sstignore` (add `src/test/fixtures/export.pdb` if needed)

```sh
geany-progress done 6 \
  -r Makefile \
  -r src/test/pdb_parse_test.c.snip \
  -w "Test 4 (real fixture) only passes after Phase 1 — document this in README if needed"
```

---

## Summary

| Phase | Deliverable | Key risk |
|-------|-------------|----------|
| 1 | `src/test/fixtures/export.pdb` | Deck must be live on CDJ link |
| 2 | `src/nfs/pdb.h` (API) | Callback ownership semantics |
| 3 | `src/nfs/pdb.c` (parser) | Row-group index arithmetic; devicesql bounds |
| 4 | `pdb_parse_test.c.snip` + `.make` | SNIP\_FILE paths; fixture not present |
| 5 | `src/bin/xdj_pdb.c` + Makefile | Full NFS link; live deck needed |
| 6 | Makefile test wiring | sniprun on PATH |
