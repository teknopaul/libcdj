#ifndef CDJ_NFS_FAKE_PDB_H
#define CDJ_NFS_FAKE_PDB_H

#include <stdint.h>

typedef struct {
    const char *title;
    const char *filename;
    const char *file_path;
    uint32_t    duration_secs;
    uint32_t    bpm_x100;      /* e.g. 12800 = 128.00 BPM */
} fake_track_t;

/* Returns malloc'd buffer containing a valid export.pdb, sets *len_out.
 * Caller must free() the returned pointer. */
uint8_t *fake_pdb_generate(const fake_track_t *track, uint32_t *len_out);

#endif /* CDJ_NFS_FAKE_PDB_H */
