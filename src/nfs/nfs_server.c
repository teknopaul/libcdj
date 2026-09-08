#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/select.h>

#include "nfs_server.h"
#include "mount_server.h"
#include "xdr.h"

#define SERVER_BUF  65507   /* max UDP payload */

/* ONC-RPC constants */
#define RPC_CALL      0
#define RPC_REPLY     1
#define MSG_ACCEPTED  0

/* RPC accept_stat values */
#define RPC_SUCCESS       0
#define RPC_PROC_UNAVAIL  3

static int open_udp(uint16_t port) {
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) return -1;
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons(port);
    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

int nfs_server_init(nfs_server_t *s, uint16_t mount_port, uint16_t nfs_port,
                    nfs_ops_t *ops, void *ctx) {
    memset(s, 0, sizeof(*s));
    s->ops        = ops;
    s->ctx        = ctx;
    s->mount_port = mount_port;
    s->nfs_port   = nfs_port;
    s->running    = 1;
    s->mount_fd   = open_udp(mount_port);
    if (s->mount_fd < 0) return -1;
    s->nfs_fd = open_udp(nfs_port);
    if (s->nfs_fd < 0) { close(s->mount_fd); s->mount_fd = -1; return -1; }
    return 0;
}

void nfs_server_destroy(nfs_server_t *s) {
    if (s->mount_fd >= 0) { close(s->mount_fd); s->mount_fd = -1; }
    if (s->nfs_fd   >= 0) { close(s->nfs_fd);   s->nfs_fd   = -1; }
    s->running = 0;
}

void nfs_fh_make(fhandle_t *fh, uint32_t fsid, uint32_t inode) {
    memset(fh->data, 0, FHSIZE);
    uint32_t magic = NFS_FH_MAGIC;
    fh->data[0]  = (uint8_t)(magic >> 24);
    fh->data[1]  = (uint8_t)(magic >> 16);
    fh->data[2]  = (uint8_t)(magic >>  8);
    fh->data[3]  = (uint8_t)(magic);
    fh->data[4]  = (uint8_t)(fsid  >> 24);
    fh->data[5]  = (uint8_t)(fsid  >> 16);
    fh->data[6]  = (uint8_t)(fsid  >>  8);
    fh->data[7]  = (uint8_t)(fsid);
    fh->data[8]  = (uint8_t)(inode >> 24);
    fh->data[9]  = (uint8_t)(inode >> 16);
    fh->data[10] = (uint8_t)(inode >>  8);
    fh->data[11] = (uint8_t)(inode);
}

int nfs_fh_parse(const fhandle_t *fh, uint32_t *fsid_out, uint32_t *inode_out) {
    uint32_t magic = ((uint32_t)fh->data[0] << 24) | ((uint32_t)fh->data[1] << 16)
                   | ((uint32_t)fh->data[2] << 8)  |  (uint32_t)fh->data[3];
    if (magic != NFS_FH_MAGIC) return -1;
    if (fsid_out)
        *fsid_out  = ((uint32_t)fh->data[4] << 24) | ((uint32_t)fh->data[5] << 16)
                   | ((uint32_t)fh->data[6] << 8)  |  (uint32_t)fh->data[7];
    if (inode_out)
        *inode_out = ((uint32_t)fh->data[8] << 24) | ((uint32_t)fh->data[9] << 16)
                   | ((uint32_t)fh->data[10] << 8) |  (uint32_t)fh->data[11];
    return 0;
}

/* --- RPC call header decode --- */

typedef struct {
    uint32_t xid;
    uint32_t prog;
    uint32_t vers;
    uint32_t proc;
} rpc_call_hdr_t;

