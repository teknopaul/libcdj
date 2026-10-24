#ifndef CDJ_NFS_PORTMAP_SERVER_H
#define CDJ_NFS_PORTMAP_SERVER_H

#include <stdint.h>

/*
 * A minimal ONC-RPC portmapper (program 100000, version 2) over UDP: NULL, GETPORT and DUMP. SET, UNSET and CALLIT are refused.
 * It listens on every port it is allowed to bind. Port 111 needs root, but the players ask a rekordbox PC at UDP 50111 (seen in
 * captures), so one unprivileged port is enough for that; both can be listed and the ones that fail are skipped.
 */
#define PMAP_MAX_SOCKETS  4
#define PMAP_MAX_ENTRIES  16

typedef struct {
    uint32_t program, version;       /* proto is always UDP (17) */
    uint16_t port;
} pmap_entry_t;

typedef struct {
    int          fds[PMAP_MAX_SOCKETS];
    uint16_t     ports[PMAP_MAX_SOCKETS];     /* the ports actually bound */
    int          nfds;
    pmap_entry_t entries[PMAP_MAX_ENTRIES];
    int          nentries;
    uint64_t     calls, getports;
    /* optional: called for every call handled ("GETPORT mount v1 -> 50112 from 1.2.3.4") */
    void       (*log)(void *ctx, const char *line);
    void        *log_ctx;
} portmap_server_t;

/* Bind the given UDP ports (0 ends the list is not needed: nports counts). bind_addr is in network order, 0 = any.
 * Returns the number of ports bound; unbound ports are reported through errs (may be NULL, size errlen, ';' separated). */
int  portmap_server_init(portmap_server_t *p, const uint16_t *ports, int nports, uint32_t bind_addr, char *errs, int errlen);
int  portmap_server_register(portmap_server_t *p, uint32_t program, uint32_t version, uint16_t port);
/* Wait up to timeout_ms for a call and answer it (all readable sockets). Returns the number of calls answered, -1 on error. */
int  portmap_server_poll(portmap_server_t *p, int timeout_ms);
void portmap_server_destroy(portmap_server_t *p);

/* exposed for tests: build the reply to one call; returns its length or 0 when it is not a portmap call at all */
int  portmap_server_handle(portmap_server_t *p, const uint8_t *call, int len, uint8_t *reply, int reply_max, const char *from);

#endif /* CDJ_NFS_PORTMAP_SERVER_H */
