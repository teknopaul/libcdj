---
name: rekordbox-db-format
description: Reference knowledge for parsing Pioneer rekordbox export.pdb files in C, derived from crate-digger kaitai spec.
---

<objective>
Parse rekordbox export.pdb files fetched from Pioneer CDJ/XDJ players via NFS to extract track metadata (filename, file_path, title, BPM, duration, etc.).
</objective>

<process>

## How to get the file

Fetch via NFS from a connected deck using the existing NFS client stack:
- portmap → mountd port, nfs port
- mount `/C/` export
- `nfs_path_lookup` to `PIONEER/rekordbox/export.pdb`
- read in chunks with `nfs_read` (2048-byte chunks like xdj_explore does)

The deck at 169.254.177.253 (player 4, XDJ-700) has a 1,638,400 byte export.pdb and a 73,728 byte exportExt.pdb. Both are accessible. The NFS client does NOT allow walking the audio file directories directly (LOOKUP on C/P/S/K returns NFSERR_STALE=70), so the PDB is the only way to get track filenames.

Note: READDIR names from the XDJ-700 are truncated to single UTF-16LE characters — useless for directory walking. LOOKUP by known exact name (e.g. "PIONEER/rekordbox/export.pdb") works fine.

## File header (little-endian throughout)

```
Offset  Size  Field
0       4     unknown (always 0)
4       4     len_page   ← ALL page offsets = page_index * len_page
8       4     num_tables
12      4     next_unused_page
16      4     unknown
20      4     sequence
24      4     gap (zeros)
28      16*num_tables   table headers
```

## Table header (16 bytes each)

```
0   4   type   (enum below)
4   4   empty_candidate
8   4   first_page  index
12  4   last_page   index
```

Page type enum:
- 0 = tracks
- 1 = genres
- 2 = artists
- 3 = albums
- 4 = labels
- 5 = keys
- 6 = colors
- 7 = playlist_tree
- 8 = playlist_entries
- 11 = history_playlists
- 12 = history_entries
- 13 = artwork

## Page structure (heap, little-endian)

Page offset in file = `page_index * len_page`.

Header = 40 bytes:
```
0   4   gap (zeros)
4   4   page_index
8   4   type
12  4   next_page index
16  4   sequence
20  4   padding
24  3   packed bits (LE): num_row_offsets (b13) | num_rows (b11)
27  1   page_flags  (0x40 bit set = NOT a data page, skip it)
28  2   free_size
30  2   used_size
32  2   transaction_row_count
34  2   transaction_row_index
36  2   unknown
38  2   unknown
--- heap_pos = 40 ---
```

Unpack the 3 bytes at offset 24:
```c
uint32_t packed = b[24] | (b[25] << 8) | (b[26] << 16);
uint16_t num_row_offsets = packed & 0x1FFF;
uint16_t num_rows        = (packed >> 13) & 0x7FF;
```

Skip non-data pages: `if (page_flags & 0x40) continue;`

## Row index (built backwards from end of page)

```c
uint32_t num_row_groups = (num_row_offsets - 1) / 16 + 1;
```

For group `g` (g=0 is the last/innermost group):
```
base = len_page - (g * 0x24)   /* offset from start of page */

[base + 0]       u2  transaction_row_flags  (skip)
[base - 4]       u2  row_present_flags      bit i set = row i present
[base - 6]       u2  ofs_row[0]             offset from heap_pos to row 0
[base - 8]       u2  ofs_row[1]
[base - 6 - 2*i] u2  ofs_row[i]
```

Iteration pattern:
```c
for (int g = 0; g < num_row_groups; g++) {
    uint32_t base = len_page - (g * 0x24);
    uint16_t present = read_u16_le(page + base - 4);
    for (int i = 0; i < 16; i++) {
        if (!(present & (1 << i))) continue;
        uint16_t ofs = read_u16_le(page + base - 6 - 2*i);
        uint8_t *row_base = page + 40 + ofs;
        /* parse row */
    }
}
```

The first page of any table chain is garbage — start iteration from `first_page`'s `next_page`.
Stop when `next_page` index * len_page >= file size, or next page has a different type.

## Track row layout (page_type 0)

Fixed fields = 94 bytes, then 21 × u2 string offsets = 136 bytes total before heap strings.

