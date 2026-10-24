#ifndef CDJ_ANLZ_H
#define CDJ_ANLZ_H

/*
 * rekordbox ANLZ analysis files (ANLZ0000.DAT / .EXT / .2EX).
 * Format: doc-internal/rekordbox-anlz-format.text. All integers in the files are big-endian.
 *
 * Ownership: an anlz_t either owns the file bytes (anlz_open_file) or borrows them
 * (anlz_open_mem: the buffer must outlive the anlz_t). Every pointer handed out stays valid
 * until anlz_close().
 *
 * Robustness: every read is bounds-checked. A damaged file keeps the tags that precede the
 * damage and is flagged with anlz_is_damaged().
 */

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    ANLZ_OK = 0,
    ANLZ_ERR_IO,        /* cannot read the file */
    ANLZ_ERR_MAGIC,     /* not a PMAI file (e.g. USBMNG.DAT) */
    ANLZ_ERR_TRUNC,     /* data ends inside a header or tag */
    ANLZ_ERR_BAD_LEN,   /* a length field is impossible */
    ANLZ_ERR_NOMEM,
    ANLZ_ERR_ABSENT     /* the requested tag is not in this file */
} anlz_err_t;

typedef struct anlz anlz_t;

/* One tag of the file, whether or not libcdj decodes it. */
typedef struct {
    char           fourcc[5];   /* NUL-terminated, e.g. "PQTZ" */
    uint32_t       offset;      /* from the start of the file */
    uint32_t       len_header;  /* as stored */
    uint32_t       len_tag;     /* whole tag including its header */
    const uint8_t *data;        /* start of the tag (its fourcc), len_tag bytes are readable */
} anlz_tag_t;

const char *anlz_strerror(anlz_err_t err);

/* Borrows data (not copied). On error *out is NULL. */
anlz_err_t anlz_open_mem(const uint8_t *data, uint32_t len, anlz_t **out);
/* Reads the file (max 64 MB) and owns the bytes. */
anlz_err_t anlz_open_file(const char *path, anlz_t **out);
void       anlz_close(anlz_t *a);

uint32_t          anlz_tag_count(const anlz_t *a);
const anlz_tag_t *anlz_tag_at(const anlz_t *a, uint32_t index);
/* nth (0-based) tag with this fourcc, e.g. the second PCOB; NULL if there is none. */
const anlz_tag_t *anlz_find_tag(const anlz_t *a, const char fourcc[4], uint32_t nth);

/* 1 when the tag walk hit a bad length or truncated tag before the end of the file. */
int anlz_is_damaged(const anlz_t *a);
/* 1 when the PMAI len_file field equals the real size. A mismatch is tolerated. */
int anlz_file_len_ok(const anlz_t *a);

/* ---- track path (PPTH) ---- */

/* The audio file path stored in the analysis, e.g. "/Contents/Artist/Album/file.mp3" (UTF-8,
 * owned by the anlz_t). ANLZ_ERR_ABSENT when there is no PPTH, ANLZ_ERR_BAD_LEN when it is malformed. */
anlz_err_t anlz_path(const anlz_t *a, const char **utf8);

/* ---- beat grid ---- */

typedef enum {
    ANLZ_GRID_PQTZ = 1,     /* .DAT: one 8-byte entry per beat, millisecond times */
    ANLZ_GRID_PQT2 = 2      /* .EXT: first/last anchors + microsecond fractions; times are reconstructed (+-1 ms) */
} anlz_grid_source_t;

typedef struct {
    uint16_t beat_number;   /* 1..4 within the bar */
    uint16_t bpm_x100;      /* tempo, BPM x 100 */
    uint32_t time_ms;       /* at normal speed */
    uint16_t sub_ms_us;     /* 0..999: microseconds after time_ms; 0 when no PQT2 data is available */
} anlz_beat_t;

typedef struct {
    anlz_grid_source_t source;
    uint32_t count;         /* number of beats; 0 for a track rekordbox has not analysed */
    int      monotonic;     /* 1 when beat times never go backwards */
    int      clamped;       /* 1 when the tag claimed more beats than it holds */
    int      has_sub_ms;    /* 1 when microsecond fractions are available (PQT2) */
    /* internals: use anlz_beat_at() */
    const uint8_t *pqtz;    /* PQTZ entries */
    const uint8_t *sub;     /* PQT2 fractions, 2 bytes per beat */
    uint32_t header_count;  /* PQT2 only: beat count stored in the header */
    uint16_t first_number, tempo;
    uint32_t first_ms, last_ms;
} anlz_beatgrid_t;

