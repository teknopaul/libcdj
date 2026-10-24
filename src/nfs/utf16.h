#ifndef CDJ_NFS_UTF16_H
#define CDJ_NFS_UTF16_H

#include <stdint.h>

/* ASCII → UTF-16LE (no BOM). Pioneer paths contain only ASCII-range characters. */
int utf8_to_utf16le(const char *src, uint8_t *dst, uint32_t dst_max, uint32_t *len_out);

/* UTF-16LE → NUL-terminated ASCII. Safe for Pioneer paths (all ASCII-range). */
int utf16le_to_utf8(const uint8_t *src, uint32_t src_len, char *dst, uint32_t dst_max);

/* Strict UTF-16LE → NUL-terminated UTF-8 (BMP + surrogate pairs). Unlike utf16le_to_utf8() it never
 * guesses the encoding, so non-ASCII text survives. Needs dst_max >= src_len * 3 / 2 + 1.
 * Returns 0 on success, -1 if dst is too small. Unpaired surrogates become U+FFFD. */
int utf16le_decode(const uint8_t *src, uint32_t src_len, char *dst, uint32_t dst_max);

/* Strict UTF-16BE → NUL-terminated UTF-8 (used by ANLZ analysis files). Same rules as utf16le_decode(), but
 * big-endian and decoding stops at the first NUL code unit (paths are stored with a trailing NUL). */
int utf16be_decode(const uint8_t *src, uint32_t src_len, char *dst, uint32_t dst_max);

#endif /* CDJ_NFS_UTF16_H */