static int decode_call_header(xdr_t *x, rpc_call_hdr_t *hdr) {
    uint32_t msg_type, rpcvers, cred_flavor, cred_len, verf_flavor, verf_len;
    if (xdr_uint32(x, &hdr->xid)  < 0) return -1;
    if (xdr_uint32(x, &msg_type)  < 0) return -1;
    if (msg_type != RPC_CALL)          return -1;
    if (xdr_uint32(x, &rpcvers)   < 0) return -1;
    if (xdr_uint32(x, &hdr->prog) < 0) return -1;
    if (xdr_uint32(x, &hdr->vers) < 0) return -1;
    if (xdr_uint32(x, &hdr->proc) < 0) return -1;
    /* skip credentials */
    if (xdr_uint32(x, &cred_flavor) < 0) return -1;
    if (xdr_uint32(x, &cred_len)    < 0) return -1;
    if (cred_len > 0) {
        if (cred_len > x->len - x->pos) return -1;   /* check before pad to avoid overflow */
        uint32_t pad = (4 - (cred_len & 3)) & 3;
        if (x->pos + cred_len + pad > x->len) return -1;
        x->pos += cred_len + pad;
    }
    /* skip verifier */
    if (xdr_uint32(x, &verf_flavor) < 0) return -1;
    if (xdr_uint32(x, &verf_len)    < 0) return -1;
    if (verf_len > 0) {
        if (verf_len > x->len - x->pos) return -1;   /* check before pad to avoid overflow */
        uint32_t pad = (4 - (verf_len & 3)) & 3;
        if (x->pos + verf_len + pad > x->len) return -1;
        x->pos += verf_len + pad;
    }
    return 0;
}

/* --- Common reply header --- */

static int encode_reply_ok(xdr_t *tx, uint32_t xid) {
    uint32_t msg_type    = RPC_REPLY;
    uint32_t reply_stat  = MSG_ACCEPTED;
    uint32_t verf_flavor = 0;
    uint32_t verf_len    = 0;
    uint32_t accept_stat = RPC_SUCCESS;
    if (xdr_uint32(tx, &xid)         < 0) return -1;
    if (xdr_uint32(tx, &msg_type)    < 0) return -1;
    if (xdr_uint32(tx, &reply_stat)  < 0) return -1;
    if (xdr_uint32(tx, &verf_flavor) < 0) return -1;
    if (xdr_uint32(tx, &verf_len)    < 0) return -1;
    if (xdr_uint32(tx, &accept_stat) < 0) return -1;
    return 0;
}

static int encode_proc_unavail(xdr_t *tx, uint32_t xid) {
    uint32_t msg_type    = RPC_REPLY;
    uint32_t reply_stat  = MSG_ACCEPTED;
    uint32_t verf_flavor = 0;
    uint32_t verf_len    = 0;
    uint32_t accept_stat = RPC_PROC_UNAVAIL;
    if (xdr_uint32(tx, &xid)         < 0) return -1;
    if (xdr_uint32(tx, &msg_type)    < 0) return -1;
    if (xdr_uint32(tx, &reply_stat)  < 0) return -1;
    if (xdr_uint32(tx, &verf_flavor) < 0) return -1;
    if (xdr_uint32(tx, &verf_len)    < 0) return -1;
    if (xdr_uint32(tx, &accept_stat) < 0) return -1;
    return 0;
}

/* --- fattr encode helper --- */

static int encode_fattr(xdr_t *tx, fattr_t *a) {
    if (xdr_uint32(tx, &a->type)       < 0) return -1;
    if (xdr_uint32(tx, &a->mode)       < 0) return -1;
    if (xdr_uint32(tx, &a->nlink)      < 0) return -1;
    if (xdr_uint32(tx, &a->uid)        < 0) return -1;
    if (xdr_uint32(tx, &a->gid)        < 0) return -1;
    if (xdr_uint32(tx, &a->size)       < 0) return -1;
    if (xdr_uint32(tx, &a->blocksize)  < 0) return -1;
    if (xdr_uint32(tx, &a->rdev)       < 0) return -1;
    if (xdr_uint32(tx, &a->blocks)     < 0) return -1;
    if (xdr_uint32(tx, &a->fsid)       < 0) return -1;
    if (xdr_uint32(tx, &a->fileid)     < 0) return -1;
    if (xdr_uint32(tx, &a->atime_sec)  < 0) return -1;
    if (xdr_uint32(tx, &a->atime_usec) < 0) return -1;
    if (xdr_uint32(tx, &a->mtime_sec)  < 0) return -1;
    if (xdr_uint32(tx, &a->mtime_usec) < 0) return -1;
    if (xdr_uint32(tx, &a->ctime_sec)  < 0) return -1;
    if (xdr_uint32(tx, &a->ctime_usec) < 0) return -1;
    return 0;
}

