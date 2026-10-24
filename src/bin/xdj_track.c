/*
 * xdj-track: show what libcdj reads from a track's ANLZ files, and cross-check a whole rekordbox export.
 *
 *   xdj-track <root> <analyze_path>     summary of one track, e.g.
 *                                       xdj-track /var/tmp/rekordbox-example /PIONEER/USBANLZ/P042/00015D16/ANLZ0000.DAT
 *   xdj-track --check <root>            for every track in <root>/PIONEER/rekordbox/export.pdb: open its analysis, compare
 *                                       the tempo and length with the database, print statistics and outliers
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "anlz.h"
#include "pdb.h"

static void summary(const anlz_track_t *t)
{
    const char *path;
    if (anlz_track_path(t, &path) == ANLZ_OK) printf("path:      %s\n", path);
    printf("files:     DAT=%d EXT=%d 2EX=%d\n", anlz_track_file(t, ANLZ_FILE_DAT) != NULL,
           anlz_track_file(t, ANLZ_FILE_EXT) != NULL, anlz_track_file(t, ANLZ_FILE_2EX) != NULL);

    anlz_beatgrid_t g;
    if (anlz_track_beatgrid(t, &g) == ANLZ_OK) {
        anlz_beat_t first, last;
        if (g.count) {
            anlz_beat_at(&g, 0, &first);
            anlz_beat_at(&g, g.count - 1, &last);
            printf("beat grid: %u beats from %s, first %u ms (%u us) tempo %.2f, last %u ms, avg %.3f BPM%s%s\n",
                   g.count, g.source == ANLZ_GRID_PQTZ ? "PQTZ" : "PQT2", first.time_ms, first.sub_ms_us,
                   first.bpm_x100 / 100.0, last.time_ms, anlz_avg_bpm(&g),
                   g.has_sub_ms ? "" : " (no microseconds)", g.monotonic ? "" : " NOT MONOTONIC");
        } else {
            printf("beat grid: empty (track not analysed)\n");
        }
    } else {
        printf("beat grid: none\n");
    }

    anlz_cue_list_t l;
    if (anlz_track_cues(t, ANLZ_CUES_ALL, &l) == ANLZ_OK) {
        printf("cues:      %u%s\n", l.count, l.damaged ? " (damaged list)" : "");
        for (uint32_t i = 0; i < l.count; i++) {
            const anlz_cue_t *c = &l.cues[i];
            printf("  %s %c  %8u ms", c->hot_cue ? "hot" : "mem", c->hot_cue ? 'A' + c->hot_cue - 1 : '-', c->time_ms);
            if (c->is_loop) printf("  loop to %u ms (%u/%u beats)", c->loop_time_ms, c->loop_num, c->loop_den);
            if (c->has_rgb) printf("  rgb %02x%02x%02x", c->red, c->green, c->blue);
            if (c->comment[0]) printf("  \"%s\"", c->comment);
            printf("\n");
        }
    }
    anlz_cue_list_free(&l);

    static const struct { anlz_wave_kind_t k; const char *name; } W[] = {
        { ANLZ_WAVE_PREVIEW, "PWAV" }, { ANLZ_WAVE_TINY, "PWV2" }, { ANLZ_WAVE_DETAIL, "PWV3" },
        { ANLZ_WAVE_COLOR_PREVIEW, "PWV4" }, { ANLZ_WAVE_COLOR_DETAIL, "PWV5" },
        { ANLZ_WAVE_3BAND_PREVIEW, "PWV6" }, { ANLZ_WAVE_3BAND_DETAIL, "PWV7" } };
    printf("waveforms:");
    for (unsigned i = 0; i < sizeof W / sizeof W[0]; i++) {
        anlz_waveform_t w;
        if (anlz_track_waveform(t, W[i].k, &w) == ANLZ_OK) printf(" %s(%u)", W[i].name, w.count);
    }
    printf("\n");
}

typedef struct {
    const char *root;
    unsigned tracks, opened, no_analyze_path, bpm_checked, bpm_ok, dur_checked, dur_ok, damaged;
    unsigned shown;
} check_t;

static int check_cb(const pdb_track_t *t, void *ud)
{
    check_t *c = ud;
    c->tracks++;
    if (!t->analyze_path || !t->analyze_path[0]) { c->no_analyze_path++; return 0; }

    anlz_track_t *tr;
    if (anlz_track_open(c->root, t->analyze_path, &tr) != ANLZ_OK) {
        if (c->shown++ < 10) printf("cannot open %s (track %u)\n", t->analyze_path, t->id);
        return 0;
    }
    c->opened++;
    anlz_beatgrid_t g;
    if (anlz_track_beatgrid(tr, &g) == ANLZ_OK && t->bpm_x100 > 0 && g.count > 0) {
        anlz_beat_t b;
        anlz_beat_at(&g, 0, &b);
        c->bpm_checked++;
        if (fabs(b.bpm_x100 / 100.0 - t->bpm_x100 / 100.0) < 1.0) c->bpm_ok++;
        else if (c->shown++ < 10) printf("bpm differs: track %u %s db %.2f anlz %.2f\n", t->id, t->analyze_path, t->bpm_x100 / 100.0, b.bpm_x100 / 100.0);
        anlz_beat_at(&g, g.count - 1, &b);
        c->dur_checked++;
        if (fabs(b.time_ms / 1000.0 - t->duration_secs) < 10.0) c->dur_ok++;
        else if (c->shown++ < 10) printf("length differs: track %u %s db %us last beat %.1fs\n", t->id, t->analyze_path, t->duration_secs, b.time_ms / 1000.0);
    }
    anlz_cue_list_t l;
    if (anlz_track_cues(tr, ANLZ_CUES_ALL, &l) == ANLZ_OK && l.damaged) c->damaged++;
    anlz_cue_list_free(&l);
    anlz_track_close(tr);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc == 3 && strcmp(argv[1], "--check") == 0) {
        char pdb[4096];
        snprintf(pdb, sizeof pdb, "%s/PIONEER/rekordbox/export.pdb", argv[2]);
        check_t c = { argv[2], 0, 0, 0, 0, 0, 0, 0, 0, 0 };
        if (pdb_parse_file(pdb, check_cb, &c) != 0) { fprintf(stderr, "cannot read %s\n", pdb); return 1; }
        printf("tracks %u, analysis opened %u, no analyze_path %u, cue lists damaged %u\n", c.tracks, c.opened, c.no_analyze_path, c.damaged);
        printf("tempo matches database (within 1 BPM): %u/%u\n", c.bpm_ok, c.bpm_checked);
        printf("last beat within 10 s of database duration: %u/%u\n", c.dur_ok, c.dur_checked);
        return (c.bpm_checked && c.bpm_ok * 100 >= c.bpm_checked * 95 && c.dur_ok * 100 >= c.dur_checked * 95) ? 0 : 1;
    }
    if (argc == 3) {
        anlz_track_t *t;
        anlz_err_t err = anlz_track_open(argv[1], argv[2], &t);
        if (err != ANLZ_OK) { fprintf(stderr, "%s\n", anlz_strerror(err)); return 1; }
        summary(t);
        anlz_track_close(t);
        return 0;
    }
    fprintf(stderr, "usage: xdj-track <root> <analyze_path> | xdj-track --check <root>\n");
    return 2;
}
