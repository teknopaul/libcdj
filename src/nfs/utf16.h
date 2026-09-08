#ifndef CDJ_NFS_UTF16_H
#define CDJ_NFS_UTF16_H

#include <stdint.h>

/* ASCII → UTF-16LE (no BOM). Pioneer paths contain only ASCII-range characters. */
int utf8_to_utf16le(const char *src, uint8_t *dst, uint32_t dst_max, uint32_t *len_out);

/* UTF-16LE → NUL-terminated ASCII. Safe for Pioneer paths (all ASCII-range). */
int utf16le_to_utf8(const uint8_t *src, uint32_t src_len, char *dst, uint32_t dst_max);

#endif /* CDJ_NFS_UTF16_H */
