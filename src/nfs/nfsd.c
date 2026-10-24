#define _GNU_SOURCE
#include <errno.h>
#include <poll.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "portmap_server.h"
#include "nfsd.h"
#include "nfs_dirfs.h"
#include "nfs_server.h"
#include "portmap_server.h"
#include "utf16.h"

#define NFSD_DEFAULT_MOUNT_PORT 50112
#define NFSD_DEFAULT_NFS_PORT   2049

struct nfsd {
    nfsd_config_t    cfg;
    nfs_dirfs_t     *fs;
    nfs_server_t     srv;
    portmap_server_t pm;
    nfs_ops_t        ops;                 /* the dirfs ops, wrapped for logging */
    pthread_t        thread;
    atomic_int       stop;
    int              started;
    uint64_t         last_log_reads;      /* read log is summarised, one line per burst */
};

static void say(struct nfsd *d, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void say(struct nfsd *d, const char *fmt, ...)
{
    char line[512];
    va_list ap;
    if (!d->cfg.log) return;
    va_start(ap, fmt);
    vsnprintf(line, sizeof line, fmt, ap);
    va_end(ap);
    d->cfg.log(d->cfg.log_ctx, line);
}

static void pm_log(void *ctx, const char *line) { say(ctx, "%s", line); }

/* ---- the dirfs ops with a log line for what the players ask ---- */

static nfs_ops_t *inner(void) { return nfs_dirfs_ops(); }

static int w_list_exports(void *ctx, export_entry_t **out)
{
    struct nfsd *d = ctx;
    int r = inner()->list_exports(d->fs, out);
    say(d, "mountd EXPORT -> %s", r == 0 ? (*out ? "list sent" : "empty list sent") : "procedure unavailable");
    return r;
}

static int w_mount(void *ctx, dirpath_t *path, fhandle_t *fh)
{
    struct nfsd *d = ctx;
    char p[MNTPATHLEN * 3];
    if (utf16le_decode(path->val, path->len, p, sizeof p) < 0) snprintf(p, sizeof p, "?");
    int r = inner()->mount(d->fs, path, fh);
    say(d, "mountd MNT \"%s\" -> %s", p, r == 0 ? "ok" : "refused (not an export)");
    return r;
}

static int w_getattr(void *ctx, fhandle_t *fh, fattr_t *a)
{
    struct nfsd *d = ctx;
    char p[512];
    nfs_dirfs_path(d->fs, fh, p, sizeof p);
    int r = inner()->getattr(d->fs, fh, a);
    say(d, "nfs GETATTR \"%s\" -> %s", p, r == 0 ? "ok" : "error");
    return r;
}

static int w_lookup(void *ctx, fhandle_t *dir, filename_t *name, fhandle_t *fh, fattr_t *a)
{
    struct nfsd *d = ctx;
    char nm[MAXNAMLEN * 3 + 1];
    if (utf16le_decode(name->val, name->len, nm, sizeof nm) < 0) snprintf(nm, sizeof nm, "?");
    int r = inner()->lookup(d->fs, dir, name, fh, a);
    say(d, "nfs LOOKUP \"%s\" -> %s", nm, r == 0 ? (a->type == NFDIR ? "directory" : "file") : "no such file");
    return r;
}

static int w_readdir(void *ctx, fhandle_t *dir, uint32_t cookie, uint32_t max, dir_entry_t **e, int *eof)
{
    struct nfsd *d = ctx;
    char p[512];
    nfs_dirfs_path(d->fs, dir, p, sizeof p);
    int r = inner()->readdir(d->fs, dir, cookie, max, e, eof);
    say(d, "nfs READDIR \"%s\" cookie %u -> %s", p, cookie, r == 0 ? "ok" : "error");
    return r;
}

static int w_read(void *ctx, fhandle_t *fh, uint32_t off, uint32_t cnt, uint8_t *buf, uint32_t *got, fattr_t *a)
{
    struct nfsd *d = ctx;
    int r = inner()->read(d->fs, fh, off, cnt, buf, got, a);
    uint64_t n = nfs_dirfs_reads(d->fs);
    if (r == 0 && (off == 0 || n - d->last_log_reads >= 200)) {          /* a line at the start of a file and one per 200 reads */
        d->last_log_reads = n;
        say(d, "nfs READ off %u count %u (%llu reads, %llu bytes served so far)", off, cnt,
            (unsigned long long)n, (unsigned long long)nfs_dirfs_bytes_read(d->fs));
    } else if (r != 0) {
        say(d, "nfs READ off %u count %u -> error", off, cnt);
    }
    return r;
}

static void w_unserved(void *ctx, uint32_t program, uint32_t proc)
{
    say(ctx, "%s procedure %u asked for (not served)", program == NFS_PROG ? "nfs" : "rpc", proc);
}

static int w_statfs(void *ctx, fhandle_t *fh, uint32_t *t, uint32_t *b, uint32_t *bl, uint32_t *bf, uint32_t *ba)
{
    struct nfsd *d = ctx;
    int r = inner()->statfs(d->fs, fh, t, b, bl, bf, ba);
    say(d, "nfs STATFS -> %s", r == 0 ? "ok" : "error");
    return r;
}

/* ---- the service thread ---- */

static void *service(void *arg)
{
    struct nfsd *d = arg;
    while (!atomic_load(&d->stop)) {
        struct pollfd pf[PMAP_MAX_SOCKETS + 2];
        int n = 0;
        for (int i = 0; i < d->pm.nfds; i++) { pf[n].fd = d->pm.fds[i]; pf[n].events = POLLIN; pf[n].revents = 0; n++; }
        pf[n].fd = d->srv.mount_fd; pf[n].events = POLLIN; pf[n].revents = 0; n++;
        pf[n].fd = d->srv.nfs_fd;   pf[n].events = POLLIN; pf[n].revents = 0; n++;
        int rc = poll(pf, (nfds_t)n, 100);
        if (rc <= 0) continue;
        portmap_server_poll(&d->pm, 0);
        for (int burst = 0; burst < 256 && !atomic_load(&d->stop); burst++) {          /* the players send reads in bursts */
            struct pollfd q[2] = { { d->srv.mount_fd, POLLIN, 0 }, { d->srv.nfs_fd, POLLIN, 0 } };
            if (poll(q, 2, 0) <= 0) break;
            nfs_server_poll(&d->srv, 0);
        }
    }
    return NULL;
}

static void append_err(char *err, int errlen, const char *s)
{
    if (!err || errlen <= 0) return;
    size_t n = strlen(err);
    snprintf(err + n, (size_t)errlen - n, "%s%s", n ? "; " : "", s);
}

nfsd_t *nfsd_start(const nfsd_config_t *cfg, char *err, int errlen)
{
    if (err && errlen > 0) err[0] = 0;
    if (!cfg || !cfg->root) { append_err(err, errlen, "no root directory"); return NULL; }
    struct nfsd *d = calloc(1, sizeof *d);
    if (!d) return NULL;
    d->cfg = *cfg;
    uint16_t mport = cfg->mount_port ? cfg->mount_port : NFSD_DEFAULT_MOUNT_PORT;
    uint16_t nport = cfg->nfs_port ? cfg->nfs_port : NFSD_DEFAULT_NFS_PORT;

    d->fs = nfs_dirfs_open(cfg->root, cfg->exports);
    if (!d->fs) { append_err(err, errlen, "cannot serve that directory"); free(d); return NULL; }

    nfs_dirfs_set_export_style(d->fs, cfg->export_style);
    d->ops.list_exports = w_list_exports; d->ops.mount = w_mount; d->ops.getattr = w_getattr; d->ops.lookup = w_lookup;
    d->ops.unserved = w_unserved; d->ops.readdir = w_readdir; d->ops.read = w_read; d->ops.statfs = w_statfs;
    if (nfs_server_init(&d->srv, mport, nport, &d->ops, d) < 0) {
        char m[96];
        snprintf(m, sizeof m, "cannot bind mountd UDP %u / nfs UDP %u (in use?)", mport, nport);
        append_err(err, errlen, m);
        nfs_dirfs_close(d->fs); free(d);
        return NULL;
    }
    int buf = 4 << 20;                                   /* bursts of 8 KB reads over many client sockets */
    setsockopt(d->srv.nfs_fd, SOL_SOCKET, SO_RCVBUF, &buf, sizeof buf);
    setsockopt(d->srv.nfs_fd, SOL_SOCKET, SO_SNDBUF, &buf, sizeof buf);

    uint16_t ports[4] = { 50111, 111, 0, 0 };
    int nports = 2;
    if (cfg->portmap_ports[0]) {
        nports = 0;
        for (int i = 0; i < 4 && cfg->portmap_ports[i]; i++) ports[nports++] = cfg->portmap_ports[i];
    }
    char perr[256] = "";
    int bound = portmap_server_init(&d->pm, ports, nports, cfg->bind_addr, perr, sizeof perr);
    if (perr[0]) append_err(err, errlen, perr);
    if (bound == 0) {
        append_err(err, errlen, "no portmap port could be bound");
        nfs_server_destroy(&d->srv); nfs_dirfs_close(d->fs); free(d);
        return NULL;
    }
    d->pm.log = pm_log; d->pm.log_ctx = d;
    portmap_server_register(&d->pm, 100005, 1, mport);
    portmap_server_register(&d->pm, 100003, 2, nport);

    if (pthread_create(&d->thread, NULL, service, d) != 0) {
        append_err(err, errlen, "cannot start the thread");
        portmap_server_destroy(&d->pm); nfs_server_destroy(&d->srv); nfs_dirfs_close(d->fs); free(d);
        return NULL;
    }
    d->started = 1;
    return d;
}

void nfsd_stop(nfsd_t *d)
{
    if (!d) return;
    atomic_store(&d->stop, 1);
    if (d->started) pthread_join(d->thread, NULL);
    portmap_server_destroy(&d->pm);
    nfs_server_destroy(&d->srv);
    nfs_dirfs_close(d->fs);
    free(d);
}

int nfsd_portmap_ports(const nfsd_t *d, uint16_t *out, int max)
{
    int n = 0;
    for (int i = 0; i < d->pm.nfds && n < max; i++) out[n++] = d->pm.ports[i];
    return n;
}
uint16_t nfsd_mount_port(const nfsd_t *d) { return d->srv.mount_port; }
uint16_t nfsd_nfs_port(const nfsd_t *d)   { return d->srv.nfs_port; }
uint64_t nfsd_bytes_read(const nfsd_t *d) { return nfs_dirfs_bytes_read(d->fs); }
uint64_t nfsd_reads(const nfsd_t *d)      { return nfs_dirfs_reads(d->fs); }
uint64_t nfsd_calls(const nfsd_t *d)      { return d->pm.calls; }
