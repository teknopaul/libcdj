#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* NFS layer headers — not in the src/c include path by default */
#include "../nfs/nfs_server.h"
#include "../nfs/nfs_client.h"
#include "../nfs/nfs_types.h"
#include "../nfs/utf16.h"
#include "../nfs/fake_pdb.h"
#include "../nfs/mount_client.h"
#include "../nfs/portmap_client.h"

#include "vdj_nfs.h"

/* Pioneer-style virtual filesystem constants (same as nfs_server_test) */
#define VDJ_NFS_FSID      0x00000001U
#define INO_ROOT    1
#define INO_PIONEER 2
#define INO_RB      3
#define INO_PDB     4

struct vdj_nfs_s {
    nfs_server_t  server;
    fake_track_t  track;
    uint8_t      *pdb_data;
    uint32_t      pdb_len;
    /* Storage for fake_track_t string fields */
    char          title_buf[256];
    char          filename_buf[256];
    char          filepath_buf[256];
};

/* ---- VFS ops ---- */

static int op_list_exports(void *ctx, export_entry_t **list_out) {
    (void)ctx;
    export_entry_t *e = calloc(1, sizeof(*e));
    if (!e) return -1;
    uint8_t *u16 = malloc(6);
    if (!u16) { free(e); return -1; }
    uint32_t u16len = 0;
    utf8_to_utf16le("/C/", u16, 6, &u16len);
    e->filesystem.val = u16;
    e->filesystem.len = u16len;
    *list_out = e;
    return 0;
}

static int op_mount(void *ctx, dirpath_t *path, fhandle_t *fh_out) {
    (void)ctx;
    char ascii[MNTPATHLEN + 1];
    utf16le_to_utf8(path->val, path->len, ascii, sizeof(ascii));
    if (strcmp(ascii, "/C/") != 0) return -1;
    nfs_fh_make(fh_out, VDJ_NFS_FSID, INO_ROOT);
    return 0;
}

static int op_getattr(void *ctx, fhandle_t *fh, fattr_t *attrs_out) {
    struct vdj_nfs_s *n = (struct vdj_nfs_s *)ctx;
    uint32_t inode;
    if (nfs_fh_parse(fh, NULL, &inode) < 0) return -1;
    memset(attrs_out, 0, sizeof(*attrs_out));
    switch (inode) {
    case INO_ROOT: case INO_PIONEER: case INO_RB:
        attrs_out->type = NFDIR; attrs_out->mode = 0040755;
        attrs_out->nlink = 2; attrs_out->fsid = VDJ_NFS_FSID;
        attrs_out->fileid = inode; attrs_out->blocksize = 4096;
        return 0;
    case INO_PDB:
        attrs_out->type = NFREG; attrs_out->mode = 0100444;
        attrs_out->nlink = 1; attrs_out->fsid = VDJ_NFS_FSID;
        attrs_out->fileid = inode; attrs_out->size = n->pdb_len;
        attrs_out->blocksize = 4096;
        attrs_out->blocks = (n->pdb_len + 511) / 512;
        return 0;
    }
    return -1;
}

static int name_eq(filename_t *fn, const char *ascii) {
    uint8_t u16[512]; uint32_t u16len = 0;
    if (utf8_to_utf16le(ascii, u16, sizeof(u16), &u16len) < 0) return 0;
    return fn->len == u16len && memcmp(fn->val, u16, u16len) == 0;
}

static int op_lookup(void *ctx, fhandle_t *dir, filename_t *name,
                     fhandle_t *fh_out, fattr_t *attrs_out) {
    struct vdj_nfs_s *n = (struct vdj_nfs_s *)ctx;
    uint32_t inode;
    if (nfs_fh_parse(dir, NULL, &inode) < 0) return -1;
    uint32_t child_inode = 0;
    if      (inode == INO_ROOT    && name_eq(name, "PIONEER"))    child_inode = INO_PIONEER;
    else if (inode == INO_PIONEER && name_eq(name, "rekordbox"))  child_inode = INO_RB;
    else if (inode == INO_RB      && name_eq(name, "export.pdb")) child_inode = INO_PDB;
    else return -1;
    nfs_fh_make(fh_out, VDJ_NFS_FSID, child_inode);
    fhandle_t tmp = *fh_out;
    return op_getattr(n, &tmp, attrs_out);
}

static int op_readdir(void *ctx, fhandle_t *dir, uint32_t cookie,
                      uint32_t max_bytes, dir_entry_t **entries_out, int *eof_out) {
    (void)ctx; (void)max_bytes;
    uint32_t inode;
    if (nfs_fh_parse(dir, NULL, &inode) < 0) return -1;
    if (cookie > 0) { *entries_out = NULL; *eof_out = 1; return 0; }
    const char *cn = NULL; uint32_t ci = 0;
    switch (inode) {
    case INO_ROOT:    cn = "PIONEER";    ci = INO_PIONEER; break;
    case INO_PIONEER: cn = "rekordbox";  ci = INO_RB;      break;
    case INO_RB:      cn = "export.pdb"; ci = INO_PDB;     break;
    default: *entries_out = NULL; *eof_out = 1; return 0;
    }
    dir_entry_t *e = calloc(1, sizeof(*e));
    if (!e) return -1;
    e->fileid = ci;
    uint8_t u16[512]; uint32_t u16len = 0;
    utf8_to_utf16le(cn, u16, sizeof(u16), &u16len);
    e->name.val = malloc(u16len);
    if (!e->name.val) { free(e); return -1; }
    memcpy(e->name.val, u16, u16len);
    e->name.len = u16len;
    *entries_out = e; *eof_out = 1;
    return 0;
}