/* --- NFS procedure handlers --- */

static int handle_nfs_null(xdr_t *tx, uint32_t xid) {
    return encode_reply_ok(tx, xid);
}

static int handle_nfs_getattr(xdr_t *rx, xdr_t *tx, uint32_t xid,
                               nfs_ops_t *ops, void *ctx) {
    fhandle_t fh;
    if (xdr_fhandle(rx, &fh) < 0) return -1;

    fattr_t  attr   = {0};
    uint32_t status = (uint32_t)NFSERR_STALE;
    if (ops->getattr && ops->getattr(ctx, &fh, &attr) == 0)
        status = NFS_OK;

    if (encode_reply_ok(tx, xid) < 0) return -1;
    if (xdr_uint32(tx, &status) < 0) return -1;
    if (status == NFS_OK) return encode_fattr(tx, &attr);
    return 0;
}

static int handle_nfs_lookup(xdr_t *rx, xdr_t *tx, uint32_t xid,
                              nfs_ops_t *ops, void *ctx) {
    fhandle_t  dir;
    filename_t name = {0};
    if (xdr_fhandle(rx, &dir) < 0) return -1;
    if (xdr_varbytes(rx, &name.val, &name.len, MAXNAMLEN) < 0) return -1;

    fhandle_t fh_out;
    fattr_t   attr   = {0};
    uint32_t  status = (uint32_t)NFSERR_NOENT;
    if (ops->lookup && ops->lookup(ctx, &dir, &name, &fh_out, &attr) == 0)
        status = NFS_OK;

    if (encode_reply_ok(tx, xid) < 0) return -1;
    if (xdr_uint32(tx, &status) < 0) return -1;
    if (status == NFS_OK) {
        if (xdr_fhandle(tx, &fh_out) < 0) return -1;
        if (encode_fattr(tx, &attr) < 0) return -1;
    }
    return 0;
}

static int handle_nfs_read(xdr_t *rx, xdr_t *tx, uint32_t xid,
                            nfs_ops_t *ops, void *ctx) {
    fhandle_t fh;
    uint32_t  offset, count, totalcount;
    if (xdr_fhandle(rx, &fh)       < 0) return -1;
    if (xdr_uint32(rx, &offset)    < 0) return -1;
    if (xdr_uint32(rx, &count)     < 0) return -1;
    if (xdr_uint32(rx, &totalcount)< 0) return -1;

    if (count > MAXDATA) count = MAXDATA;

    uint8_t  data_buf[MAXDATA];
    uint32_t bytes_read = 0;
    fattr_t  attr       = {0};
    uint32_t status     = (uint32_t)NFSERR_STALE;

    if (ops->read && ops->read(ctx, &fh, offset, count,
                               data_buf, &bytes_read, &attr) == 0)
        status = NFS_OK;

    if (encode_reply_ok(tx, xid) < 0) return -1;
    if (xdr_uint32(tx, &status) < 0) return -1;
    if (status == NFS_OK) {
        if (encode_fattr(tx, &attr) < 0) return -1;
        uint8_t *dp = data_buf;
        if (xdr_varbytes(tx, &dp, &bytes_read, MAXDATA) < 0) return -1;
    }
    return 0;
}

