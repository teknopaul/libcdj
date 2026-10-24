#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "portmap_server.h"

#define PMAP_PROG 100000
#define RPC_CALL  0
#define RPC_REPLY 1

static uint32_t rd(const uint8_t *b, int o) { return ((uint32_t)b[o] << 24) | ((uint32_t)b[o + 1] << 16) | ((uint32_t)b[o + 2] << 8) | b[o + 3]; }
static int wr(uint8_t *b, int o, uint32_t v) { b[o] = v >> 24; b[o + 1] = v >> 16; b[o + 2] = v >> 8; b[o + 3] = v; return o + 4; }

int portmap_server_init(portmap_server_t *p, const uint16_t *ports, int nports, uint32_t bind_addr, char *errs, int errlen)
{
    memset(p, 0, sizeof *p);
    if (errs && errlen > 0) errs[0] = 0;
    for (int i = 0; i < nports && p->nfds < PMAP_MAX_SOCKETS; i++) {
        int fd = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
        struct sockaddr_in a;
        int one = 1;
        if (fd < 0) continue;
        setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
        memset(&a, 0, sizeof a);
        a.sin_family = AF_INET; a.sin_addr.s_addr = bind_addr; a.sin_port = htons(ports[i]);
        if (bind(fd, (struct sockaddr *)&a, sizeof a) < 0) {
            if (errs && errlen > 0) {
                size_t n = strlen(errs);
                snprintf(errs + n, (size_t)errlen - n, "%sportmap UDP %u: %s", n ? "; " : "", ports[i], strerror(errno));
            }
            close(fd);
            continue;
        }
        p->fds[p->nfds] = fd;
        p->ports[p->nfds++] = ports[i];
    }
    return p->nfds;
}

int portmap_server_register(portmap_server_t *p, uint32_t program, uint32_t version, uint16_t port)
{
    for (int i = 0; i < p->nentries; i++)
        if (p->entries[i].program == program && p->entries[i].version == version) { p->entries[i].port = port; return 0; }
    if (p->nentries >= PMAP_MAX_ENTRIES) return -1;
    p->entries[p->nentries].program = program; p->entries[p->nentries].version = version; p->entries[p->nentries].port = port;
    p->nentries++;
    return 0;
}

static const char *prog_name(uint32_t prog)
{
    return prog == 100005 ? "mount" : prog == 100003 ? "nfs" : prog == 100000 ? "portmap" : "program";
}

int portmap_server_handle(portmap_server_t *p, const uint8_t *call, int len, uint8_t *reply, int max, const char *from)
{
    char line[160];
    if (len < 24 || rd(call, 4) != RPC_CALL || max < 64) return 0;
    uint32_t xid = rd(call, 0), rpcvers = rd(call, 8), prog = rd(call, 12), vers = rd(call, 16), proc = rd(call, 20);
    int o = 24;
    for (int k = 0; k < 2; k++) {                          /* credentials, verifier */
        if (o + 8 > len) return 0;
        uint32_t n = rd(call, o + 4);
        o += 8 + (int)((n + 3) & ~3u);
    }
    int r = 0;
    r = wr(reply, r, xid); r = wr(reply, r, RPC_REPLY);
    if (rpcvers != 2) { r = wr(reply, r, 1); r = wr(reply, r, 0); r = wr(reply, r, 2); r = wr(reply, r, 2); return r; }   /* denied: RPC mismatch */
    r = wr(reply, r, 0);                                   /* MSG_ACCEPTED */
    r = wr(reply, r, 0); r = wr(reply, r, 0);              /* verifier AUTH_NONE */
    if (prog != PMAP_PROG) { r = wr(reply, r, 1); p->calls++; return r; }                         /* PROG_UNAVAIL */
    p->calls++;
    switch (proc) {
    case 0:                                                /* NULL */
        r = wr(reply, r, 0);
        break;
    case 3: {                                              /* GETPORT(prog, vers, proto, port) */
        if (o + 16 > len) { r = wr(reply, r, 4); break; }  /* GARBAGE_ARGS */
        uint32_t qp = rd(call, o), qv = rd(call, o + 4), qproto = rd(call, o + 8), port = 0;
        for (int i = 0; i < p->nentries; i++)
            if (p->entries[i].program == qp && p->entries[i].version == qv && qproto == 17) port = p->entries[i].port;
        p->getports++;
        r = wr(reply, r, 0); r = wr(reply, r, port);
        snprintf(line, sizeof line, "GETPORT %s v%u %s -> %u from %s", prog_name(qp), qv, qproto == 17 ? "udp" : "tcp", port, from ? from : "?");
        if (p->log) p->log(p->log_ctx, line);
        return r;
    }
    case 4:                                                /* DUMP: the list of mappings */
        r = wr(reply, r, 0);
        for (int i = 0; i < p->nentries && r + 24 < max; i++) {
            r = wr(reply, r, 1); r = wr(reply, r, p->entries[i].program); r = wr(reply, r, p->entries[i].version);
            r = wr(reply, r, 17); r = wr(reply, r, p->entries[i].port);
        }
        r = wr(reply, r, 0);
        break;
    case 1: case 2:                                        /* SET, UNSET: refused (result false) */
        r = wr(reply, r, 0); r = wr(reply, r, 0);
        break;
    default:
        r = wr(reply, r, 3);                               /* PROC_UNAVAIL */
        break;
    }
    (void)vers;
    snprintf(line, sizeof line, "portmap proc %u from %s", proc, from ? from : "?");
    if (p->log && proc != 3) p->log(p->log_ctx, line);
    return r;
}

int portmap_server_poll(portmap_server_t *p, int timeout_ms)
{
    struct pollfd pf[PMAP_MAX_SOCKETS];
    int answered = 0;
    if (p->nfds == 0) return 0;
    for (int i = 0; i < p->nfds; i++) { pf[i].fd = p->fds[i]; pf[i].events = POLLIN; pf[i].revents = 0; }
    int n = poll(pf, (nfds_t)p->nfds, timeout_ms);
    if (n < 0) return errno == EINTR ? 0 : -1;
    for (int i = 0; i < p->nfds; i++) {
        if (!(pf[i].revents & POLLIN)) continue;
        for (int k = 0; k < 16; k++) {                    /* drain a burst */
            uint8_t in[1500], out[1500];
            struct sockaddr_in from;
            socklen_t fl = sizeof from;
            ssize_t got = recvfrom(p->fds[i], in, sizeof in, MSG_DONTWAIT, (struct sockaddr *)&from, &fl);
            if (got <= 0) break;
            char who[32];
            snprintf(who, sizeof who, "%s:%u", inet_ntoa(from.sin_addr), ntohs(from.sin_port));
            int rl = portmap_server_handle(p, in, (int)got, out, sizeof out, who);
            if (rl > 0 && sendto(p->fds[i], out, (size_t)rl, 0, (struct sockaddr *)&from, fl) >= 0) answered++;
        }
    }
    return answered;
}

void portmap_server_destroy(portmap_server_t *p)
{
    for (int i = 0; i < p->nfds; i++) if (p->fds[i] >= 0) close(p->fds[i]);
    p->nfds = 0;
}
