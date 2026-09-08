#include <stdlib.h>
#include <string.h>
#include "nfs_client.h"
#include "portmap_client.h"
#include "utf16.h"

/* --- XDR codecs for NFS-specific types --- */

static int xdr_fattr(xdr_t *x, fattr_t *a) {
    if (xdr_uint32(x, &a->type)       < 0) return -1;
    if (xdr_uint32(x, &a->mode)       < 0) return -1;
    if (xdr_uint32(x, &a->nlink)      < 0) return -1;
    if (xdr_uint32(x, &a->uid)        < 0) return -1;
    if (xdr_uint32(x, &a->gid)        < 0) return -1;
    if (xdr_uint32(x, &a->size)       < 0) return -1;
    if (xdr_uint32(x, &a->blocksize)  < 0) return -1;
    if (xdr_uint32(x, &a->rdev)       < 0) return -1;
    if (xdr_uint32(x, &a->blocks)     < 0) return -1;
    if (xdr_uint32(x, &a->fsid)       < 0) return -1;
    if (xdr_uint32(x, &a->fileid)     < 0) return -1;
    if (xdr_uint32(x, &a->atime_sec)  < 0) return -1;
    if (xdr_uint32(x, &a->atime_usec) < 0) return -1;
    if (xdr_uint32(x, &a->mtime_sec)  < 0) return -1;
    if (xdr_uint32(x, &a->mtime_usec) < 0) return -1;
    if (xdr_uint32(x, &a->ctime_sec)  < 0) return -1;
    if (xdr_uint32(x, &a->ctime_usec) < 0) return -1;
    return 0;
}

static int xdr_filename(xdr_t *x, filename_t *fn) {
    return xdr_varbytes(x, &fn->val, &fn->len, MAXNAMLEN);
}

static int xdr_diropargs(xdr_t *x, diropargs_t *a) {
    if (xdr_fhandle(x, &a->dir)     < 0) return -1;
    if (xdr_filename(x, &a->name)   < 0) return -1;
    return 0;
}

static int xdr_diropres(xdr_t *x, diropres_t *r) {
    if (xdr_uint32(x, &r->status) < 0) return -1;
    if (r->status != NFS_OK) return 0;
    if (xdr_fhandle(x, &r->file)          < 0) return -1;
    if (xdr_fattr(x, &r->attributes)      < 0) return -1;
    return 0;
}

/* --- NFS procedures --- */

int nfs_getport(const char *host, uint16_t *port_out) {
    return portmap_getport(host, NFS_PROG, NFS_VERS, port_out);
}

int nfs_null(rpc_client_t *c) {
    return rpc_call(c, NFSPROC_NULL, NULL, NULL, NULL, NULL);
}

static int encode_fhandle_arg(xdr_t *x, void *arg) {
    return xdr_fhandle(x, (fhandle_t *)arg);
}

static int decode_fattr_result(xdr_t *x, void *res) {
    uint32_t status;
    if (xdr_uint32(x, &status) < 0) return -1;
    if (status != NFS_OK) return (int)status;
    return xdr_fattr(x, (fattr_t *)res);
}

int nfs_getattr(rpc_client_t *c, fhandle_t *fh, fattr_t *attrs_out) {
    memset(attrs_out, 0, sizeof(*attrs_out));
    int rc = rpc_call(c, NFSPROC_GETATTR, encode_fhandle_arg, fh,
                      decode_fattr_result, attrs_out);
    return rc;
}

static int encode_diropargs(xdr_t *x, void *arg) {
    return xdr_diropargs(x, (diropargs_t *)arg);
}

static int decode_diropres(xdr_t *x, void *res) {
    return xdr_diropres(x, (diropres_t *)res);
}

int nfs_lookup(rpc_client_t *c, diropargs_t *args, diropres_t *res_out) {
    memset(res_out, 0, sizeof(*res_out));
    return rpc_call(c, NFSPROC_LOOKUP, encode_diropargs, args,
                    decode_diropres, res_out);
}

/* READDIR args: fhandle + cookie[4] + count */
typedef struct {
    fhandle_t *dir;
    uint8_t   *cookie;
    uint32_t   count;
} readdir_args_t;

static int encode_readdir_args(xdr_t *x, void *arg) {
    readdir_args_t *a = (readdir_args_t *)arg;
    if (xdr_fhandle(x, a->dir) < 0) return -1;
    if (xdr_opaque(x, a->cookie, COOKIESIZE) < 0) return -1;
    if (xdr_uint32(x, &a->count) < 0) return -1;
    return 0;
}

static int decode_readdir_result(xdr_t *x, void *res) {
    readdirres_t *r = (readdirres_t *)res;
    if (xdr_uint32(x, &r->status) < 0) return -1;
    if (r->status != NFS_OK) return 0;

    dir_entry_t  *head = NULL;
    dir_entry_t **tail = &head;
    uint32_t      present;

    while (xdr_uint32(x, &present) == 0 && present) {
        dir_entry_t *e = calloc(1, sizeof(*e));
        if (!e) goto fail;

        if (xdr_uint32(x, &e->fileid) < 0) { free(e); goto fail; }

        uint8_t  *nval;
        uint32_t  nlen;
        if (xdr_varbytes(x, &nval, &nlen, MAXNAMLEN) < 0) { free(e); goto fail; }
        e->name.val = malloc(nlen + 1);
        if (!e->name.val) { free(e); goto fail; }
        memcpy(e->name.val, nval, nlen);
        e->name.len = nlen;

        if (xdr_opaque(x, e->cookie, COOKIESIZE) < 0) { free(e->name.val); free(e); goto fail; }

        *tail = e;
        tail  = &e->next;
    }

    /* eof boolean */
    int eof = 0;
    if (xdr_bool(x, &eof) < 0) goto fail;
    r->entries = head;
    r->eof     = eof;
    return 0;

fail:
    {
        dir_entry_t *cur = head;
        while (cur) {
            dir_entry_t *nx = cur->next;
            free(cur->name.val);
            free(cur);
            cur = nx;
        }
    }
    return -1;
}

