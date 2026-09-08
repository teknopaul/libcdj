#ifndef CDJ_NFS_PORTMAP_CLIENT_H
#define CDJ_NFS_PORTMAP_CLIENT_H

#include <stdint.h>

/* Register a UDP service with the portmapper. Returns 0 on success, 1 if refused, -1 on error. */
int portmap_set(const char *host, uint32_t prog, uint32_t vers, uint16_t port);

/* Unregister a service. Returns 0 on success, 1 if refused, -1 on error. */
int portmap_unset(const char *host, uint32_t prog, uint32_t vers);

/* Query portmapper for the UDP port of a service.
 * Returns 0 on success, 1 if not registered, -1 on transport error. */
int portmap_getport(const char *host, uint32_t prog, uint32_t vers, uint16_t *port_out);

#endif /* CDJ_NFS_PORTMAP_CLIENT_H */
