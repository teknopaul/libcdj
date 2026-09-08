#ifndef _VDJ_NFS_H_INCLUDED_
#define _VDJ_NFS_H_INCLUDED_

#include "vdj.h"

/* Opaque NFS state — allocated by vdj_nfs_init, stored in v->client. */
typedef struct vdj_nfs_s vdj_nfs_t;

/*
 * Attach an NFS server to a vdj instance.  Binds UDP sockets on mount_port
 * and nfs_port, generates an initial export.pdb, and stores state in v->client.
 * Returns 0 on success, -1 on error.
 * WARNING: if NFS port 2049 is needed, run as root.
 */
int vdj_nfs_init(vdj_t *v, uint16_t mount_port, uint16_t nfs_port);

/*
 * Service pending NFS requests.  Call this regularly from the main event loop.
 * timeout_ms: max time to wait for a packet (0 = non-blocking).
 */
int vdj_nfs_poll(vdj_t *v, int timeout_ms);

/*
 * Update the fake track information served by export.pdb.
 * Regenerates the PDB in memory — subsequent READ requests return the new data.
 */
void vdj_nfs_update_track(vdj_t *v, const void *track);

void vdj_nfs_destroy(vdj_t *v);

#endif /* _VDJ_NFS_H_INCLUDED_ */