int nfs_readdir(rpc_client_t *c, fhandle_t *dir,
                uint8_t cookie[COOKIESIZE], uint32_t count,
                readdirres_t *res_out) {
    readdir_args_t args = { dir, cookie, count };
    memset(res_out, 0, sizeof(*res_out));
    return rpc_call(c, NFSPROC_READDIR, encode_readdir_args, &args,
                    decode_readdir_result, res_out);
}

static int encode_readargs(xdr_t *x, void *arg) {
    readargs_t *a = (readargs_t *)arg;
    if (xdr_fhandle(x, &a->file)      < 0) return -1;
    if (xdr_uint32(x, &a->offset)     < 0) return -1;
    if (xdr_uint32(x, &a->count)      < 0) return -1;
    if (xdr_uint32(x, &a->totalcount) < 0) return -1;
    return 0;
}

static int decode_readres(xdr_t *x, void *res) {
    readres_t *r = (readres_t *)res;
    if (xdr_uint32(x, &r->status) < 0) return -1;
    if (r->status != NFS_OK) return 0;
    if (xdr_fattr(x, &r->attributes) < 0) return -1;

    uint8_t  *data;
    uint32_t  dlen;
    if (xdr_varbytes(x, &data, &dlen, MAXDATA) < 0) return -1;
    r->data = malloc(dlen + 1);
    if (!r->data) return -1;
    memcpy(r->data, data, dlen);
    r->data_len = dlen;
    return 0;
}

int nfs_read(rpc_client_t *c, readargs_t *args, readres_t *res_out) {
    memset(res_out, 0, sizeof(*res_out));
    return rpc_call(c, NFSPROC_READ, encode_readargs, args,
                    decode_readres, res_out);
}

/* STATFS result */
typedef struct {
    uint32_t tsize;
    uint32_t bsize;
    uint32_t blocks;
    uint32_t bfree;
    uint32_t bavail;
} statfsres_t;

static int decode_statfs_result(xdr_t *x, void *res) {
    uint32_t status;
    if (xdr_uint32(x, &status) < 0) return -1;
    if (status != NFS_OK) return (int)status;
    statfsres_t *r = (statfsres_t *)res;
    if (xdr_uint32(x, &r->tsize)  < 0) return -1;
    if (xdr_uint32(x, &r->bsize)  < 0) return -1;
    if (xdr_uint32(x, &r->blocks) < 0) return -1;
    if (xdr_uint32(x, &r->bfree)  < 0) return -1;
    if (xdr_uint32(x, &r->bavail) < 0) return -1;
    return 0;
}

int nfs_statfs(rpc_client_t *c, fhandle_t *fh, uint32_t *tsize_out) {
    statfsres_t r = {0};
    int rc = rpc_call(c, NFSPROC_STATFS, encode_fhandle_arg, fh,
                      decode_statfs_result, &r);
    if (rc == 0 && tsize_out) *tsize_out = r.tsize;
    return rc;
}

/*
 * Walk an ASCII path one component at a time.
 * Each '/' delimited component is encoded to UTF-16LE for LOOKUP.
 */
int nfs_path_lookup(rpc_client_t *c, fhandle_t *root,
                    const char *path,
                    fhandle_t *fh_out, fattr_t *attr_out) {
    fhandle_t  cur = *root;
    fattr_t    attr;
    const char *p  = path;

    while (*p) {
        /* Skip leading slashes. */
        while (*p == '/') p++;
        if (!*p) break;

        /* Extract one path component. */
        const char *end = p;
        while (*end && *end != '/') end++;

        /* Encode component as UTF-16LE. */
        uint8_t  name16[MAXNAMLEN * 2];
        uint32_t name16_len = 0;
        char     component[MAXNAMLEN + 1];
        size_t   clen = (size_t)(end - p);
        if (clen >= sizeof(component)) return -1;
        memcpy(component, p, clen);
        component[clen] = '\0';
        if (utf8_to_utf16le(component, name16, sizeof(name16), &name16_len) < 0)
            return -1;

        diropargs_t args;
        args.dir       = cur;
        args.name.val  = name16;
        args.name.len  = name16_len;

        diropres_t res;
        int rc = nfs_lookup(c, &args, &res);
        if (rc < 0) return -1;
        if (res.status != NFS_OK) return (int)res.status;

        cur  = res.file;
        attr = res.attributes;
        p    = end;
    }

    if (fh_out)   *fh_out   = cur;
    if (attr_out) *attr_out = attr;
    return 0;
}

void nfs_readdirres_free(readdirres_t *res) {
    dir_entry_t *e = res->entries;
    while (e) {
        dir_entry_t *nx = e->next;
        free(e->name.val);
        free(e);
        e = nx;
    }
    res->entries = NULL;
}

void nfs_readres_free(readres_t *res) {
    free(res->data);
    res->data     = NULL;
    res->data_len = 0;
}
