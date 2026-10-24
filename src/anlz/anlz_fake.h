#ifndef CDJ_ANLZ_FAKE_H
#define CDJ_ANLZ_FAKE_H

#include <stdint.h>

/*
 * Synthetic ANLZ file builder for tests. Written from the format doc, independently of the parser.
 *
 *   anlz_fake_t *b = anlz_fake_begin();
 *   anlz_fake_add_tag(b, "PQTZ", 0x18, body, body_len);   // body = tag bytes from offset 0x0c on
 *   uint8_t *file = anlz_fake_finish(b, &len);            // free(file)
 */
typedef struct anlz_fake anlz_fake_t;

anlz_fake_t *anlz_fake_begin(void);
/* len_header is stored as given (it need not match anything). len_tag = 12 + body_len. */
void anlz_fake_add_tag(anlz_fake_t *b, const char fourcc[4], uint32_t len_header,
                       const uint8_t *body, uint32_t body_len);
/* PPTH with a UTF-8 path, stored as UTF-16BE + NUL. */
void anlz_fake_add_ppth(anlz_fake_t *b, const char *utf8_path);

typedef struct { uint16_t number; uint16_t bpm_x100; uint32_t time_ms; } anlz_fake_beat_t;
/* PQTZ with n beats; len_beats_field is written as given (pass n for a normal file). */
void anlz_fake_add_pqtz(anlz_fake_t *b, const anlz_fake_beat_t *beats, uint32_t n, uint32_t len_beats_field);
/* PQT2: header anchors from first/last, count field `count_field`, then n 16-bit fractions (microseconds). */
void anlz_fake_add_pqt2(anlz_fake_t *b, anlz_fake_beat_t first, anlz_fake_beat_t last,
                        uint32_t count_field, const uint16_t *sub_us, uint32_t n);

typedef struct {
    uint32_t    hot_cue;            /* 0 = memory point */
    int         is_loop;
    uint32_t    time_ms, loop_time_ms;
    uint32_t    status;             /* PCPT only */
    uint32_t    marker;             /* 3 bytes; 0 = write the usual 1000 */
    uint8_t     color_id;           /* PCP2 only from here on */
    uint8_t     raw_1d[3];
    uint16_t    loop_num, loop_den;
    const char *comment;            /* UTF-8 or NULL */
    int         has_rgb;
    uint8_t     color_code, red, green, blue;
} anlz_fake_cue_t;
/* PCOB (PCPT entries, 0x38 bytes each); len_cues_field is written as given. */
void anlz_fake_add_pcob(anlz_fake_t *b, uint32_t list_type, const anlz_fake_cue_t *cues, uint32_t n, uint32_t len_cues_field);
/* PCO2 (variable PCP2 entries: 0x2c bytes + comment (UTF-16BE, NUL included in len_comment) + optional 4 colour bytes). */
void anlz_fake_add_pco2(anlz_fake_t *b, uint32_t list_type, const anlz_fake_cue_t *cues, uint32_t n, uint32_t len_cues_field);

/* A waveform tag with the header layout of that fourcc ("PWAV","PWV2","PWV3","PWV4","PWV5","PWV6","PWV7").
 * count_field is written as given; data is data_len raw bytes. */
void anlz_fake_add_wave(anlz_fake_t *b, const char fourcc[4], uint32_t entry_bytes, uint32_t count_field,
                        const uint8_t *data, uint32_t data_len);

/* Frees the builder; the caller free()s the returned buffer. */
uint8_t *anlz_fake_finish(anlz_fake_t *b, uint32_t *len_out);

#endif /* CDJ_ANLZ_FAKE_H */
