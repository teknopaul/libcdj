#include "utf16.h"

//SNIP_utf16

static int utf16_decode(const uint8_t *src, uint32_t src_len, char *dst, uint32_t dst_max, int big_endian,
                        int stop_at_nul) {
    uint32_t out = 0;
    for (uint32_t i = 0; i + 1 < src_len; i += 2) {
        uint32_t c = big_endian ? (((uint32_t)src[i] << 8) | src[i + 1]) : ((uint32_t)src[i] | ((uint32_t)src[i + 1] << 8));
        if (c == 0 && stop_at_nul) break;
        if (c >= 0xD800 && c < 0xDC00 && i + 3 < src_len) {
            uint32_t lo = big_endian ? (((uint32_t)src[i + 2] << 8) | src[i + 3])
                                     : ((uint32_t)src[i + 2] | ((uint32_t)src[i + 3] << 8));
            if (lo >= 0xDC00 && lo < 0xE000) {
                c = 0x10000 + ((c - 0xD800) << 10) + (lo - 0xDC00);
                i += 2;
            } else {
                c = 0xFFFD;
            }
        } else if (c >= 0xD800 && c < 0xE000) {
            c = 0xFFFD;
        }
        uint32_t need = c < 0x80 ? 1 : c < 0x800 ? 2 : c < 0x10000 ? 3 : 4;
        if (out + need + 1 > dst_max) return -1;
        if (need == 1) {
            dst[out++] = (char)c;
        } else if (need == 2) {
            dst[out++] = (char)(0xC0 | (c >> 6));
            dst[out++] = (char)(0x80 | (c & 0x3F));
        } else if (need == 3) {
            dst[out++] = (char)(0xE0 | (c >> 12));
            dst[out++] = (char)(0x80 | ((c >> 6) & 0x3F));
            dst[out++] = (char)(0x80 | (c & 0x3F));
        } else {
            dst[out++] = (char)(0xF0 | (c >> 18));
            dst[out++] = (char)(0x80 | ((c >> 12) & 0x3F));
            dst[out++] = (char)(0x80 | ((c >> 6) & 0x3F));
            dst[out++] = (char)(0x80 | (c & 0x3F));
        }
    }
    if (out >= dst_max) return -1;
    dst[out] = '\0';
    return 0;
}

int utf16le_decode(const uint8_t *src, uint32_t src_len, char *dst, uint32_t dst_max) {
    return utf16_decode(src, src_len, dst, dst_max, 0, 0);
}

int utf16be_decode(const uint8_t *src, uint32_t src_len, char *dst, uint32_t dst_max) {
    return utf16_decode(src, src_len, dst, dst_max, 1, 1);
}

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
