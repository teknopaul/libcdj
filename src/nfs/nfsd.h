#ifndef CDJ_NFS_NFSD_H
#define CDJ_NFS_NFSD_H

#include <stdint.h>

/*
 * A read-only NFS file service for the players: portmapper(s) + mountd + NFS v2 over UDP, serving a directory (a rekordbox export
 * folder or a pendrive) on one thread of its own.
 *
 *   portmap  UDP 50111 (where the players ask a rekordbox PC, no root) and 111 (root); the ports that cannot be bound are skipped
 *   mountd   UDP 50112     NFS  UDP 2049      both are announced through GETPORT
 */
typedef struct nfsd nfsd_t;

typedef struct {
    const char        *root;                 /* directory to serve (required) */
    const char *const *exports;              /* mount paths, NULL-terminated, NULL = {"/C/"} */
    uint32_t           bind_addr;            /* network order, 0 = any (the portmapper only) */
    uint16_t           portmap_ports[4];     /* 0 ends the list; all zero = {50111, 111} */
    uint16_t           mount_port;           /* 0 = 50112 */
    uint16_t           nfs_port;             /* 0 = 2049 */
    int                export_style;         /* mountd EXPORT: 0 list (default), 1 list with group "*", 2 empty list, 3 procedure unavailable */
    void             (*log)(void *ctx, const char *line);    /* called from the service thread */
    void              *log_ctx;
} nfsd_config_t;

/* Start the service. Returns NULL (and a reason in err) when nothing could be served: root not a directory, mount/nfs port busy,
 * or no portmap port could be bound. Portmap ports that failed are listed in err too, with a non-NULL result. */
nfsd_t  *nfsd_start(const nfsd_config_t *cfg, char *err, int errlen);
void     nfsd_stop(nfsd_t *d);                      /* stops the thread and closes the sockets; safe with NULL */
int      nfsd_portmap_ports(const nfsd_t *d, uint16_t *out, int max);     /* the portmap ports bound */
uint16_t nfsd_mount_port(const nfsd_t *d);
uint16_t nfsd_nfs_port(const nfsd_t *d);
uint64_t nfsd_bytes_read(const nfsd_t *d);
uint64_t nfsd_reads(const nfsd_t *d);
uint64_t nfsd_calls(const nfsd_t *d);               /* RPC calls answered by the portmapper */

#endif /* CDJ_NFS_NFSD_H */
