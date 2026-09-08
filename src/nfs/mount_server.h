#ifndef CDJ_NFS_MOUNT_SERVER_H
#define CDJ_NFS_MOUNT_SERVER_H

#include <netinet/in.h>
#include "nfs_server.h"
#include "xdr.h"

/*
 * Process one decoded mount RPC call and write the reply into tx.
 * Returns 0 on success (reply encoded), -1 on fatal encode error.
 */
int mount_server_dispatch(xdr_t *rx, xdr_t *tx,
                          uint32_t xid, uint32_t proc,
                          nfs_ops_t *ops, void *ctx);

#endif /* CDJ_NFS_MOUNT_SERVER_H */
