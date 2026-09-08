/*
 * nfs-server-test: Standalone fake-XDJ NFS server serving one export.pdb.
 *
 * Usage: nfs-server-test [--mount-port PORT] [--nfs-port PORT] [--title TITLE]
 *                        [--bpm BPM] [--duration SECS]
 *
 * Verify with: target/xdj-explore 127.0.0.1 --mount-port PORT --nfs-port PORT
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>

#include "nfs_server.h"
#include "nfs_client.h"
#include "nfs_types.h"
#include "utf16.h"
#include "fake_pdb.h"

/* Pioneer-style virtual filesystem:
 *   / (inode 1)
 *     PIONEER/ (inode 2)
 *       rekordbox/ (inode 3)
 *         export.pdb (inode 4)
 */
#define FSID  0x00000001
#define INO_ROOT    1
#define INO_PIONEER 2
#define INO_RB      3
#define INO_PDB     4

static uint8_t *g_pdb_data = NULL;
static uint32_t g_pdb_len  = 0;

/* Helper: make fattr for a directory */
static void make_dir_attr(fattr_t *a, uint32_t inode) {
    memset(a, 0, sizeof(*a));
    a->type      = NFDIR;
    a->mode      = 0755 | 0040000;
    a->nlink     = 2;
    a->fsid      = FSID;
    a->fileid    = inode;
    a->blocksize = 4096;
}

/* Helper: make fattr for a regular file */
static void make_file_attr(fattr_t *a, uint32_t inode, uint32_t size) {
    memset(a, 0, sizeof(*a));
    a->type      = NFREG;
    a->mode      = 0444 | 0100000;
    a->nlink     = 1;
    a->fsid      = FSID;
    a->fileid    = inode;
    a->size      = size;
    a->blocksize = 4096;
    a->blocks    = (size + 511) / 512;
}

/* Compare UTF-16LE filename with ASCII string */
static int name_eq(filename_t *fn, const char *ascii) {
    uint8_t u16[512];
    uint32_t u16len = 0;
    if (utf8_to_utf16le(ascii, u16, sizeof(u16), &u16len) < 0) return 0;
    return fn->len == u16len && memcmp(fn->val, u16, u16len) == 0;
}

/* ops callbacks */

static int op_list_exports(void *ctx, export_entry_t **list_out) {
    (void)ctx;
    export_entry_t *e = calloc(1, sizeof(*e));
    if (!e) return -1;
    /* Export path "/C/" as UTF-16LE */
    uint8_t *u16 = malloc(6);
    if (!u16) { free(e); return -1; }
    uint32_t u16len = 0;
    utf8_to_utf16le("/C/", u16, 6, &u16len);
    e->filesystem.val = u16;
    e->filesystem.len = u16len;
    e->next = NULL;
    *list_out = e;
    return 0;
}

static int op_mount(void *ctx, dirpath_t *path, fhandle_t *fh_out) {
    (void)ctx;
    char ascii[MNTPATHLEN + 1];
    utf16le_to_utf8(path->val, path->len, ascii, sizeof(ascii));
    if (strcmp(ascii, "/C/") != 0) return -1;
    nfs_fh_make(fh_out, FSID, INO_ROOT);
    return 0;
}

static int op_getattr(void *ctx, fhandle_t *fh, fattr_t *attrs_out) {
    (void)ctx;
    uint32_t inode;
    if (nfs_fh_parse(fh, NULL, &inode) < 0) return -1;
    switch (inode) {
    case INO_ROOT:    make_dir_attr(attrs_out, INO_ROOT); return 0;
    case INO_PIONEER: make_dir_attr(attrs_out, INO_PIONEER); return 0;
    case INO_RB:      make_dir_attr(attrs_out, INO_RB); return 0;
    case INO_PDB:     make_file_attr(attrs_out, INO_PDB, g_pdb_len); return 0;
    }
    return -1;
}

static int op_lookup(void *ctx, fhandle_t *dir, filename_t *name,
                     fhandle_t *fh_out, fattr_t *attrs_out) {
    (void)ctx;
    uint32_t inode;
    if (nfs_fh_parse(dir, NULL, &inode) < 0) return -1;

    if (inode == INO_ROOT && name_eq(name, "PIONEER")) {
        nfs_fh_make(fh_out, FSID, INO_PIONEER);
        make_dir_attr(attrs_out, INO_PIONEER);
        return 0;
    }
    if (inode == INO_PIONEER && name_eq(name, "rekordbox")) {
        nfs_fh_make(fh_out, FSID, INO_RB);
        make_dir_attr(attrs_out, INO_RB);
        return 0;
    }
    if (inode == INO_RB && name_eq(name, "export.pdb")) {
        nfs_fh_make(fh_out, FSID, INO_PDB);
        make_file_attr(attrs_out, INO_PDB, g_pdb_len);
        return 0;
    }
    return -1;
}

