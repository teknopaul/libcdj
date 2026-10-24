#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>

#undef MAXNAMLEN                       /* glibc <dirent.h> defines its own; the NFS headers define 255 */
#include "nfs_dirfs.h"
#include "utf16.h"

#define PBUF (PATH_MAX * 2 + 1024)
#define DIRFS_FSID   0x0d1f0001U
#define ROOT_INODE   1U
#define FD_CACHE     8

struct nfs_dirfs {
    int       export_style;
    char     *root;
    char    **exports;
    int       nexports;
    char    **rel;            /* inode -> path relative to the root ("" = root); index = inode, 0 unused */
    uint32_t *hash;
    uint32_t  ninodes, capinodes;
    struct { uint32_t inode; int fd; uint64_t used; } fds[FD_CACHE];
    uint64_t  tick, bytes_read, reads;
};

static uint32_t hash_str(const char *s) { uint32_t h = 2166136261u; for (; *s; s++) h = (h ^ (uint8_t)*s) * 16777619u; return h; }

/* ---------- inode table ---------- */

static uint32_t inode_for(nfs_dirfs_t *fs, const char *rel)
{
    uint32_t h = hash_str(rel);
    for (uint32_t i = 1; i < fs->ninodes; i++)
        if (fs->hash[i] == h && strcmp(fs->rel[i], rel) == 0) return i;
    if (fs->ninodes >= fs->capinodes) {
        uint32_t cap = fs->capinodes ? fs->capinodes * 2 : 256;
        char **r = realloc(fs->rel, cap * sizeof(char *));
        if (!r) return 0;
        fs->rel = r;
        uint32_t *hh = realloc(fs->hash, cap * sizeof(uint32_t));
        if (!hh) return 0;
        fs->hash = hh;
        fs->capinodes = cap;
    }
    char *copy = strdup(rel);
    if (!copy) return 0;
    fs->rel[fs->ninodes] = copy;
    fs->hash[fs->ninodes] = h;
    return fs->ninodes++;
}

static int fh_inode(nfs_dirfs_t *fs, const fhandle_t *fh, uint32_t *inode)
{
    uint32_t fsid, ino;
    if (nfs_fh_parse(fh, &fsid, &ino) < 0 || fsid != DIRFS_FSID || ino == 0 || ino >= fs->ninodes) return -1;
    *inode = ino;
    return 0;
}

static void abs_path(nfs_dirfs_t *fs, uint32_t inode, char *out, size_t n)
{
    if (fs->rel[inode][0]) snprintf(out, n, "%s/%s", fs->root, fs->rel[inode]);
    else snprintf(out, n, "%s", fs->root);
}

/* ---------- attributes ---------- */

static int attrs_of(nfs_dirfs_t *fs, uint32_t inode, fattr_t *a)
{
    char p[PBUF];
    struct stat st;
    abs_path(fs, inode, p, sizeof p);
    if (lstat(p, &st) < 0) return -1;
    memset(a, 0, sizeof *a);
    if (S_ISDIR(st.st_mode)) { a->type = NFDIR; a->mode = 0040555; a->nlink = 2; }
    else if (S_ISREG(st.st_mode)) { a->type = NFREG; a->mode = 0100444; a->nlink = 1; }
    else return -1;                                  /* symlinks, devices, sockets: not served */
    uint64_t sz = (uint64_t)st.st_size;
    a->size = sz > 0xffffffffULL ? 0xffffffffU : (uint32_t)sz;
    a->blocksize = 4096;
    a->blocks = (uint32_t)((sz + 511) / 512);
    a->fsid = DIRFS_FSID;
    a->fileid = inode;
    a->atime_sec = (uint32_t)st.st_atime; a->mtime_sec = (uint32_t)st.st_mtime; a->ctime_sec = (uint32_t)st.st_ctime;
    return 0;
}

/* ---------- ops ---------- */

