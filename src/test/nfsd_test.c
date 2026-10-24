/*
 * End to end test of the NFS file service (portmap + mountd + NFS v2) over real UDP sockets on 127.0.0.1, using libcdj's own clients.
 * usage: nfsd_test            (ports 53111 / 53112 / 53049)
 */
#define _GNU_SOURCE
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "../nfs/mount_client.h"
#include "../nfs/nfs_client.h"
#include "../nfs/nfsd.h"
#include "../nfs/rpc.h"
#include "../nfs/utf16.h"

#define PM_PORT 53111
#define MT_PORT 53112
#define NF_PORT 53049
#define FILE_SIZE 100000

static int failures;
#define CHECK(cond, msg) do { if (!(cond)) { failures++; fprintf(stderr, "[error] %s (line %d)\n", msg, __LINE__); } } while (0)

static int logged;
static char last_mount_line[256];
static void on_log(void *ctx, const char *line)
{
    (void)ctx;
    logged++;
    if (strstr(line, "MNT")) snprintf(last_mount_line, sizeof last_mount_line, "%s", line);
}

typedef struct { uint32_t prog, vers; } gp_t;
static int enc_getport(xdr_t *x, void *a)
{
    gp_t *g = a; uint32_t proto = 17, port = 0;
    return xdr_uint32(x, &g->prog) | xdr_uint32(x, &g->vers) | xdr_uint32(x, &proto) | xdr_uint32(x, &port);
}
static int dec_u32(xdr_t *x, void *r) { return xdr_uint32(x, (uint32_t *)r); }

static uint32_t getport(uint32_t prog, uint32_t vers)
{
    rpc_client_t c;
    uint32_t port = 12345;
    gp_t g = { prog, vers };
    if (rpc_client_init(&c, "127.0.0.1", PM_PORT, 100000, 2, 2000) < 0) return 0xffff;
    int rc = rpc_call(&c, 3, enc_getport, &g, dec_u32, &port);
    rpc_client_destroy(&c);
    return rc == 0 ? port : 0xfffe;
}

static void put_tree(const char *root)
{
    char p[512];
    snprintf(p, sizeof p, "%s/Contents/Intapol/UK Dembow", root);
    for (char *s = p + strlen(root) + 1; *s; s++)
        if (*s == '/') { *s = 0; mkdir(p, 0755); *s = '/'; }
    mkdir(p, 0755);
    snprintf(p, sizeof p, "%s/Contents/Intapol/UK Dembow/Track-02.mp3", root);
    FILE *f = fopen(p, "wb");
    for (int i = 0; i < FILE_SIZE; i++) fputc((i * 7 + i / 251) & 0xff, f);
    fclose(f);
    snprintf(p, sizeof p, "%s/outside.txt", root);                   /* a file the symlink below would reach */
    snprintf(p, sizeof p, "%s/Contents/escape", root);
    symlink("/etc", p);
}

static uint8_t expect_byte(int i) { return (uint8_t)((i * 7 + i / 251) & 0xff); }

static int lookup(rpc_client_t *c, fhandle_t *dir, const char *name, fhandle_t *out, fattr_t *a)
{
    diropargs_t args;
    diropres_t res;
    uint8_t u16[600]; uint32_t n = 0;
    utf8_to_utf16le(name, u16, sizeof u16, &n);
    args.dir = *dir; args.name.val = u16; args.name.len = n;
    memset(&res, 0, sizeof res);
    if (nfs_lookup(c, &args, &res) != 0 || res.status != 0) return -1;
    *out = res.file; if (a) *a = res.attributes;
    return 0;
}

