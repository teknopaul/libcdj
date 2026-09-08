#include "portmap_client.h"
#include "rpc.h"
#include "xdr.h"

#define PMAP_PROG         100000
#define PMAP_VERS         2
#define PMAP_PORT         111
#define PMAPPROC_SET      1
#define PMAPPROC_UNSET    2
#define PMAPPROC_GETPORT  3
#define PMAP_PROT_UDP     17

typedef struct {
    uint32_t prog;
    uint32_t vers;
    uint32_t prot;
    uint32_t port;
} pmap_args_t;

static int encode_getport(xdr_t *x, void *arg) {
    pmap_args_t *a = (pmap_args_t *)arg;
    if (xdr_uint32(x, &a->prog) < 0) return -1;
    if (xdr_uint32(x, &a->vers) < 0) return -1;
    if (xdr_uint32(x, &a->prot) < 0) return -1;
    if (xdr_uint32(x, &a->port) < 0) return -1;
    return 0;
}

static int decode_getport(xdr_t *x, void *res) {
    return xdr_uint32(x, (uint32_t *)res);
}

static int decode_bool(xdr_t *x, void *res) {
    int *v = (int *)res;
    uint32_t u = 0;
    if (xdr_uint32(x, &u) < 0) return -1;
    *v = (u != 0) ? 1 : 0;
    return 0;
}

int portmap_set(const char *host, uint32_t prog, uint32_t vers, uint16_t port) {
    rpc_client_t c;
    pmap_args_t  args = { prog, vers, PMAP_PROT_UDP, port };
    int          ok   = 0;

    if (rpc_client_init(&c, host, PMAP_PORT, PMAP_PROG, PMAP_VERS, 3000) < 0)
        return -1;

    int rc = rpc_call(&c, PMAPPROC_SET, encode_getport, &args, decode_bool, &ok);
    rpc_client_destroy(&c);
    if (rc < 0) return -1;
    return ok ? 0 : 1;
}

int portmap_unset(const char *host, uint32_t prog, uint32_t vers) {
    rpc_client_t c;
    pmap_args_t  args = { prog, vers, PMAP_PROT_UDP, 0 };
    int          ok   = 0;

    if (rpc_client_init(&c, host, PMAP_PORT, PMAP_PROG, PMAP_VERS, 3000) < 0)
        return -1;

    int rc = rpc_call(&c, PMAPPROC_UNSET, encode_getport, &args, decode_bool, &ok);
    rpc_client_destroy(&c);
    if (rc < 0) return -1;
    return ok ? 0 : 1;
}

int portmap_getport(const char *host, uint32_t prog, uint32_t vers, uint16_t *port_out) {
    rpc_client_t c;
    pmap_args_t  args = { prog, vers, PMAP_PROT_UDP, 0 };
    uint32_t     port = 0;

    if (rpc_client_init(&c, host, PMAP_PORT, PMAP_PROG, PMAP_VERS, 3000) < 0)
        return -1;

    int rc = rpc_call(&c, PMAPPROC_GETPORT, encode_getport, &args,
                      decode_getport, &port);
    rpc_client_destroy(&c);

    if (rc < 0) return -1;
    if (port == 0) return 1;
    *port_out = (uint16_t)port;
    return 0;
}