static uint8_t *utf8_to_u16(const char *s, uint32_t *len)
{
    size_t n = strlen(s);
    uint8_t *buf = malloc(n * 2 + 2);
    if (!buf) return NULL;
    uint32_t o = 0;
    for (size_t i = 0; i < n;) {
        uint32_t c = (uint8_t)s[i];
        int extra = 0;
        if (c >= 0xf0) { c &= 0x07; extra = 3; } else if (c >= 0xe0) { c &= 0x0f; extra = 2; } else if (c >= 0xc0) { c &= 0x1f; extra = 1; }
        i++;
        while (extra-- > 0 && i < n) c = (c << 6) | ((uint8_t)s[i++] & 0x3f);
        if (c >= 0x10000) {
            c -= 0x10000;
            uint32_t hi = 0xd800 + (c >> 10), lo = 0xdc00 + (c & 0x3ff);
            buf[o++] = hi & 0xff; buf[o++] = hi >> 8; buf[o++] = lo & 0xff; buf[o++] = lo >> 8;
            buf = realloc(buf, n * 2 + 2 + o);   /* a 4-byte UTF-8 sequence is 4 bytes of UTF-16, never more: safe */
        } else {
            buf[o++] = c & 0xff; buf[o++] = c >> 8;
        }
    }
    *len = o;
    return buf;
}

static int decode_name(const uint8_t *val, uint32_t len, char *out, size_t n)
{
    if (len > 2 * MAXNAMLEN || n < (size_t)len * 3 / 2 + 1) return -1;
    if (utf16le_decode(val, len, out, (uint32_t)n) < 0) return -1;
    return 0;
}

static int same_export(const char *a, const char *b)
{
    size_t la = strlen(a), lb = strlen(b);
    while (la > 1 && a[la - 1] == '/') la--;
    while (lb > 1 && b[lb - 1] == '/') lb--;
    return la == lb && strncasecmp(a, b, la) == 0;
}

static int op_list_exports(void *ctx, export_entry_t **list_out)
{
    nfs_dirfs_t *fs = ctx;
    export_entry_t *head = NULL, **tail = &head;
    if (fs->export_style == 3) return -2;                 /* the mount server answers "procedure unavailable" */
    if (fs->export_style == 2) { *list_out = NULL; return 0; }
    for (int i = 0; i < fs->nexports; i++) {
        export_entry_t *e = calloc(1, sizeof *e);
        if (!e) break;
        e->filesystem.val = utf8_to_u16(fs->exports[i], &e->filesystem.len);
        if (!e->filesystem.val) { free(e); break; }
        if (fs->export_style == 1) e->group.val = utf8_to_u16("*", &e->group.len);
        *tail = e; tail = &e->next;
    }
    *list_out = head;
    return 0;
}

static int op_mount(void *ctx, dirpath_t *path, fhandle_t *fh_out)
{
    nfs_dirfs_t *fs = ctx;
    char p[MNTPATHLEN * 3];
    if (decode_name(path->val, path->len, p, sizeof p) < 0) return -1;
    for (int i = 0; i < fs->nexports; i++)
        if (same_export(p, fs->exports[i])) { nfs_fh_make(fh_out, DIRFS_FSID, ROOT_INODE); return 0; }
    return -1;
}

static int op_getattr(void *ctx, fhandle_t *fh, fattr_t *attrs_out)
{
    nfs_dirfs_t *fs = ctx;
    uint32_t ino;
    if (fh_inode(fs, fh, &ino) < 0) return -1;
    return attrs_of(fs, ino, attrs_out);
}

static void parent_of(const char *rel, char *out, size_t n)
{
    snprintf(out, n, "%s", rel);
    char *slash = strrchr(out, '/');
    if (slash) *slash = 0; else out[0] = 0;
}