static int handle_nfs_readdir(xdr_t *rx, xdr_t *tx, uint32_t xid,
                               nfs_ops_t *ops, void *ctx) {
    fhandle_t fh;
    uint8_t   raw_cookie[COOKIESIZE];
    uint32_t  count;
    if (xdr_fhandle(rx, &fh) < 0) return -1;
    if (xdr_opaque(rx, raw_cookie, COOKIESIZE) < 0) return -1;
    if (xdr_uint32(rx, &count) < 0) return -1;

    /* Interpret cookie as big-endian uint32 entry index. */
    uint32_t cookie = ((uint32_t)raw_cookie[0] << 24) | ((uint32_t)raw_cookie[1] << 16)
                    | ((uint32_t)raw_cookie[2] << 8)  |  (uint32_t)raw_cookie[3];

    dir_entry_t *entries = NULL;
    int          eof     = 1;
    uint32_t     status  = (uint32_t)NFSERR_STALE;

    if (ops->readdir && ops->readdir(ctx, &fh, cookie, count, &entries, &eof) == 0)
        status = NFS_OK;

    if (encode_reply_ok(tx, xid) < 0) goto cleanup;
    if (xdr_uint32(tx, &status) < 0) goto cleanup;

    if (status == NFS_OK) {
        uint32_t idx = cookie;
        for (dir_entry_t *e = entries; e; e = e->next) {
            uint32_t present = 1;
            if (xdr_uint32(tx, &present) < 0) goto cleanup;
            if (xdr_uint32(tx, &e->fileid) < 0) goto cleanup;
            if (xdr_varbytes(tx, &e->name.val, &e->name.len, MAXNAMLEN) < 0)
                goto cleanup;
            /* encode cookie as big-endian entry index */
            idx++;
            uint8_t ck[COOKIESIZE];
            ck[0] = (uint8_t)(idx >> 24);
            ck[1] = (uint8_t)(idx >> 16);
            ck[2] = (uint8_t)(idx >> 8);
            ck[3] = (uint8_t)(idx);
            if (xdr_opaque(tx, ck, COOKIESIZE) < 0) goto cleanup;
        }
        uint32_t end = 0;
        if (xdr_uint32(tx, &end) < 0) goto cleanup;
        int eof_val = eof;
        if (xdr_bool(tx, &eof_val) < 0) goto cleanup;
    }

    /* free entries (allocated by ops->readdir) */
    {
        dir_entry_t *e = entries;
        while (e) { dir_entry_t *nx = e->next; free(e->name.val); free(e); e = nx; }
    }
    return 0;

cleanup:
    {
        dir_entry_t *e = entries;
        while (e) { dir_entry_t *nx = e->next; free(e->name.val); free(e); e = nx; }
    }
    return -1;
}

static int handle_nfs_statfs(xdr_t *rx, xdr_t *tx, uint32_t xid,
                              nfs_ops_t *ops, void *ctx) {
    fhandle_t fh;
    if (xdr_fhandle(rx, &fh) < 0) return -1;

    uint32_t tsize = 0, bsize = 0, blocks = 0, bfree = 0, bavail = 0;
    uint32_t status = (uint32_t)NFSERR_STALE;
    if (ops->statfs && ops->statfs(ctx, &fh, &tsize, &bsize, &blocks, &bfree, &bavail) == 0)
        status = NFS_OK;

    if (encode_reply_ok(tx, xid) < 0) return -1;
    if (xdr_uint32(tx, &status) < 0) return -1;
    if (status == NFS_OK) {
        if (xdr_uint32(tx, &tsize)  < 0) return -1;
        if (xdr_uint32(tx, &bsize)  < 0) return -1;
        if (xdr_uint32(tx, &blocks) < 0) return -1;
        if (xdr_uint32(tx, &bfree)  < 0) return -1;
        if (xdr_uint32(tx, &bavail) < 0) return -1;
    }
    return 0;
}