int main(void)
{
    char root[] = "/tmp/nfsd_test_XXXXXX";
    if (!mkdtemp(root)) { perror("mkdtemp"); return 2; }
    put_tree(root);

    char err[256];
    const char *exports[] = { "/C/", NULL };
    nfsd_config_t cfg;
    memset(&cfg, 0, sizeof cfg);
    cfg.root = root; cfg.exports = exports;
    cfg.portmap_ports[0] = PM_PORT; cfg.mount_port = MT_PORT; cfg.nfs_port = NF_PORT;
    cfg.log = on_log;
    nfsd_t *d = nfsd_start(&cfg, err, sizeof err);
    if (!d) { fprintf(stderr, "[error] nfsd_start: %s\n", err); return 3; }

    /* portmap */
    CHECK(getport(100005, 1) == MT_PORT, "GETPORT mountd v1 gives the mount port");
    CHECK(getport(100003, 2) == NF_PORT, "GETPORT nfs v2 gives the nfs port");
    CHECK(getport(100003, 3) == 0, "GETPORT of an unknown version is 0");
    CHECK(getport(123456, 1) == 0, "GETPORT of an unknown program is 0");

    /* mountd */
    rpc_client_t mc, nc;
    CHECK(rpc_client_init(&mc, "127.0.0.1", MT_PORT, 100005, 1, 2000) == 0, "mount client");
    CHECK(rpc_client_init(&nc, "127.0.0.1", NF_PORT, 100003, 2, 2000) == 0, "nfs client");
    export_entry_t *ex = NULL;
    CHECK(mount_export(&mc, &ex) == 0 && ex, "EXPORT answers");
    char exp[64] = "";
    if (ex) utf16le_to_utf8(ex->filesystem.val, ex->filesystem.len, exp, sizeof exp);
    CHECK(strcmp(exp, "/C/") == 0, "the export is /C/");
    mount_export_free(ex);

    fhstatus_t fhs;
    uint8_t path[32]; uint32_t plen = 0;
    dirpath_t dp = { path, 0 };
    utf8_to_utf16le("/C/", path, sizeof path, &plen); dp.len = plen;
    CHECK(mount_mnt(&mc, &dp, &fhs) == 0 && fhs.status == 0, "MNT /C/ succeeds");
    fhandle_t rootfh = fhs.directory;
    utf8_to_utf16le("/c", path, sizeof path, &plen); dp.len = plen;
    fhstatus_t fhs2;
    CHECK(mount_mnt(&mc, &dp, &fhs2) == 0 && fhs2.status == 0, "MNT /c (no case, no slash) succeeds");
    utf8_to_utf16le("/X/", path, sizeof path, &plen); dp.len = plen;
    CHECK(mount_mnt(&mc, &dp, &fhs2) == 0 && fhs2.status != 0, "MNT /X/ is refused");
    CHECK(strstr(last_mount_line, "refused") != NULL, "the refusal is logged");

    /* lookups, the way the players do them: from the root, other case */
    fhandle_t f1, f2, f3, f4, dummy;
    fattr_t a;
    CHECK(lookup(&nc, &rootfh, "Contents", &f1, &a) == 0 && a.type == NFDIR, "LOOKUP Contents");
    CHECK(lookup(&nc, &f1, "intapol", &f2, &a) == 0 && a.type == NFDIR, "LOOKUP intapol finds Intapol");
    CHECK(lookup(&nc, &f2, "uk dembow", &f3, &a) == 0, "LOOKUP uk dembow finds UK Dembow");
    CHECK(lookup(&nc, &f3, "TRACK-02.MP3", &f4, &a) == 0 && a.type == NFREG && a.size == FILE_SIZE, "LOOKUP the file, size right");
    CHECK(lookup(&nc, &f3, "nope.mp3", &dummy, NULL) != 0, "LOOKUP of a missing name fails");
    CHECK(lookup(&nc, &f1, "escape", &dummy, NULL) != 0, "a symlink is not followed");
    CHECK(lookup(&nc, &rootfh, "a/b", &dummy, NULL) != 0, "a name with a slash is refused");
    CHECK(lookup(&nc, &rootfh, "..", &dummy, &a) == 0 && a.fileid == 1, ".. at the root stays at the root");

    /* reads: the pattern of the players, the tag at the end first, then the start, then the rest */
    readargs_t ra;
    readres_t rr;
    int offs[] = { FILE_SIZE - 160, 0, 2048, 8192, 99000, 65536 };
    int cnts[] = { 160, 2048, 2048, 8192, 8192, 8192 };
    for (int i = 0; i < 6; i++) {
        memset(&rr, 0, sizeof rr);
        ra.file = f4; ra.offset = (uint32_t)offs[i]; ra.count = (uint32_t)cnts[i]; ra.totalcount = 0;
        CHECK(nfs_read(&nc, &ra, &rr) == 0 && rr.status == 0, "READ succeeds");
        uint32_t want = (uint32_t)(offs[i] + cnts[i] > FILE_SIZE ? FILE_SIZE - offs[i] : cnts[i]);
        CHECK(rr.data_len == want, "READ returns the right length");
        int bad = 0;
        for (uint32_t k = 0; k < rr.data_len; k++) if (rr.data[k] != expect_byte(offs[i] + (int)k)) bad++;
        CHECK(bad == 0, "READ returns the right bytes");
        nfs_readres_free(&rr);
    }
    memset(&rr, 0, sizeof rr);
    ra.file = f4; ra.offset = FILE_SIZE + 10; ra.count = 100;
    CHECK(nfs_read(&nc, &ra, &rr) == 0 && rr.status == 0 && rr.data_len == 0, "READ past the end is empty");
    nfs_readres_free(&rr);

    /* directory listing */
    readdirres_t dr;
    uint8_t cookie[COOKIESIZE] = { 0, 0, 0, 0 };
    memset(&dr, 0, sizeof dr);
    CHECK(nfs_readdir(&nc, &f3, cookie, 4096, &dr) == 0 && dr.status == 0 && dr.entries && dr.eof, "READDIR lists the folder");
    char first[64] = "";
    if (dr.entries) utf16le_to_utf8(dr.entries->name.val, dr.entries->name.len, first, sizeof first);
    CHECK(strcmp(first, "Track-02.mp3") == 0, "READDIR names the file");
    nfs_readdirres_free(&dr);

    /* a deck reading with several sockets at once */
    rpc_client_t extra[6];
    for (int i = 0; i < 6; i++) {
        CHECK(rpc_client_init(&extra[i], "127.0.0.1", NF_PORT, 100003, 2, 2000) == 0, "another client socket");
        memset(&rr, 0, sizeof rr);
        ra.file = f4; ra.offset = (uint32_t)(i * 8192); ra.count = 8192;
        CHECK(nfs_read(&extra[i], &ra, &rr) == 0 && rr.data_len == 8192, "parallel client READ");
        nfs_readres_free(&rr);
        rpc_client_destroy(&extra[i]);
    }

    CHECK(nfsd_reads(d) >= 12 && nfsd_bytes_read(d) > 50000, "the service counts what it served");
    CHECK(logged > 10, "the service logs what the players ask");

    rpc_client_destroy(&mc);
    rpc_client_destroy(&nc);
    nfsd_stop(d);

    /* the ways mountd EXPORT can be answered (what a deck does with each one is found out on the deck) */
    for (int style = 1; style <= 3; style++) {
        cfg.export_style = style;
        nfsd_t *d2 = nfsd_start(&cfg, err, sizeof err);
        CHECK(d2 != NULL, "restart with another EXPORT style");
        if (!d2) continue;
        rpc_client_t c2;
        export_entry_t *list = NULL;
        CHECK(rpc_client_init(&c2, "127.0.0.1", MT_PORT, 100005, 1, 2000) == 0, "mount client again");
        int rc = mount_export(&c2, &list);
        if (style == 1) CHECK(rc == 0 && list && list->filesystem.len == 6, "style 1: the list is still decoded (group skipped)");
        if (style == 2) CHECK(rc == 0 && list == NULL, "style 2: an empty list");
        if (style == 3) CHECK(rc == RPC_PROC_UNAVAIL, "style 3: procedure unavailable");
        mount_export_free(list);
        rpc_client_destroy(&c2);
        nfsd_stop(d2);
    }

    char cmd[600];
    snprintf(cmd, sizeof cmd, "rm -rf '%s'", root);
    if (system(cmd)) {}
    if (failures) { fprintf(stderr, "%d check(s) failed\n", failures); return 1; }
    printf("[pass] nfsd_test\n");
    return 0;
}
