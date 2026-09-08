#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>
#include <errno.h>

#include "xdr.h"
#include "rpc.h"

#define RPC_BUF_SIZE    65535
#define RPC_MAX_RETRIES 3

/* ONC-RPC message types */
#define RPC_CALL    0
#define RPC_REPLY   1

/* Reply status */
#define MSG_ACCEPTED  0

static uint32_t next_xid(void) {
    static uint32_t xid = 0;
    if (xid == 0) {
        srand((unsigned int)time(NULL));
        xid = (uint32_t)rand() | 1;
    }
    return ++xid;
}

int rpc_client_init(rpc_client_t *c, const char *host, uint16_t port,
                    uint32_t prog, uint32_t vers, int timeout_ms) {
    memset(c, 0, sizeof(*c));
    c->prog       = prog;
    c->vers       = vers;
    c->timeout_ms = timeout_ms;
    c->sock       = socket(AF_INET, SOCK_DGRAM, 0);
    if (c->sock < 0) return -1;

    memset(&c->server_addr, 0, sizeof(c->server_addr));
    c->server_addr.sin_family = AF_INET;
    c->server_addr.sin_port   = htons(port);
    if (inet_pton(AF_INET, host, &c->server_addr.sin_addr) <= 0) {
        close(c->sock);
        c->sock = -1;
        return -1;
    }
    return 0;
}

void rpc_client_destroy(rpc_client_t *c) {
    if (c->sock >= 0) {
        close(c->sock);
        c->sock = -1;
    }
}

static int encode_call_header(xdr_t *x, uint32_t xid, uint32_t prog,
                               uint32_t vers, uint32_t proc) {
    uint32_t msg_type = RPC_CALL;
    uint32_t rpcvers  = 2;
    uint32_t zero     = 0;

    if (xdr_uint32(x, &xid)      < 0) return -1;
    if (xdr_uint32(x, &msg_type) < 0) return -1;
    if (xdr_uint32(x, &rpcvers)  < 0) return -1;
    if (xdr_uint32(x, &prog)     < 0) return -1;
    if (xdr_uint32(x, &vers)     < 0) return -1;
    if (xdr_uint32(x, &proc)     < 0) return -1;
    /* cred: AUTH_NONE, length 0 */
    if (xdr_uint32(x, &zero) < 0) return -1;
    if (xdr_uint32(x, &zero) < 0) return -1;
    /* verf: AUTH_NONE, length 0 */
    if (xdr_uint32(x, &zero) < 0) return -1;
    if (xdr_uint32(x, &zero) < 0) return -1;
    return 0;
}

static int encode_unix_auth_header(xdr_t *x, uint32_t xid, uint32_t prog,
                                    uint32_t vers, uint32_t proc) {
    uint32_t msg_type    = RPC_CALL;
    uint32_t rpcvers     = 2;
    uint32_t cred_flavor = 1;  /* AUTH_UNIX */
    uint32_t verf_flavor = 0;  /* AUTH_NONE */
    uint32_t zero        = 0;

    /* Pre-encode AUTH_UNIX body: stamp + machinename + uid + gid + gids[] */
    uint8_t body[64];
    xdr_t   bx;
    xdr_init_encode(&bx, body, sizeof(body));

    uint32_t  stamp    = 0;
    uint8_t  *mname    = (uint8_t *)"libcdj";
    uint32_t  mname_len = 6;
    uint32_t  uid      = 0;
    uint32_t  gid      = 0;
    uint32_t  ngids    = 0;

    if (xdr_uint32(&bx, &stamp)                            < 0) return -1;
    if (xdr_varbytes(&bx, &mname, &mname_len, 255)        < 0) return -1;
    if (xdr_uint32(&bx, &uid)                             < 0) return -1;
    if (xdr_uint32(&bx, &gid)                             < 0) return -1;
    if (xdr_uint32(&bx, &ngids)                           < 0) return -1;

    uint32_t cred_len = bx.pos;

    if (xdr_uint32(x, &xid)       < 0) return -1;
    if (xdr_uint32(x, &msg_type)  < 0) return -1;
    if (xdr_uint32(x, &rpcvers)   < 0) return -1;
    if (xdr_uint32(x, &prog)      < 0) return -1;
    if (xdr_uint32(x, &vers)      < 0) return -1;
    if (xdr_uint32(x, &proc)      < 0) return -1;
    /* cred: AUTH_UNIX */
    if (xdr_uint32(x, &cred_flavor) < 0) return -1;
    if (xdr_uint32(x, &cred_len)    < 0) return -1;
    if (xdr_opaque(x, body, cred_len) < 0) return -1;
    /* verf: AUTH_NONE */
    if (xdr_uint32(x, &verf_flavor) < 0) return -1;
    if (xdr_uint32(x, &zero)        < 0) return -1;
    return 0;
}