/* PQTZ when the file has one (even an empty one), otherwise PQT2, otherwise ANLZ_ERR_ABSENT. */
anlz_err_t anlz_beatgrid(const anlz_t *a, anlz_beatgrid_t *out);
/* Add microsecond precision from the .EXT's PQT2 to a grid read from the .DAT. The beat counts must agree
 * (ANLZ_ERR_BAD_LEN otherwise); ANLZ_ERR_ABSENT when ext has no PQT2. */
anlz_err_t anlz_beatgrid_attach_sub_ms(anlz_beatgrid_t *g, const anlz_t *ext);
/* 1 and *out filled, or 0 when index >= count. */
int anlz_beat_at(const anlz_beatgrid_t *g, uint32_t index, anlz_beat_t *out);
/* Index of the last beat at or before time_ms, -1 when time_ms is before the first beat or the grid is empty. */
int32_t anlz_beat_for_time(const anlz_beatgrid_t *g, uint32_t time_ms);
/* Average tempo in BPM over the whole grid; 0 when there are fewer than 2 beats. */
double anlz_avg_bpm(const anlz_beatgrid_t *g);

/* ---- cue points, hot cues and loops (PCOB/PCPT in .DAT, PCO2/PCP2 in .EXT) ---- */

typedef struct {
    uint8_t  hot_cue;         /* 0 = memory point, 1..8 = hot cue A..H */
    uint8_t  is_loop;
    uint8_t  from_pco2;       /* 1 when read from PCO2/PCP2 (the full record), 0 for PCOB/PCPT */
    uint32_t time_ms;         /* position */
    uint32_t loop_time_ms;    /* end of the loop; 0 for a plain cue */
    uint32_t status;          /* PCPT status word (1 in every sample); 0 for PCP2 */
    uint32_t marker;          /* 3 bytes (PCPT 0x1d, PCP2 0x11): 1000 on most cues, 150 on the first cue (A) of every track of the
                                 second sample export; meaning unknown */
    uint16_t loop_num, loop_den;   /* PCP2 only: loop length in beats = num/den (16/1 = 16 beats); 0 if absent */
    uint8_t  color_id;        /* PCP2 byte 0x1c */
    uint8_t  raw_1d[3];       /* PCP2 bytes 0x1d..0x1f; "03 00 xx" on cues that carry a colour; meaning unknown */
    uint8_t  has_rgb;         /* the 4 colour bytes after the comment are present */
    uint8_t  color_code, red, green, blue;
    char     comment[256];    /* UTF-8, "" when none (at most 85 UTF-16 units are kept) */
} anlz_cue_t;

typedef struct {
    uint32_t     count;
    anlz_cue_t  *cues;        /* malloc'd; release with anlz_cue_list_free() */
    int          damaged;     /* an entry was malformed; the entries before it are returned */
} anlz_cue_list_t;

typedef enum { ANLZ_CUES_MEMORY = 0, ANLZ_CUES_HOT = 1, ANLZ_CUES_ALL = 2 } anlz_cue_kind_t;

/* Cues of one file. A file with PCO2 tags is read from those, otherwise from its PCOB tags (a .DAT only
 * lists the first three hot cues; the .EXT PCO2 has all of them). Memory points come sorted by time,
 * hot cues by hot_cue number; ALL gives memory points first. ANLZ_ERR_ABSENT when the file has no cue
 * tags at all; an empty list is ANLZ_OK with count 0. */
anlz_err_t anlz_cues(const anlz_t *a, anlz_cue_kind_t kind, anlz_cue_list_t *out);
/* Same, reading a chosen source instead of "PCO2 if present": use_pco2 = 1 reads only PCO2 tags, 0 only PCOB tags. */
anlz_err_t anlz_cues_from(const anlz_t *a, anlz_cue_kind_t kind, int use_pco2, anlz_cue_list_t *out);
void       anlz_cue_list_free(anlz_cue_list_t *list);

/* ---- waveforms (PWAV, PWV2 in .DAT; PWV3, PWV4, PWV5 in .EXT; PWV6, PWV7 in .2EX) ---- */

typedef enum {
    ANLZ_WAVE_PREVIEW = 1,        /* PWAV  .DAT  400 columns, 1 byte: height 5 bits + whiteness 3 bits */
    ANLZ_WAVE_TINY,               /* PWV2  .DAT  100 columns, 1 byte: height 4 bits */
    ANLZ_WAVE_DETAIL,             /* PWV3  .EXT  150 entries/s, encoded like PWAV */
    ANLZ_WAVE_COLOR_PREVIEW,      /* PWV4  .EXT  1200 columns, 6 bytes (layout only partly understood: raw) */
    ANLZ_WAVE_COLOR_DETAIL,       /* PWV5  .EXT  150 entries/s, 16 bit: red 3, green 3, blue 3, height 5 */
    ANLZ_WAVE_3BAND_PREVIEW,      /* PWV6  .2EX  1200 columns, 3 bytes (mid, high, low); spec only, no sample */
    ANLZ_WAVE_3BAND_DETAIL        /* PWV7  .2EX  150 entries/s, 3 bytes (mid, high, low); spec only, no sample */
} anlz_wave_kind_t;