static int op_lookup(void *ctx, fhandle_t *dir, filename_t *name, fhandle_t *fh_out, fattr_t *attrs_out)
{
    nfs_dirfs_t *fs = ctx;
    uint32_t dino;
    char nm[MAXNAMLEN * 3 + 1], dpath[PBUF], rel[PBUF];
    if (fh_inode(fs, dir, &dino) < 0) return -1;
    if (decode_name(name->val, name->len, nm, sizeof nm) < 0 || nm[0] == 0 || strchr(nm, '/')) return -1;

    uint32_t ino;
    if (strcmp(nm, ".") == 0) {
        ino = dino;
    } else if (strcmp(nm, "..") == 0) {
        parent_of(fs->rel[dino], rel, sizeof rel);
        ino = inode_for(fs, rel);
    } else {
        abs_path(fs, dino, dpath, sizeof dpath);
        const char *base = fs->rel[dino];
        char full[PBUF + 1024], found[MAXNAMLEN * 3 + 1] = "";
        struct stat st;
        snprintf(full, sizeof full, "%s/%s", dpath, nm);
        if (lstat(full, &st) == 0) {
            snprintf(found, sizeof found, "%s", nm);
        } else {                                              /* the players do not match the case of the name on disk */
            DIR *d = opendir(dpath);
            if (!d) return -1;
            struct dirent *de;
            while ((de = readdir(d))) {
                if (strcasecmp(de->d_name, nm) == 0) { snprintf(found, sizeof found, "%s", de->d_name); break; }
            }
            closedir(d);
            if (!found[0]) return -1;
        }
        if (base[0]) snprintf(rel, sizeof rel, "%s/%s", base, found); else snprintf(rel, sizeof rel, "%s", found);
        ino = inode_for(fs, rel);
    }
    if (!ino || attrs_of(fs, ino, attrs_out) < 0) return -1;
    nfs_fh_make(fh_out, DIRFS_FSID, ino);
    return 0;
}

static int cmp_names(const void *a, const void *b) { return strcmp(*(char *const *)a, *(char *const *)b); }

static int op_readdir(void *ctx, fhandle_t *dir, uint32_t cookie, uint32_t max_bytes,
                      dir_entry_t **entries_out, int *eof_out)
{
    nfs_dirfs_t *fs = ctx;
    uint32_t dino;
    char dpath[PBUF];
    if (fh_inode(fs, dir, &dino) < 0) return -1;
    abs_path(fs, dino, dpath, sizeof dpath);
    DIR *d = opendir(dpath);
    if (!d) return -1;
    char **names = NULL;
    size_t n = 0, cap = 0;
    struct dirent *de;
    while ((de = readdir(d))) {
        if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, "..")) continue;
        if (n == cap) { cap = cap ? cap * 2 : 64; char **t = realloc(names, cap * sizeof *t); if (!t) break; names = t; }
        names[n++] = strdup(de->d_name);
    }
    closedir(d);
    if (n) qsort(names, n, sizeof *names, cmp_names);

    dir_entry_t *head = NULL, **tail = &head;
    uint32_t budget = max_bytes ? max_bytes : 4096, used = 16;
    size_t i = cookie;
    for (; i < n; i++) {
        char rel[PBUF];
        if (fs->rel[dino][0]) snprintf(rel, sizeof rel, "%s/%s", fs->rel[dino], names[i]); else snprintf(rel, sizeof rel, "%s", names[i]);
        dir_entry_t *e = calloc(1, sizeof *e);
        if (!e) break;
        e->name.val = utf8_to_u16(names[i], &e->name.len);
        uint32_t need = 24 + ((e->name.len + 3) & ~3u);
        if (!e->name.val || (head && used + need > budget)) { free(e->name.val); free(e); break; }
        e->fileid = inode_for(fs, rel);
        used += need;
        *tail = e; tail = &e->next;
    }
    *eof_out = (i >= n);
    for (size_t k = 0; k < n; k++) free(names[k]);
    free(names);
    *entries_out = head;
    return 0;
}

