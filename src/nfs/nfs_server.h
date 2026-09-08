#ifndef CDJ_NFS_SERVER_H
#define CDJ_NFS_SERVER_H

#include <stdint.h>
#include "nfs_types.h"
#include "nfs_client.h"

/*
 * NFS v2 + Mount v1 server over UDP — virtual filesystem via callbacks.
 * Read-only. Single-threaded; caller drives the event loop via nfs_server_poll.
 */

typedef struct {
    int (*list_exports)(void *ctx, export_entry_t **list_out);
    int (*mount)(void *ctx, dirpath_t *path, fhandle_t *fh_out);
    int (*getattr)(void *ctx, fhandle_t *fh, fattr_t *attrs_out);
    int (*lookup)(void *ctx, fhandle_t *dir, filename_t *name,
                  fhandle_t *fh_out, fattr_t *attrs_out);
    int (*readdir)(void *ctx, fhandle_t *dir, uint32_t cookie,
                   uint32_t max_bytes, dir_entry_t **entries_out, int *eof_out);
    int (*read)(void *ctx, fhandle_t *fh, uint32_t offset, uint32_t count,
                uint8_t *buf_out, uint32_t *bytes_out, fattr_t *attrs_out);
    int (*statfs)(void *ctx, fhandle_t *fh, uint32_t *tsize_out,
                  uint32_t *bsize_out, uint32_t *blocks_out,
                  uint32_t *bfree_out, uint32_t *bavail_out);
} nfs_ops_t;

typedef struct {
    int       mount_fd;
    int       nfs_fd;
    uint16_t  mount_port;
    uint16_t  nfs_port;
    nfs_ops_t *ops;
    void      *ctx;
    int        running;
} nfs_server_t;

int  nfs_server_init(nfs_server_t *s, uint16_t mount_port, uint16_t nfs_port,
                     nfs_ops_t *ops, void *ctx);
int  nfs_server_poll(nfs_server_t *s, int timeout_ms);
void nfs_server_destroy(nfs_server_t *s);

/*
 * Simple inode-based file handle scheme (32 bytes):
 *   [0-3]  magic  0xCDCD0001
 *   [4-7]  fsid
 *   [8-11] inode
 *   [12-31] zeros
 */
#define NFS_FH_MAGIC  0xCDCD0001U

void nfs_fh_make(fhandle_t *fh, uint32_t fsid, uint32_t inode);
int  nfs_fh_parse(const fhandle_t *fh, uint32_t *fsid_out, uint32_t *inode_out);

#endif /* CDJ_NFS_SERVER_H */