```
Offset  Size  Field
0       2     subtype     (always 0x24 for tracks; 0x04 bit = use u16 offsets, always set)
2       2     index_shift
4       4     bitmask
8       4     sample_rate
12      4     composer_id
16      4     file_size
20      4     unknown
24      2     unknown
26      2     unknown
28      4     artwork_id
32      4     key_id
36      4     original_artist_id
40      4     label_id
44      4     remixer_id
48      4     bitrate
52      4     track_number
56      4     tempo          ← BPM * 100
60      4     genre_id
64      4     album_id
68      4     artist_id
72      4     id             ← matches player CDJ_STATUS packet track_id
76      2     disc_number
78      2     play_count
80      2     year
82      2     sample_depth
84      2     duration       ← seconds
86      2     unknown
88      1     color_id
89      1     rating
90      2     unknown
92      2     unknown
94      2×21  ofs_strings[0..20]   ← offsets relative to row_base
```

String slot indices:
```
[0]   isrc
[1]   texter
[2]   unknown_string_2
[3]   unknown_string_3
[4]   unknown_string_4
[5]   message
[6]   kuvo_public
[7]   autoload_hot_cues
[8]   unknown_string_5
[9]   unknown_string_6
[10]  date_added
[11]  release_date
[12]  mix_name
[13]  unknown_string_7
[14]  analyze_path
[15]  analyze_date
[16]  comment
[17]  title            ← track title
[18]  unknown_string_8
[19]  filename         ← bare filename, e.g. "track.mp3"
[20]  file_path        ← full USB path, e.g. "/C/Music/track.mp3"
```

## device_sql_string decoding

All string fields use this variable-length encoding. Read from `row_base + ofs_strings[i]`:

```c
uint8_t kind = *ptr;
if (kind == 0x40) {
    /* long ASCII: 1(kind) + 2(len) + 1(pad) + text */
    uint16_t len = read_u16_le(ptr + 1);  /* includes 4-byte header */
    char *text = (char *)(ptr + 4);
    int text_len = len - 4;               /* bytes of ASCII */
} else if (kind == 0x90) {
    /* long UTF-16LE: 1(kind) + 2(len) + 1(pad) + text */
    uint16_t len = read_u16_le(ptr + 1);  /* includes 4-byte header */
    uint8_t *text = ptr + 4;
    int text_len = len - 4;               /* bytes of UTF-16LE, use utf16.h to decode */
} else {
    /* short ASCII: kind is odd, length = kind >> 1 (total type size) */
    int slen = kind >> 1;                 /* total length of this type including kind byte */
    char *text = (char *)(ptr + 1);
    int text_len = slen - 1;             /* bytes of ASCII */
}
```

UTF-16LE strings use the existing `utf16le_to_utf8()` from `src/nfs/utf16.h`.

## Minimal parser pseudocode (track filenames only)

```c
// 1. Read header
len_page   = u32 at offset 4
num_tables = u32 at offset 8

// 2. Find tracks table (type == 0)
for each table at offset 28 + i*16:
    if table.type == 0:
        first_page = table.first_page_index

// 3. Walk page chain, skip first page (garbage)
page_index = read_next_page(first_page)   /* skip first */
while page_index * len_page < file_size:
    page = file + page_index * len_page
    if page.type != 0: break
    if !(page.page_flags & 0x40):          /* data page */
        for each present row in page:
            ofs_path = read_u16_le(row_base + 94 + 20*2)  /* ofs_strings[20] */
            decode device_sql_string at row_base + ofs_path
            print file_path
    page_index = page.next_page

```

## Reference files

- Format spec: `references/crate-digger/src/main/kaitai/rekordbox_pdb.ksy`
- Java parser: `references/crate-digger/src/main/java/org/deepsymmetry/cratedigger/Database.java`
- UTF-16 codec: `src/nfs/utf16.h` / `src/nfs/utf16.c`
- NFS fetch: `src/bin/xdj_explore.c` fetch_pdb() shows the read loop

## Network context

- Player 1 (XDJ-700): 169.254.231.210
- Player 4 (XDJ-700): 169.254.177.253
- NIC for CDJ link: enx001060ce107c (USB ethernet)
- cdj-scan needs `-i enx001060ce107c` due to multiple NICs on host

</process>
