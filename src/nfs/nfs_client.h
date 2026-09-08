#ifndef CDJ_NFS_CLIENT_H
#define CDJ_NFS_CLIENT_H

#include "rpc.h"
#include "nfs_types.h"

#define NFS_PROG  100003
#define NFS_VERS  2

/* NFS v2 procedure numbers (RFC 1094). */
#define NFSPROC_NULL     0
#define NFSPROC_GETATTR  1
#define NFSPROC_SETATTR  2
#define NFSPROC_LOOKUP   4
#define NFSPROC_READ     6
#define NFSPROC_WRITE    7
#define NFSPROC_CREATE   8
#define NFSPROC_REMOVE   9
#define NFSPROC_RENAME   10
#define NFSPROC_MKDIR    13
#define NFSPROC_RMDIR    14
#define NFSPROC_READDIR  16
#define NFSPROC_STATFS   17

/* File type constants (FType in NFS v2). */
#define NFNON   0
#define NFREG   1
#define NFDIR   2
#define NFBLK   3
#define NFCHR   4
#define NFLNK   5

/* NFS error codes (subset). */
#define NFS_OK          0
#define NFSERR_PERM     1
#define NFSERR_NOENT    2
#define NFSERR_ACCES    13
#define NFSERR_EXIST    17
#define NFSERR_NOTDIR   20
#define NFSERR_ISDIR    21
#define NFSERR_FBIG     27
#define NFSERR_NOSPC    28
#define NFSERR_ROFS     30
#define NFSERR_NAMETOOLONG 63
#define NFSERR_NOTEMPTY 66
#define NFSERR_STALE    70

typedef struct {
    uint32_t type;
    uint32_t mode;
    uint32_t nlink;
    uint32_t uid;
    uint32_t gid;
    uint32_t size;
    uint32_t blocksize;
    uint32_t rdev;
    uint32_t blocks;
    uint32_t fsid;
    uint32_t fileid;
    uint32_t atime_sec;  uint32_t atime_usec;
    uint32_t mtime_sec;  uint32_t mtime_usec;
    uint32_t ctime_sec;  uint32_t ctime_usec;
} fattr_t;

typedef struct {
    uint8_t  *val;       /* UTF-16LE bytes */
    uint32_t  len;
} filename_t;

typedef struct {
    fhandle_t  dir;
    filename_t name;
} diropargs_t;

typedef struct {
    uint32_t  status;
    fhandle_t file;
    fattr_t   attributes;
} diropres_t;

typedef struct {
    fhandle_t file;
    uint32_t  offset;
    uint32_t  count;
    uint32_t  totalcount;
} readargs_t;

typedef struct {
    uint32_t  status;
    fattr_t   attributes;
    uint8_t  *data;
    uint32_t  data_len;
} readres_t;

typedef struct dir_entry {
    uint32_t        fileid;
    filename_t      name;
    uint8_t         cookie[COOKIESIZE];
    struct dir_entry *next;
} dir_entry_t;

typedef struct {
    uint32_t     status;
    dir_entry_t *entries;
    int          eof;
} readdirres_t;

/* Query portmapper for the NFS port. */
int nfs_getport(const char *host, uint16_t *port_out);

int nfs_null(rpc_client_t *c);
int nfs_getattr(rpc_client_t *c, fhandle_t *fh, fattr_t *attrs_out);
int nfs_lookup(rpc_client_t *c, diropargs_t *args, diropres_t *res_out);
int nfs_readdir(rpc_client_t *c, fhandle_t *dir,
                uint8_t cookie[COOKIESIZE], uint32_t count,
                readdirres_t *res_out);
int nfs_read(rpc_client_t *c, readargs_t *args, readres_t *res_out);
int nfs_statfs(rpc_client_t *c, fhandle_t *fh, uint32_t *tsize_out);

/*
 * Walk an ASCII path one component at a time from root_fh.
 * path must use '/' as separator, e.g. "PIONEER/rekordbox/export.pdb".
 */
int nfs_path_lookup(rpc_client_t *c, fhandle_t *root,
                    const char *path,
                    fhandle_t *fh_out, fattr_t *attr_out);

void nfs_readdirres_free(readdirres_t *res);
void nfs_readres_free(readres_t *res);

#endif /* CDJ_NFS_CLIENT_H */
