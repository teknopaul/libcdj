#ifndef CDJ_NFS_DIRFS_H
#define CDJ_NFS_DIRFS_H

#include "nfs_server.h"

/*
 * A read-only directory tree behind nfs_ops_t: what a rekordbox export folder or a pendrive looks like to a player.
 *
 *  - exports: the mount paths accepted (UTF-8, e.g. "/C/"), compared without regard to case and to a trailing slash;
 *    every one of them mounts the root directory
 *  - LOOKUP matches names without regard to case (the players ask for "intapol" where the folder is "Intapol", the sticks are FAT)
 *  - names are UTF-16LE on the wire and UTF-8 on disk; "/" and NUL in a name, symlinks and everything outside the root are refused
 *  - nothing is ever written
 */
typedef struct nfs_dirfs nfs_dirfs_t;

/* exports: NULL-terminated array of mount paths, NULL means {"/C/"}. Returns NULL when root is not a directory. */
nfs_dirfs_t *nfs_dirfs_open(const char *root, const char *const *exports);
void         nfs_dirfs_close(nfs_dirfs_t *fs);
nfs_ops_t   *nfs_dirfs_ops(void);                 /* pass with the nfs_dirfs_t* as ctx to nfs_server_init() */

/* counters for logging / tests */
/* How mountd EXPORT is answered: 0 a list of the export names (default), 1 the same with the group "*" after each name,
 * 2 an empty list, 3 "procedure unavailable" (the list_exports callback fails). */
void nfs_dirfs_set_export_style(nfs_dirfs_t *fs, int style);

/* The path (relative to the root, "/" for the root) a file handle stands for, for log lines. Returns 0, or -1 for a stale handle. */
int nfs_dirfs_path(nfs_dirfs_t *fs, const fhandle_t *fh, char *out, size_t n);

uint64_t nfs_dirfs_bytes_read(const nfs_dirfs_t *fs);
uint64_t nfs_dirfs_reads(const nfs_dirfs_t *fs);

#endif /* CDJ_NFS_DIRFS_H */
