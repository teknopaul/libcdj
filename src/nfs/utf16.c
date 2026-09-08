#include "utf16.h"

//SNIP_utf16

int utf8_to_utf16le(const char *src, uint8_t *dst, uint32_t dst_max, uint32_t *len_out) {
    uint32_t n = 0;
    while (*src) {
        if (n + 2 > dst_max) return -1;
        dst[n++] = (uint8_t)(*src);
        dst[n++] = 0x00;
        src++;
    }
    *len_out = n;
    return 0;
}

int utf16le_to_utf8(const uint8_t *src, uint32_t src_len, char *dst, uint32_t dst_max) {
    uint32_t i   = 0;
    uint32_t out = 0;
    /* If even-length and second byte is 0x00, treat as UTF-16LE (Pioneer standard).
     * Otherwise fall through to plain ASCII (XDJ READDIR sends 1-byte ASCII names). */
    if (src_len >= 2 && src[1] == 0x00) {
        while (i + 1 < src_len) {
            if (out + 1 >= dst_max) return -1;
            dst[out++] = (char)src[i];
            i += 2;
        }
    } else {
        while (i < src_len) {
            if (out + 1 >= dst_max) return -1;
            dst[out++] = (char)src[i++];
        }
    }
    if (out >= dst_max) return -1;
    dst[out] = '\0';
    return 0;
}

//SNIP_utf16