static int cached_fd(nfs_dirfs_t *fs, uint32_t ino)
{
    int victim = 0;
    for (int i = 0; i < FD_CACHE; i++) {
        if (fs->fds[i].fd > 0 && fs->fds[i].inode == ino) { fs->fds[i].used = ++fs->tick; return fs->fds[i].fd; }
        if (fs->fds[i].used < fs->fds[victim].used) victim = i;
    }
    char p[PBUF];
    abs_path(fs, ino, p, sizeof p);
    int fd = open(p, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    if (fd < 0) return -1;
    if (fs->fds[victim].fd > 0) close(fs->fds[victim].fd);
    fs->fds[victim].fd = fd; fs->fds[victim].inode = ino; fs->fds[victim].used = ++fs->tick;
    return fd;
}

static int op_read(void *ctx, fhandle_t *fh, uint32_t offset, uint32_t count,
                   uint8_t *buf_out, uint32_t *bytes_out, fattr_t *attrs_out)
{
    nfs_dirfs_t *fs = ctx;
    uint32_t ino;
    if (fh_inode(fs, fh, &ino) < 0 || attrs_of(fs, ino, attrs_out) < 0 || attrs_out->type != NFREG) return -1;
    int fd = cached_fd(fs, ino);
    if (fd < 0) return -1;
    ssize_t got = pread(fd, buf_out, count, (off_t)offset);
    if (got < 0) return -1;
    *bytes_out = (uint32_t)got;
    fs->bytes_read += (uint64_t)got;
    fs->reads++;
    return 0;
}

static int op_statfs(void *ctx, fhandle_t *fh, uint32_t *tsize, uint32_t *bsize, uint32_t *blocks, uint32_t *bfree, uint32_t *bavail)
{
    nfs_dirfs_t *fs = ctx;
    struct statvfs sv;
    (void)fh;
    *tsize = MAXDATA; *bsize = 4096; *blocks = 0; *bfree = 0; *bavail = 0;      /* read-only: no free space */
    if (statvfs(fs->root, &sv) == 0 && sv.f_frsize) {
        uint64_t b = (uint64_t)sv.f_blocks * sv.f_frsize / 4096;
        *blocks = b > 0xffffffffULL ? 0xffffffffU : (uint32_t)b;
    }
    return 0;
}

static nfs_ops_t dirfs_ops = {
    .list_exports = op_list_exports,
    .mount        = op_mount,
    .getattr      = op_getattr,
    .lookup       = op_lookup,
    .readdir      = op_readdir,
    .read         = op_read,
    .statfs       = op_statfs,
};

nfs_ops_t *nfs_dirfs_ops(void) { return &dirfs_ops; }

nfs_dirfs_t *nfs_dirfs_open(const char *root, const char *const *exports)
{
    struct stat st;
    char rp[PATH_MAX];
    if (!root || !realpath(root, rp) || stat(rp, &st) < 0 || !S_ISDIR(st.st_mode)) return NULL;
    nfs_dirfs_t *fs = calloc(1, sizeof *fs);
    if (!fs) return NULL;
    fs->root = strdup(rp);
    static const char *const def[] = { "/C/", NULL };
    for (const char *const *e = exports ? exports : def; *e; e++) fs->nexports++;
    fs->exports = calloc((size_t)fs->nexports, sizeof(char *));
    int k = 0;
    for (const char *const *e = exports ? exports : def; *e; e++) fs->exports[k++] = strdup(*e);
    fs->ninodes = 1;                                  /* inode 0 is unused */
    if (!fs->root || !fs->exports || inode_for(fs, "") != ROOT_INODE) { nfs_dirfs_close(fs); return NULL; }
    return fs;
}

void nfs_dirfs_close(nfs_dirfs_t *fs)
{
    if (!fs) return;
    for (int i = 0; i < FD_CACHE; i++) if (fs->fds[i].fd > 0) close(fs->fds[i].fd);
    for (uint32_t i = 1; i < fs->ninodes; i++) free(fs->rel[i]);
    for (int i = 0; i < fs->nexports; i++) free(fs->exports ? fs->exports[i] : NULL);
    free(fs->rel); free(fs->hash); free(fs->exports); free(fs->root); free(fs);
}

void nfs_dirfs_set_export_style(nfs_dirfs_t *fs, int style) { fs->export_style = style; }

int nfs_dirfs_path(nfs_dirfs_t *fs, const fhandle_t *fh, char *out, size_t n)
{
    uint32_t ino;
    if (fh_inode(fs, fh, &ino) < 0) { snprintf(out, n, "(stale handle)"); return -1; }
    snprintf(out, n, "/%s", fs->rel[ino]);
    return 0;
}

uint64_t nfs_dirfs_bytes_read(const nfs_dirfs_t *fs) { return fs->bytes_read; }
uint64_t nfs_dirfs_reads(const nfs_dirfs_t *fs) { return fs->reads; }