typedef struct {
    anlz_wave_kind_t kind;
    uint32_t         count;           /* entries (columns) */
    uint32_t         entry_bytes;
    const uint8_t   *data;            /* count * entry_bytes bytes, inside the file buffer */
    double           entries_per_sec; /* detail waveforms: 150; 0 for previews (they span the whole track) */
    int              clamped;         /* the tag claimed more entries than it holds */
} anlz_waveform_t;

typedef struct {
    uint8_t height;                   /* 0..31 (PWV2: 0..15); 3-band: the largest of low/mid/high */
    uint8_t whiteness;                /* PWAV/PWV3: 0..7 */
    uint8_t red, green, blue;         /* PWV5, scaled to 0..255 */
    uint8_t low, mid, high;           /* PWV6/PWV7 */
    uint8_t raw[6];                   /* the entry bytes as stored (PWV4: all there is) */
} anlz_wave_column_t;

anlz_err_t anlz_waveform(const anlz_t *a, anlz_wave_kind_t kind, anlz_waveform_t *out);
/* 1 and *out filled, 0 when index >= count. */
int anlz_wave_column(const anlz_waveform_t *w, uint32_t index, anlz_wave_column_t *out);
/* Entry for a position: detail waveforms use time * 150/s and ignore track_ms; previews scale over
 * track_ms (0 gives -1). Result is clamped to the last entry; -1 for an empty waveform. */
int32_t anlz_wave_index_for_time(const anlz_waveform_t *w, uint32_t time_ms, uint32_t track_ms);

/* ---- whole track: DAT + EXT + 2EX together ---- */

typedef enum { ANLZ_FILE_DAT = 0, ANLZ_FILE_EXT = 1, ANLZ_FILE_2EX = 2 } anlz_file_t;
typedef struct anlz_track anlz_track_t;

/* Path of one of the track's files: root + analyze_path with the extension swapped (DAT, EXT, 2EX).
 * analyze_path is the DB's path of the .DAT (pdb_track_t.analyze_path, e.g. "/PIONEER/USBANLZ/P02E/0002F1FC/ANLZ0000.DAT").
 * It comes from an untrusted database: it must start with '/', contain no ".." component, no backslash and end in
 * ".DAT" (any case). Returns 0, or -1 when the path is rejected or does not fit in out. */
int anlz_resolve_path(const char *root, const char *analyze_path, anlz_file_t which, char *out, size_t outlen);

/* Open the track's files. Each is optional (the extension is matched in upper and lower case); a file that is
 * missing or not a valid ANLZ file is simply absent. Fails (the DAT's error, else IO) only when none could be read
 * or the path is rejected (ANLZ_ERR_BAD_LEN). */
anlz_err_t anlz_track_open(const char *root, const char *analyze_path, anlz_track_t **out);
void       anlz_track_close(anlz_track_t *t);
const anlz_t *anlz_track_file(const anlz_track_t *t, anlz_file_t which);      /* NULL when that file is absent */

anlz_err_t anlz_track_path(const anlz_track_t *t, const char **utf8);
/* PQTZ from the .DAT with microsecond fractions from the .EXT PQT2; the PQT2 alone when there is no .DAT PQTZ. */
anlz_err_t anlz_track_beatgrid(const anlz_track_t *t, anlz_beatgrid_t *out);
/* The .EXT PCO2 when there is one (complete: all hot cues, loop lengths, colours, comments); otherwise the .DAT
 * and .EXT PCOB lists merged without duplicates (a .DAT-only track has at most the first three hot cues). */
anlz_err_t anlz_track_cues(const anlz_track_t *t, anlz_cue_kind_t kind, anlz_cue_list_t *out);
/* Waveform from whichever file holds it: PWAV/PWV2 .DAT, PWV3/4/5 .EXT, PWV6/7 .2EX (PWV7 also tried in .EXT). */
anlz_err_t anlz_track_waveform(const anlz_track_t *t, anlz_wave_kind_t kind, anlz_waveform_t *out);

#ifdef __cplusplus
}
#endif

#endif /* CDJ_ANLZ_H */
