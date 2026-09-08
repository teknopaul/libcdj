#ifndef CDJ_NFS_MOUNT_CLIENT_H
#define CDJ_NFS_MOUNT_CLIENT_H

#include "rpc.h"
#include "nfs_types.h"

#define MOUNT_PROG  100005
#define MOUNT_VERS  1

/* Query portmapper on host to discover the mount daemon's UDP port. */
int mount_getport(const char *host, uint16_t *port_out);

/* MOUNTPROC_EXPORT (5): retrieve the list of exports. Caller frees with mount_export_free. */
int mount_export(rpc_client_t *c, export_entry_t **list_out);

/* MOUNTPROC_MNT (1): mount a filesystem path, receive the root file handle. */
int mount_mnt(rpc_client_t *c, dirpath_t *path, fhstatus_t *result);

void mount_export_free(export_entry_t *list);

#endif /* CDJ_NFS_MOUNT_CLIENT_H */
