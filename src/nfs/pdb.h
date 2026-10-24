#ifndef PDB_H
#define PDB_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Page type constants (table header type field). */
#define PDB_PAGE_TRACKS             0
#define PDB_PAGE_GENRES             1
#define PDB_PAGE_ARTISTS            2
#define PDB_PAGE_ALBUMS             3
#define PDB_PAGE_LABELS             4
#define PDB_PAGE_KEYS               5
#define PDB_PAGE_COLORS             6
#define PDB_PAGE_PLAYLIST_TREE      7
#define PDB_PAGE_PLAYLIST_ENTRIES   8
#define PDB_PAGE_HISTORY_PLAYLISTS  11
#define PDB_PAGE_HISTORY_ENTRIES    12
#define PDB_PAGE_ARTWORK            13

/*
 * Track record populated from a tracks-page row.
 * All heap-allocated strings are valid only during the pdb_track_cb call.
 * NULL means the field was absent or empty in the database.
 */
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
    uint32_t bpm_x100;      /* BPM * 100, e.g. 12345 = 123.45 BPM */
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
 * Callback invoked once per track row during pdb_parse / pdb_parse_file.
 * Return 0 to continue iteration, non-zero to stop early.
 * String pointers inside *track are freed immediately after the callback returns.
 */
typedef int (*pdb_track_cb)(const pdb_track_t *track, void *userdata);

/*
 * Parse a rekordbox export.pdb from a memory buffer.
 * Calls cb once per track row in the tracks table.
 * Returns 0 on success, -1 on fatal parse error.
 */
int pdb_parse(const uint8_t *data, uint32_t len,
              pdb_track_cb cb, void *userdata);

/*
 * Parse a rekordbox export.pdb by file path.
 * Loads the file into memory then delegates to pdb_parse().
 * Returns 0 on success, -1 on I/O or parse error.
 */
int pdb_parse_file(const char *path, pdb_track_cb cb, void *userdata);

/*
 * Load a whole file into memory for the pdb_parse_* functions.
 * Caller free()s the result. Returns NULL on I/O error.
 */
uint8_t *pdb_read_file(const char *path, uint32_t *len_out);

/*
 * Generic access: call cb for every present row of the given table type
 * (PDB_PAGE_*). row points at the start of the row, avail is the number of
 * bytes from there to the end of its page. Return non-zero from cb to stop.
 * A table that is absent from the file is not an error.
 */
typedef int (*pdb_row_cb)(const uint8_t *row, uint32_t avail, void *userdata);
int pdb_foreach_row(const uint8_t *data, uint32_t len, uint32_t table_type,
                    pdb_row_cb cb, void *userdata);

/*
 * Typed table readers. Strings in the structs are valid only during the callback.
 * All return 0 on success, -1 on fatal parse error (or the non-zero value a callback returned).
 */

/* genres, artists, labels, keys, colors, artwork (name = path for artwork).
 * id is the table row id (for colors, the color id used by tracks). */
typedef struct pdb_named {
    uint32_t id;
    char    *name;
} pdb_named_t;
typedef int (*pdb_named_cb)(const pdb_named_t *row, void *userdata);

typedef struct pdb_album {
    uint32_t id;
    uint32_t artist_id;
    char    *name;
} pdb_album_t;
typedef int (*pdb_album_cb)(const pdb_album_t *row, void *userdata);

/* A node of the playlist tree: a folder (is_folder) or a playlist. parent_id 0 = root. */
typedef struct pdb_playlist {
    uint32_t id;
    uint32_t parent_id;
    uint32_t sort_order;
    int      is_folder;
    char    *name;
} pdb_playlist_t;
typedef int (*pdb_playlist_cb)(const pdb_playlist_t *row, void *userdata);

/* One track in a playlist; order by entry_index. */
typedef struct pdb_playlist_entry {
    uint32_t entry_index;
    uint32_t track_id;
    uint32_t playlist_id;
} pdb_playlist_entry_t;
typedef int (*pdb_playlist_entry_cb)(const pdb_playlist_entry_t *row, void *userdata);

int pdb_parse_genres  (const uint8_t *data, uint32_t len, pdb_named_cb cb, void *userdata);
int pdb_parse_artists (const uint8_t *data, uint32_t len, pdb_named_cb cb, void *userdata);
int pdb_parse_albums  (const uint8_t *data, uint32_t len, pdb_album_cb cb, void *userdata);
int pdb_parse_labels  (const uint8_t *data, uint32_t len, pdb_named_cb cb, void *userdata);
int pdb_parse_keys    (const uint8_t *data, uint32_t len, pdb_named_cb cb, void *userdata);
int pdb_parse_colors  (const uint8_t *data, uint32_t len, pdb_named_cb cb, void *userdata);
int pdb_parse_artwork (const uint8_t *data, uint32_t len, pdb_named_cb cb, void *userdata);
int pdb_parse_playlists(const uint8_t *data, uint32_t len, pdb_playlist_cb cb, void *userdata);
int pdb_parse_playlist_entries(const uint8_t *data, uint32_t len,
                               pdb_playlist_entry_cb cb, void *userdata);

#ifdef __cplusplus
}
#endif

#endif /* PDB_H */