static int op_readdir(void *ctx, fhandle_t *dir, uint32_t cookie,
                      uint32_t max_bytes, dir_entry_t **entries_out, int *eof_out) {
    (void)ctx; (void)max_bytes;
    uint32_t inode;
    if (nfs_fh_parse(dir, NULL, &inode) < 0) return -1;

    /* Each directory has one child. If cookie > 0, all entries already sent. */
    if (cookie > 0) {
        *entries_out = NULL;
        *eof_out = 1;
        return 0;
    }

    const char *child_name = NULL;
    uint32_t    child_inode = 0;

    switch (inode) {
    case INO_ROOT:    child_name = "PIONEER";    child_inode = INO_PIONEER; break;
    case INO_PIONEER: child_name = "rekordbox";  child_inode = INO_RB;      break;
    case INO_RB:      child_name = "export.pdb"; child_inode = INO_PDB;     break;
    default: *entries_out = NULL; *eof_out = 1; return 0;
    }

    dir_entry_t *e = calloc(1, sizeof(*e));
    if (!e) return -1;
    e->fileid = child_inode;

    uint8_t u16[512];
    uint32_t u16len = 0;
    utf8_to_utf16le(child_name, u16, sizeof(u16), &u16len);
    e->name.val = malloc(u16len);
    if (!e->name.val) { free(e); return -1; }
    memcpy(e->name.val, u16, u16len);
    e->name.len = u16len;

    *entries_out = e;
    *eof_out = 1;
    return 0;
}

static int op_read(void *ctx, fhandle_t *fh, uint32_t offset, uint32_t count,
                   uint8_t *buf_out, uint32_t *bytes_out, fattr_t *attrs_out) {
    (void)ctx;
    uint32_t inode;
    if (nfs_fh_parse(fh, NULL, &inode) < 0) return -1;
    if (inode != INO_PDB) return -1;

    make_file_attr(attrs_out, INO_PDB, g_pdb_len);
    if (offset >= g_pdb_len) { *bytes_out = 0; return 0; }
    uint32_t avail = g_pdb_len - offset;
    uint32_t n     = (count < avail) ? count : avail;
    memcpy(buf_out, g_pdb_data + offset, n);
    *bytes_out = n;
    return 0;
}

static int op_statfs(void *ctx, fhandle_t *fh, uint32_t *tsize_out,
                     uint32_t *bsize_out, uint32_t *blocks_out,
                     uint32_t *bfree_out, uint32_t *bavail_out) {
    (void)ctx; (void)fh;
    *tsize_out  = 4096;
    *bsize_out  = 4096;
    *blocks_out = (g_pdb_len + 4095) / 4096;
    *bfree_out  = 0;
    *bavail_out = 0;
    return 0;
}

static volatile int g_running = 1;
static void on_signal(int sig) { (void)sig; g_running = 0; }

int main(int argc, char *argv[]) {
    uint16_t    mount_port  = 7004;
    uint16_t    nfs_port    = 7005;
    const char *title       = "Fake Track (libcdj)";
    uint32_t    bpm_x100    = 12800;
    uint32_t    duration    = 300;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--mount-port") == 0 && i+1 < argc)
            mount_port = (uint16_t)atoi(argv[++i]);
        else if (strcmp(argv[i], "--nfs-port") == 0 && i+1 < argc)
            nfs_port = (uint16_t)atoi(argv[++i]);
        else if (strcmp(argv[i], "--title") == 0 && i+1 < argc)
            title = argv[++i];
        else if (strcmp(argv[i], "--bpm") == 0 && i+1 < argc)
            bpm_x100 = (uint32_t)(atof(argv[++i]) * 100);
        else if (strcmp(argv[i], "--duration") == 0 && i+1 < argc)
            duration = (uint32_t)atoi(argv[++i]);
    }

    fake_track_t track = {
        .title         = title,
        .filename      = "fake.mp3",
        .file_path     = "/C/fake.mp3",
        .duration_secs = duration,
        .bpm_x100      = bpm_x100,
    };

    g_pdb_data = fake_pdb_generate(&track, &g_pdb_len);
    if (!g_pdb_data) { fprintf(stderr, "fake_pdb_generate failed\n"); return 1; }

    printf("Generated export.pdb: %u bytes\n", g_pdb_len);
    printf("Title: %s\n", title);
    printf("BPM: %.2f\n", bpm_x100 / 100.0);

    nfs_ops_t ops = {
        .list_exports = op_list_exports,
        .mount        = op_mount,
        .getattr      = op_getattr,
        .lookup       = op_lookup,
        .readdir      = op_readdir,
        .read         = op_read,
        .statfs       = op_statfs,
    };

    nfs_server_t s;
    if (nfs_server_init(&s, mount_port, nfs_port, &ops, NULL) < 0) {
        perror("nfs_server_init");
        free(g_pdb_data);
        return 1;
    }

    printf("NFS server ready: mount-port=%u nfs-port=%u\n", mount_port, nfs_port);
    printf("Test with: target/xdj-explore 127.0.0.1 --mount-port %u --nfs-port %u\n",
           mount_port, nfs_port);
    printf("Press Ctrl-C to stop.\n");

    signal(SIGINT,  on_signal);
    signal(SIGTERM, on_signal);

    while (g_running)
        nfs_server_poll(&s, 200);

    nfs_server_destroy(&s);
    free(g_pdb_data);
    return 0;
}
