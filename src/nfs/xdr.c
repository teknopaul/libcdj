#include <string.h>
#include <arpa/inet.h>
#include "xdr.h"

void xdr_init_encode(xdr_t *x, uint8_t *buf, uint32_t len) {
    x->buf    = buf;
    x->pos    = 0;
    x->len    = len;
    x->encode = 1;
}

void xdr_init_decode(xdr_t *x, uint8_t *buf, uint32_t len) {
    x->buf    = buf;
    x->pos    = 0;
    x->len    = len;
    x->encode = 0;
}

int xdr_uint32(xdr_t *x, uint32_t *v) {
    if (x->pos + 4 > x->len) return -1;
    if (x->encode) {
        uint32_t n = htonl(*v);
        memcpy(x->buf + x->pos, &n, 4);
    } else {
        uint32_t n;
        memcpy(&n, x->buf + x->pos, 4);
        *v = ntohl(n);
    }
    x->pos += 4;
    return 0;
}

int xdr_opaque(xdr_t *x, uint8_t *buf, uint32_t len) {
    uint32_t pad = (4 - (len & 3)) & 3;
    if (x->pos + len + pad > x->len) return -1;
    if (x->encode) {
        memcpy(x->buf + x->pos, buf, len);
        if (pad) memset(x->buf + x->pos + len, 0, pad);
    } else {
        memcpy(buf, x->buf + x->pos, len);
    }
    x->pos += len + pad;
    return 0;
}

int xdr_varbytes(xdr_t *x, uint8_t **buf, uint32_t *len, uint32_t maxlen) {
    if (x->encode) {
        if (*len > maxlen) return -1;
        if (xdr_uint32(x, len) < 0) return -1;
        return xdr_opaque(x, *buf, *len);
    } else {
        if (xdr_uint32(x, len) < 0) return -1;
        if (*len > maxlen) return -1;
        uint32_t pad = (4 - (*len & 3)) & 3;
        if (x->pos + *len + pad > x->len) return -1;
        *buf = x->buf + x->pos;
        x->pos += *len + pad;
        return 0;
    }
}

int xdr_bool(xdr_t *x, int *v) {
    uint32_t u = (*v != 0) ? 1 : 0;
    if (xdr_uint32(x, &u) < 0) return -1;
    if (!x->encode) *v = (u != 0) ? 1 : 0;
    return 0;
}

int xdr_optional(xdr_t *x, int *present, void *obj,
                 int (*fn)(xdr_t *, void *)) {
    if (xdr_bool(x, present) < 0) return -1;
    if (*present) return fn(x, obj);
    return 0;
}