/* Write operations reply with NFSERR_ROFS. */
static int handle_nfs_rofs(xdr_t *tx, uint32_t xid) {
    uint32_t status = (uint32_t)NFSERR_ROFS;
    if (encode_reply_ok(tx, xid) < 0) return -1;
    return xdr_uint32(tx, &status);
}

static int dispatch_nfs(xdr_t *rx, xdr_t *tx, rpc_call_hdr_t *hdr,
                        nfs_ops_t *ops, void *ctx) {
    switch (hdr->proc) {
    case NFSPROC_NULL:    return handle_nfs_null(tx, hdr->xid);
    case NFSPROC_GETATTR: return handle_nfs_getattr(rx, tx, hdr->xid, ops, ctx);
    case NFSPROC_LOOKUP:  return handle_nfs_lookup(rx, tx, hdr->xid, ops, ctx);
    case NFSPROC_READ:    return handle_nfs_read(rx, tx, hdr->xid, ops, ctx);
    case NFSPROC_READDIR: return handle_nfs_readdir(rx, tx, hdr->xid, ops, ctx);
    case NFSPROC_STATFS:  return handle_nfs_statfs(rx, tx, hdr->xid, ops, ctx);
    /* Write procedures → NFSERR_ROFS */
    case 2:  /* SETATTR */
    case 3:  /* ROOT */
    case 5:  /* WRITECACHE */
    case 7:  /* WRITE */
    case 8:  /* CREATE */
    case 9:  /* REMOVE */
    case 10: /* RENAME */
    case 11: /* LINK */
    case 12: /* SYMLINK */
    case 13: /* MKDIR */
    case 14: /* RMDIR */
        return handle_nfs_rofs(tx, hdr->xid);
    default:
        return encode_proc_unavail(tx, hdr->xid);
    }
}

/* --- Poll loop --- */

static int handle_datagram(int fd, nfs_server_t *s) {
    static uint8_t recv_buf[SERVER_BUF];
    static uint8_t send_buf[SERVER_BUF];

    struct sockaddr_in client;
    socklen_t          addrlen = sizeof(client);
    ssize_t nb = recvfrom(fd, recv_buf, sizeof(recv_buf), 0,
                          (struct sockaddr *)&client, &addrlen);
    if (nb < 0) return -1;

    xdr_t rx, tx;
    xdr_init_decode(&rx, recv_buf, (uint32_t)nb);
    xdr_init_encode(&tx, send_buf, sizeof(send_buf));

    rpc_call_hdr_t hdr;
    if (decode_call_header(&rx, &hdr) < 0) return 0;  /* bad packet, ignore */

    int rc;
    if (fd == s->mount_fd) {
        rc = mount_server_dispatch(&rx, &tx, hdr.xid, hdr.proc, s->ops, s->ctx);
    } else {
        rc = dispatch_nfs(&rx, &tx, &hdr, s->ops, s->ctx);
    }

    if (rc < 0) return -1;
    if (tx.pos > 0)
        sendto(fd, send_buf, tx.pos, 0, (struct sockaddr *)&client, addrlen);
    return 0;
}

int nfs_server_poll(nfs_server_t *s, int timeout_ms) {
    fd_set         fds;
    struct timeval tv;
    int            maxfd;

    FD_ZERO(&fds);
    FD_SET(s->mount_fd, &fds);
    FD_SET(s->nfs_fd,   &fds);
    maxfd = (s->mount_fd > s->nfs_fd) ? s->mount_fd : s->nfs_fd;

    tv.tv_sec  = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;

    int n = select(maxfd + 1, &fds, NULL, NULL, &tv);
    if (n < 0) return -1;
    if (n == 0) return 0;

    if (FD_ISSET(s->mount_fd, &fds)) handle_datagram(s->mount_fd, s);
    if (FD_ISSET(s->nfs_fd,   &fds)) handle_datagram(s->nfs_fd,   s);
    return 0;
}
