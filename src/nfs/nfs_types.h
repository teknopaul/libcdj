#ifndef CDJ_NFS_TYPES_H
#define CDJ_NFS_TYPES_H

#include <stdint.h>
#include "xdr.h"

#define FHSIZE        32
#define MNTPATHLEN    1024
#define MNTNAMLEN     255
#define MAXNAMLEN     255
#define NFSMAXPATHLEN 1024
#define MAXDATA       8192
#define COOKIESIZE    4

typedef struct { uint8_t data[FHSIZE]; } fhandle_t;

/* Variable-length opaque bytes — Pioneer encodes paths as UTF-16LE here. */
typedef struct {
    uint8_t  *val;
    uint32_t  len;
} dirpath_t;

typedef struct {
    uint32_t  status;       /* 0 = OK, NFS errno otherwise */
    fhandle_t directory;    /* valid when status == 0 */
} fhstatus_t;

typedef struct export_entry {
    dirpath_t           filesystem;
    struct export_entry *next;
} export_entry_t;

/* XDR codecs for shared NFS types (implemented in mount_client.c). */
int xdr_fhandle(xdr_t *x, fhandle_t *fh);
int xdr_dirpath(xdr_t *x, dirpath_t *dp);
int xdr_fhstatus(xdr_t *x, fhstatus_t *fhs);

#endif /* CDJ_NFS_TYPES_H */