static int op_read(void *ctx, fhandle_t *fh, uint32_t offset, uint32_t count,
                   uint8_t *buf_out, uint32_t *bytes_out, fattr_t *attrs_out) {
    struct vdj_nfs_s *n = (struct vdj_nfs_s *)ctx;
    uint32_t inode;
    if (nfs_fh_parse(fh, NULL, &inode) < 0) return -1;
    if (inode != INO_PDB) return -1;
    fhandle_t tmp = *fh;
    if (op_getattr(n, &tmp, attrs_out) < 0) return -1;
    if (offset >= n->pdb_len) { *bytes_out = 0; return 0; }
    uint32_t avail = n->pdb_len - offset;
    uint32_t nb    = (count < avail) ? count : avail;
    memcpy(buf_out, n->pdb_data + offset, nb);
    *bytes_out = nb;
    return 0;
}

static int op_statfs(void *ctx, fhandle_t *fh,
                     uint32_t *tsize, uint32_t *bsize,
                     uint32_t *blocks, uint32_t *bfree, uint32_t *bavail) {
    struct vdj_nfs_s *n = (struct vdj_nfs_s *)ctx;
    (void)fh;
    *tsize = 4096; *bsize = 4096;
    *blocks = (n->pdb_len + 4095) / 4096;
    *bfree = 0; *bavail = 0;
    return 0;
}

static nfs_ops_t vdj_nfs_ops = {
    .list_exports = op_list_exports,
    .mount        = op_mount,
    .getattr      = op_getattr,
    .lookup       = op_lookup,
    .readdir      = op_readdir,
    .read         = op_read,
    .statfs       = op_statfs,
};

/* ---- Public API ---- */

int vdj_nfs_init(vdj_t *v, uint16_t mount_port, uint16_t nfs_port) {
    if (nfs_port == 2049) {
        /* Check effective UID — port 2049 needs root */
        fprintf(stderr, "vdj_nfs: NFS port 2049 requires root — ensure running as root\n");
    }

    struct vdj_nfs_s *n = calloc(1, sizeof(*n));
    if (!n) return -1;

    /* Default track */
    snprintf(n->title_buf,    sizeof(n->title_buf),    "libcdj virtual track");
    snprintf(n->filename_buf, sizeof(n->filename_buf),  "virtual.mp3");
    snprintf(n->filepath_buf, sizeof(n->filepath_buf),  "/C/virtual.mp3");
    n->track.title         = n->title_buf;
    n->track.filename      = n->filename_buf;
    n->track.file_path     = n->filepath_buf;
    n->track.duration_secs = 0;
    n->track.bpm_x100      = 0;

    n->pdb_data = fake_pdb_generate(&n->track, &n->pdb_len);
    if (!n->pdb_data) { free(n); return -1; }

    if (nfs_server_init(&n->server, mount_port, nfs_port, &vdj_nfs_ops, n) < 0) {
        free(n->pdb_data);
        free(n);
        return -1;
    }

    /* Register with system portmapper so XDJs can discover us via rpcinfo / portmap. */
    if (portmap_set("127.0.0.1", MOUNT_PROG, MOUNT_VERS, mount_port) == 0)
        fprintf(stderr, "vdj_nfs: registered mountd port %u with portmapper\n", mount_port);
    else
        fprintf(stderr, "vdj_nfs: portmapper registration failed (not root?)\n");

    if (portmap_set("127.0.0.1", NFS_PROG, NFS_VERS, nfs_port) == 0)
        fprintf(stderr, "vdj_nfs: registered NFS port %u with portmapper\n", nfs_port);
    else
        fprintf(stderr, "vdj_nfs: NFS portmapper registration failed\n");

    v->client = n;
    fprintf(stderr, "vdj_nfs: listening on mount=%u nfs=%u\n", mount_port, nfs_port);
    return 0;
}

int vdj_nfs_poll(vdj_t *v, int timeout_ms) {
    struct vdj_nfs_s *n = (struct vdj_nfs_s *)v->client;
    if (!n) return -1;
    return nfs_server_poll(&n->server, timeout_ms);
}

void vdj_nfs_update_track(vdj_t *v, const void *track_ptr) {
    struct vdj_nfs_s *n = (struct vdj_nfs_s *)v->client;
    if (!n) return;
    const fake_track_t *t = (const fake_track_t *)track_ptr;

    /* Copy strings into internal buffers */
    if (t->title)     snprintf(n->title_buf,    sizeof(n->title_buf),    "%s", t->title);
    if (t->filename)  snprintf(n->filename_buf, sizeof(n->filename_buf),  "%s", t->filename);
    if (t->file_path) snprintf(n->filepath_buf, sizeof(n->filepath_buf),  "%s", t->file_path);
    n->track.title         = n->title_buf;
    n->track.filename      = n->filename_buf;
    n->track.file_path     = n->filepath_buf;
    n->track.duration_secs = t->duration_secs;
    n->track.bpm_x100      = t->bpm_x100;

    /* Regenerate PDB — generate first so old data is preserved on alloc failure */
    uint32_t new_len = 0;
    uint8_t *new_pdb = fake_pdb_generate(&n->track, &new_len);
    if (new_pdb) {
        free(n->pdb_data);
        n->pdb_data = new_pdb;
        n->pdb_len  = new_len;
    }
}

void vdj_nfs_destroy(vdj_t *v) {
    struct vdj_nfs_s *n = (struct vdj_nfs_s *)v->client;
    if (!n) return;
    portmap_unset("127.0.0.1", MOUNT_PROG, MOUNT_VERS);
    portmap_unset("127.0.0.1", NFS_PROG, NFS_VERS);
    nfs_server_destroy(&n->server);
    free(n->pdb_data);
    free(n);
    v->client = NULL;
}
