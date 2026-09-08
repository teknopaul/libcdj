#ifndef PDB_H
#define PDB_H

#include <stdint.h>

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

#endif /* PDB_H */
