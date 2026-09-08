#ifndef CDJ_NFS_RPC_H
#define CDJ_NFS_RPC_H

#include <stdint.h>
#include <netinet/in.h>
#include "xdr.h"

/*
 * Minimal ONC-RPC over UDP client (RFC 1831).
 * No dependency on rpc/rpc.h or libnsl.
 */

typedef struct {
    int                sock;
    uint32_t           prog;
    uint32_t           vers;
    struct sockaddr_in server_addr;
    int                timeout_ms;
} rpc_client_t;

/* Returns 0 on success, -1 on error. */
int  rpc_client_init(rpc_client_t *c, const char *host, uint16_t port,
                     uint32_t prog, uint32_t vers, int timeout_ms);
void rpc_client_destroy(rpc_client_t *c);

/*
 * Issue an RPC call using AUTH_NONE credentials.
 *
 * args_fn: encodes procedure arguments into the XDR buffer (may be NULL)
 * res_fn:  decodes procedure results from the XDR buffer (may be NULL)
 *
 * Returns 0 on success, -1 on transport/decode error,
 *         or a positive RPC accept_stat on RPC-layer failure.
 */
int rpc_call(rpc_client_t *c, uint32_t proc,
             int (*args_fn)(xdr_t *, void *), void *args,
             int (*res_fn)(xdr_t *, void *),  void *res);

/*
 * Same as rpc_call but uses AUTH_UNIX credentials (uid=0, gid=0).
 * Try this if AUTH_NONE results in NFSERR_ACCES from the Pioneer player.
 */
int rpc_call_unix_auth(rpc_client_t *c, uint32_t proc,
                       int (*args_fn)(xdr_t *, void *), void *args,
                       int (*res_fn)(xdr_t *, void *),  void *res);

/* RPC accept_stat values (returned as positive integers on RPC error). */
#define RPC_SUCCESS         0
#define RPC_PROG_UNAVAIL    1
#define RPC_PROG_MISMATCH   2
#define RPC_PROC_UNAVAIL    3
#define RPC_GARBAGE_ARGS    4
#define RPC_SYSTEM_ERR      5

#endif /* CDJ_NFS_RPC_H */
