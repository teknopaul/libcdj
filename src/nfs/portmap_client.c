#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <arpa/inet.h>

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

#define RPCBIND_SOCK      "/run/rpcbind.sock"

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

/* Read exactly n bytes from a stream fd. */
static int read_all(int fd, uint8_t *buf, uint32_t n) {
    uint32_t got = 0;
    while (got < n) {
        ssize_t r = read(fd, buf + got, n - got);
        if (r <= 0) return -1;
        got += (uint32_t)r;
    }
    return 0;
}

/*
 * Register or unregister a service via rpcbind's local Unix socket.
 * The Unix socket bypasses rpcbind's privileged-port source check.
 * Uses TCP record marking (4-byte big-endian length prefix, MSB = last fragment).
 * Returns 0 if rpcbind says ok, 1 if refused, -1 on transport error.
 */
static int portmap_via_sock(uint32_t prog, uint32_t vers, uint16_t port, uint32_t proc) {
    uint8_t  body[128];
    uint8_t  reply[128];
    xdr_t    bx;

    xdr_init_encode(&bx, body, sizeof(body));

    uint32_t xid      = 0x4c424344u; /* "LBCD" */
    uint32_t call     = 0;
    uint32_t rpcvers  = 2;
    uint32_t pmap_p   = PMAP_PROG;
    uint32_t pmap_v   = PMAP_VERS;
    uint32_t zero     = 0;
    uint32_t prot     = PMAP_PROT_UDP;
    uint32_t p32      = port;

    /* RPC call header with AUTH_NONE */
    if (xdr_uint32(&bx, &xid)     < 0) return -1;
    if (xdr_uint32(&bx, &call)    < 0) return -1;
    if (xdr_uint32(&bx, &rpcvers) < 0) return -1;
    if (xdr_uint32(&bx, &pmap_p)  < 0) return -1;
    if (xdr_uint32(&bx, &pmap_v)  < 0) return -1;
    if (xdr_uint32(&bx, &proc)    < 0) return -1;
    if (xdr_uint32(&bx, &zero)    < 0) return -1;  /* cred flavor AUTH_NONE */
    if (xdr_uint32(&bx, &zero)    < 0) return -1;  /* cred len 0 */
    if (xdr_uint32(&bx, &zero)    < 0) return -1;  /* verf flavor AUTH_NONE */
    if (xdr_uint32(&bx, &zero)    < 0) return -1;  /* verf len 0 */
    /* PMAP arguments */
    if (xdr_uint32(&bx, &prog)    < 0) return -1;
    if (xdr_uint32(&bx, &vers)    < 0) return -1;
    if (xdr_uint32(&bx, &prot)    < 0) return -1;
    if (xdr_uint32(&bx, &p32)     < 0) return -1;

    uint32_t body_len = bx.pos;

    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return -1;

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, RPCBIND_SOCK, sizeof(addr.sun_path) - 1);

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(fd);
        return -1;
    }

    /* TCP record marking: MSB=1 signals last (and only) fragment */
    uint32_t mark = htonl(body_len | 0x80000000u);
    if (write(fd, &mark, 4) != 4 ||
        write(fd, body, body_len) != (ssize_t)body_len) {
        close(fd);
        return -1;
    }

    /* Read reply record mark */
    uint32_t rmark = 0;
    if (read_all(fd, (uint8_t *)&rmark, 4) < 0) { close(fd); return -1; }
    uint32_t rlen = ntohl(rmark) & 0x7fffffffu;
    if (rlen == 0 || rlen > sizeof(reply)) { close(fd); return -1; }
    if (read_all(fd, reply, rlen) < 0) { close(fd); return -1; }
    close(fd);

    /* Decode reply: xid + REPLY + ACCEPTED + verf + SUCCESS + bool_result */
    xdr_t rx;
    xdr_init_decode(&rx, reply, rlen);
    uint32_t rxid, msg_type, reply_stat, verf_flavor, verf_len, accept_stat, result;
    if (xdr_uint32(&rx, &rxid)        < 0) return -1;
    if (xdr_uint32(&rx, &msg_type)    < 0) return -1;
    if (msg_type != 1)                     return -1;  /* not REPLY */
    if (xdr_uint32(&rx, &reply_stat)  < 0) return -1;
    if (reply_stat != 0)                   return -1;  /* not MSG_ACCEPTED */
    if (xdr_uint32(&rx, &verf_flavor) < 0) return -1;
    if (xdr_uint32(&rx, &verf_len)    < 0) return -1;
    rx.pos += verf_len;
    if (xdr_uint32(&rx, &accept_stat) < 0) return -1;
    if (accept_stat != 0)                  return -1;  /* not RPC_SUCCESS */
    if (xdr_uint32(&rx, &result)      < 0) return -1;
    return result ? 0 : 1;
}

int portmap_set(const char *host, uint32_t prog, uint32_t vers, uint16_t port) {
    /* Try the local Unix socket first — bypasses rpcbind's privileged-port check. */
    int rc = portmap_via_sock(prog, vers, port, PMAPPROC_SET);
    if (rc >= 0) return rc;

    /* Fall back to UDP (works if running as root or with a privileged source port). */
    rpc_client_t c;
    pmap_args_t  args = { prog, vers, PMAP_PROT_UDP, port };
    int          ok   = 0;

    if (rpc_client_init(&c, host, PMAP_PORT, PMAP_PROG, PMAP_VERS, 3000) < 0)
        return -1;

    rc = rpc_call(&c, PMAPPROC_SET, encode_getport, &args, decode_bool, &ok);
    rpc_client_destroy(&c);
    if (rc < 0) return -1;
    return ok ? 0 : 1;
}

int portmap_unset(const char *host, uint32_t prog, uint32_t vers) {
    /* Try the local Unix socket first. */
    int rc = portmap_via_sock(prog, vers, 0, PMAPPROC_UNSET);
    if (rc >= 0) return rc;

    rpc_client_t c;
    pmap_args_t  args = { prog, vers, PMAP_PROT_UDP, 0 };
    int          ok   = 0;

    if (rpc_client_init(&c, host, PMAP_PORT, PMAP_PROG, PMAP_VERS, 3000) < 0)
        return -1;

    rc = rpc_call(&c, PMAPPROC_UNSET, encode_getport, &args, decode_bool, &ok);
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
