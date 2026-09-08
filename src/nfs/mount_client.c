#include <stdlib.h>
#include <string.h>
#include "mount_client.h"
#include "portmap_client.h"

/* Mount procedure numbers (RFC 1094 Appendix A). */
#define MOUNTPROC_MNT     1
#define MOUNTPROC_EXPORT  5

/* --- Shared XDR codecs (declared in nfs_types.h) --- */

int xdr_fhandle(xdr_t *x, fhandle_t *fh) {
    return xdr_opaque(x, fh->data, FHSIZE);
}

int xdr_dirpath(xdr_t *x, dirpath_t *dp) {
    return xdr_varbytes(x, &dp->val, &dp->len, MNTPATHLEN);
}

int xdr_fhstatus(xdr_t *x, fhstatus_t *fhs) {
    if (xdr_uint32(x, &fhs->status) < 0) return -1;
    if (fhs->status == 0)
        return xdr_fhandle(x, &fhs->directory);
    return 0;
}

/* --- Mount client implementation --- */

int mount_getport(const char *host, uint16_t *port_out) {
    return portmap_getport(host, MOUNT_PROG, MOUNT_VERS, port_out);
}

/*
 * Skip past the groups list in the EXPORT response.
 * groups = optional(groupnode { name: string, next: groups })
 */
static int skip_groups(xdr_t *x) {
    uint32_t present;
    while (xdr_uint32(x, &present) == 0 && present) {
        uint8_t *gn;
        uint32_t gnlen;
        if (xdr_varbytes(x, &gn, &gnlen, MNTNAMLEN) < 0) return -1;
    }
    return 0;
}

/*
 * Decode the exports linked list from the XDR stream.
 *
 * Wire format (RFC 1094 Appendix A):
 *   exports = optional(exportnode)
 *   exportnode = { filesys: dirpath, groups: groups, exNext: exports }
 *
 * The loop reads each discriminant; after reading filesys+groups it loops back
 * to read the exNext discriminant, which is the next iteration's condition.
 */
static int decode_exports(xdr_t *x, export_entry_t **head_out) {
    export_entry_t  *head = NULL;
    export_entry_t **tail = &head;
    uint32_t         present;

    while (xdr_uint32(x, &present) == 0 && present) {
        uint8_t  *val;
        uint32_t  len;
        if (xdr_varbytes(x, &val, &len, MNTPATHLEN) < 0) goto fail;

        export_entry_t *e = calloc(1, sizeof(*e));
        if (!e) goto fail;

        e->filesystem.val = malloc(len + 1);
        if (!e->filesystem.val) { free(e); goto fail; }
        memcpy(e->filesystem.val, val, len);
        e->filesystem.len = len;

        *tail = e;
        tail  = &e->next;

        if (skip_groups(x) < 0) goto fail;
    }

    *head_out = head;
    return 0;

fail:
    mount_export_free(head);
    return -1;
}

static int decode_export_result(xdr_t *x, void *res) {
    return decode_exports(x, (export_entry_t **)res);
}

int mount_export(rpc_client_t *c, export_entry_t **list_out) {
    *list_out = NULL;
    return rpc_call(c, MOUNTPROC_EXPORT, NULL, NULL,
                    decode_export_result, list_out);
}

static int encode_mnt_args(xdr_t *x, void *arg) {
    return xdr_dirpath(x, (dirpath_t *)arg);
}

static int decode_mnt_result(xdr_t *x, void *res) {
    return xdr_fhstatus(x, (fhstatus_t *)res);
}

int mount_mnt(rpc_client_t *c, dirpath_t *path, fhstatus_t *result) {
    memset(result, 0, sizeof(*result));
    return rpc_call(c, MOUNTPROC_MNT, encode_mnt_args, path,
                    decode_mnt_result, result);
}

void mount_export_free(export_entry_t *list) {
    while (list) {
        export_entry_t *next = list->next;
        free(list->filesystem.val);
        free(list);
        list = next;
    }
}
