#include <stdlib.h>
#include <string.h>
#include "mount_server.h"
#include "nfs_types.h"
#include "mount_client.h"

/* Mount procedure numbers */
#define MOUNTPROC_NULL    0
#define MOUNTPROC_MNT     1
#define MOUNTPROC_UMNT    3
#define MOUNTPROC_UMNTALL 4
#define MOUNTPROC_EXPORT  5

/* RPC accept_stat */
#define RPC_SUCCESS       0
#define RPC_PROC_UNAVAIL  3

static int encode_reply_ok(xdr_t *tx, uint32_t xid) {
    uint32_t msg_type    = 1;  /* REPLY */
    uint32_t reply_stat  = 0;  /* MSG_ACCEPTED */
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
    uint32_t msg_type    = 1;
    uint32_t reply_stat  = 0;
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

static int handle_export(xdr_t *tx, export_entry_t *list) {
    for (export_entry_t *e = list; e; e = e->next) {
        uint32_t present = 1;
        if (xdr_uint32(tx, &present) < 0) return -1;
        if (xdr_dirpath(tx, &e->filesystem) < 0) return -1;
        if (e->group.len) {                                /* groups: one node, then the end of the groups list */
            uint32_t one = 1;
            if (xdr_uint32(tx, &one) < 0) return -1;
            if (xdr_dirpath(tx, &e->group) < 0) return -1;
        }
        uint32_t no_groups = 0;
        if (xdr_uint32(tx, &no_groups) < 0) return -1;
    }
    uint32_t end = 0;
    return xdr_uint32(tx, &end);
}

static int handle_mnt(xdr_t *rx, xdr_t *tx, nfs_ops_t *ops, void *ctx) {
    dirpath_t dp = {0};
    if (xdr_dirpath(rx, &dp) < 0) return -1;

    fhandle_t fh;
    uint32_t  status;

    if (!ops->mount || ops->mount(ctx, &dp, &fh) < 0) {
        status = 13;  /* NFSERR_ACCES */
        if (xdr_uint32(tx, &status) < 0) return -1;
        return 0;
    }

    status = 0;  /* MNT_OK */
    if (xdr_uint32(tx, &status) < 0) return -1;
    if (xdr_fhandle(tx, &fh) < 0) return -1;
    return 0;
}

int mount_server_dispatch(xdr_t *rx, xdr_t *tx,
                          uint32_t xid, uint32_t proc,
                          nfs_ops_t *ops, void *ctx) {
    switch (proc) {
    case MOUNTPROC_NULL:
        return encode_reply_ok(tx, xid);

    case MOUNTPROC_MNT:
        if (encode_reply_ok(tx, xid) < 0) return -1;
        return handle_mnt(rx, tx, ops, ctx);

    case MOUNTPROC_UMNT: {
        /* A player unmounts when it sees our USB slot go to "removable" (prolink-usb-change.pcap: the XDJ-700 answers with an empty success).
         * Answering "procedure unavailable" tells it the unmount failed, and it then never mounts the slot again. */
        dirpath_t dp = {0};
        int have = xdr_dirpath(rx, &dp) == 0;
        if (ops->unmount) ops->unmount(ctx, have ? &dp : NULL);
        return encode_reply_ok(tx, xid);
    }

    case MOUNTPROC_UMNTALL:
        if (ops->unmount) ops->unmount(ctx, NULL);
        return encode_reply_ok(tx, xid);

    case MOUNTPROC_EXPORT: {
        export_entry_t *list = NULL;
        int rc = ops->list_exports ? ops->list_exports(ctx, &list) : -1;
        if (rc == -2) { mount_export_free(list); return encode_proc_unavail(tx, xid); }   /* "no such procedure" */
        if (rc < 0) list = NULL;                                                            /* any other failure: an empty list */
        int r = encode_reply_ok(tx, xid) < 0 ? -1 : handle_export(tx, list);
        mount_export_free(list);
        return r;
    }

    default:
        return encode_proc_unavail(tx, xid);
    }
}
