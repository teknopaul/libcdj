#ifndef CDJ_NFS_XDR_H
#define CDJ_NFS_XDR_H

#include <stdint.h>

/*
 * Minimal XDR (eXternal Data Representation, RFC 4506) encode/decode.
 * All values are big-endian, 4-byte aligned.  No dependency on rpc/rpc.h.
 *
 * Encode mode (encode=1): reads C values, writes to buf.
 * Decode mode (encode=0): reads from buf, writes to C values.
 */

typedef struct {
    uint8_t *buf;
    uint32_t pos;
    uint32_t len;
    int      encode;   /* 1 = encode (write to buf), 0 = decode (read from buf) */
} xdr_t;

void xdr_init_encode(xdr_t *x, uint8_t *buf, uint32_t len);
void xdr_init_decode(xdr_t *x, uint8_t *buf, uint32_t len);

/* All return 0 on success, -1 on overflow or underflow. */

int xdr_uint32(xdr_t *x, uint32_t *v);

/* Fixed-length opaque bytes, padded to 4-byte boundary. */
int xdr_opaque(xdr_t *x, uint8_t *buf, uint32_t len);

/*
 * Variable-length opaque (var_bytes): 4-byte length + bytes + padding.
 *
 * Encode: reads *len and *buf from caller, writes to XDR buffer.
 * Decode: reads length from XDR buffer into *len; sets *buf to point
 *         directly into the XDR buffer (zero-copy, no malloc).
 *         Caller must use the data before the XDR buffer is reused.
 */
int xdr_varbytes(xdr_t *x, uint8_t **buf, uint32_t *len, uint32_t maxlen);

/* XDR bool: encodes as 0 or 1 uint32. */
int xdr_bool(xdr_t *x, int *v);

/*
 * XDR optional (pointer / discriminated union with present/absent).
 * Encodes/decodes a uint32 discriminant then calls fn(x, obj) if present.
 */
int xdr_optional(xdr_t *x, int *present, void *obj,
                 int (*fn)(xdr_t *, void *));

#endif /* CDJ_NFS_XDR_H */