static int decode_reply_header(xdr_t *x, uint32_t expected_xid) {
    uint32_t xid, msg_type, reply_stat, verf_flavor, verf_len, accept_stat;

    if (xdr_uint32(x, &xid)       < 0) return -1;
    if (xid != expected_xid)           return -2;  /* xid mismatch */
    if (xdr_uint32(x, &msg_type)  < 0) return -1;
    if (msg_type != RPC_REPLY)         return -1;
    if (xdr_uint32(x, &reply_stat) < 0) return -1;
    if (reply_stat != MSG_ACCEPTED)    return -1;
    /* skip verifier */
    if (xdr_uint32(x, &verf_flavor) < 0) return -1;
    if (xdr_uint32(x, &verf_len)    < 0) return -1;
    if (verf_len > 0) {
        uint32_t pad = (4 - (verf_len & 3)) & 3;
        if (x->pos + verf_len + pad > x->len) return -1;
        x->pos += verf_len + pad;
    }
    if (xdr_uint32(x, &accept_stat) < 0) return -1;
    if (accept_stat != RPC_SUCCESS) return (int)accept_stat;
    return 0;
}

static int do_rpc_call(rpc_client_t *c, uint32_t proc, int unix_auth,
                       int (*args_fn)(xdr_t *, void *), void *args,
                       int (*res_fn)(xdr_t *, void *),  void *res) {
    uint8_t  send_buf[RPC_BUF_SIZE];
    uint8_t  recv_buf[RPC_BUF_SIZE];
    xdr_t    x;
    uint32_t xid = next_xid();
    int      attempt;

    xdr_init_encode(&x, send_buf, sizeof(send_buf));

    if (unix_auth) {
        if (encode_unix_auth_header(&x, xid, c->prog, c->vers, proc) < 0)
            return -1;
    } else {
        if (encode_call_header(&x, xid, c->prog, c->vers, proc) < 0)
            return -1;
    }

    if (args_fn && args_fn(&x, args) < 0) return -1;

    for (attempt = 0; attempt < RPC_MAX_RETRIES; attempt++) {
        ssize_t nb = sendto(c->sock, send_buf, x.pos, 0,
                            (struct sockaddr *)&c->server_addr,
                            sizeof(c->server_addr));
        if (nb < 0) return -1;

        fd_set         fds;
        struct timeval tv;
        FD_ZERO(&fds);
        FD_SET(c->sock, &fds);
        tv.tv_sec  = c->timeout_ms / 1000;
        tv.tv_usec = (c->timeout_ms % 1000) * 1000;

        int sel = select(c->sock + 1, &fds, NULL, NULL, &tv);
        if (sel < 0) return -1;
        if (sel == 0) continue;  /* timeout — retransmit */

        nb = recvfrom(c->sock, recv_buf, sizeof(recv_buf), 0, NULL, NULL);
        if (nb < 0) return -1;

        xdr_t rx;
        xdr_init_decode(&rx, recv_buf, (uint32_t)nb);
        int rc = decode_reply_header(&rx, xid);
        if (rc == -2) continue;  /* xid mismatch — retransmit */
        if (rc < 0)  return -1;
        if (rc > 0)  return rc;  /* RPC accept_stat error */

        if (res_fn && res_fn(&rx, res) < 0) return -1;
        return 0;
    }
    return -1;  /* all retries exhausted */
}

int rpc_call(rpc_client_t *c, uint32_t proc,
             int (*args_fn)(xdr_t *, void *), void *args,
             int (*res_fn)(xdr_t *, void *),  void *res) {
    return do_rpc_call(c, proc, 0, args_fn, args, res_fn, res);
}

int rpc_call_unix_auth(rpc_client_t *c, uint32_t proc,
                       int (*args_fn)(xdr_t *, void *), void *args,
                       int (*res_fn)(xdr_t *, void *),  void *res) {
    return do_rpc_call(c, proc, 1, args_fn, args, res_fn, res);
}
