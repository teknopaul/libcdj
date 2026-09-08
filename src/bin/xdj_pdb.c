/*
 * xdj-pdb: parse rekordbox export.pdb from Pioneer CDJ/XDJ players via NFS.
 *
 * Usage: xdj-pdb <ip> [--count] [--dump-paths] [--dump-ids] [--full]
 *
 * Connects to the deck's NFS server, fetches PIONEER/rekordbox/export.pdb,
 * parses it with pdb_parse(), and prints track metadata.
 *
 * Default output (TSV): id  bpm_x100  duration_secs  file_path
 *
 *   --count       Print only the total track count.
 *   --dump-paths  Print one file_path per line (grep-friendly).
 *   --dump-ids    Print "id title" pairs (for correlating with CDJ_STATUS).
 *   --full        Print all fields for every track (UTF-8).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "rpc.h"
#include "nfs_types.h"
#include "utf16.h"
#include "mount_client.h"
#include "nfs_client.h"
#include "portmap_client.h"
#include "pdb.h"

#define READ_CHUNK 2048

typedef enum {
    MODE_TSV,
    MODE_COUNT,
    MODE_DUMP_PATHS,
    MODE_DUMP_IDS,
    MODE_FULL,
} output_mode_t;

#define S(x) ((x) ? (x) : "")

typedef struct {
    output_mode_t mode;
    int           count;
    uint32_t      total_duration;
} ctx_t;

static int track_cb(const pdb_track_t *t, void *ud)
{
    ctx_t *ctx = ud;
    ctx->count++;
    ctx->total_duration += t->duration_secs;

    switch (ctx->mode) {
        case MODE_COUNT:
            break;
        case MODE_DUMP_PATHS:
            if (t->file_path)
                printf("%s\n", t->file_path);
            break;
        case MODE_DUMP_IDS:
            printf("%u\t%s\n", t->id, t->title ? t->title : "");
            break;
        case MODE_FULL:
            printf("id=%u title=%s\n"
                   "  file=%s\n"
                   "  bpm=%.2f dur=%us year=%u\n"
                   "  artist_id=%u album_id=%u genre_id=%u\n"
                   "  bitrate=%u sample_rate=%u depth=%u\n"
                   "  plays=%u rating=%u color=%u\n",
                   t->id, S(t->title),
                   S(t->file_path),
                   t->bpm_x100 / 100.0, t->duration_secs, t->year,
                   t->artist_id, t->album_id, t->genre_id,
                   t->bitrate, t->sample_rate, t->sample_depth,
                   t->play_count, t->rating, t->color_id);
            if (t->comment)      printf("  comment=%s\n",      t->comment);
            if (t->date_added)   printf("  added=%s\n",        t->date_added);
            if (t->release_date) printf("  released=%s\n",     t->release_date);
            if (t->mix_name)     printf("  mix=%s\n",          t->mix_name);
            break;
        case MODE_TSV:
        default:
            printf("%u\t%u\t%u\t%s\n",
                   t->id, t->bpm_x100, t->duration_secs,
                   t->file_path ? t->file_path : "");
            break;
    }
    return 0;
}

static uint8_t *fetch_pdb(const char *host, uint32_t *len_out)
{
    uint16_t mount_port = 0, nfs_port = 0;

    if (portmap_getport(host, MOUNT_PROG, MOUNT_VERS, &mount_port) != 0) {
        fprintf(stderr, "%s: portmapper: no mountd port\n", host);
        return NULL;
    }
    if (nfs_getport(host, &nfs_port) != 0)
        nfs_port = 2049;

    rpc_client_t mc;
    if (rpc_client_init(&mc, host, mount_port, MOUNT_PROG, MOUNT_VERS, 5000) < 0)
        return NULL;

    export_entry_t *exports = NULL;
    int rc = mount_export(&mc, &exports);
    rpc_client_destroy(&mc);
    if (rc < 0 || !exports) {
        fprintf(stderr, "%s: no exports\n", host);
        return NULL;
    }

    /* Use first export (typically /C/) */
    rpc_client_t mc2;
    if (rpc_client_init(&mc2, host, mount_port, MOUNT_PROG, MOUNT_VERS, 5000) < 0) {
        mount_export_free(exports);
        return NULL;
    }
    dirpath_t dp = { exports->filesystem.val, exports->filesystem.len };
    fhstatus_t fhs;
    rc = mount_mnt(&mc2, &dp, &fhs);
    rpc_client_destroy(&mc2);
    mount_export_free(exports);

    if (rc < 0 || fhs.status != 0) {
        fprintf(stderr, "%s: mount failed\n", host);
        return NULL;
    }

    rpc_client_t nfs;
    if (rpc_client_init(&nfs, host, nfs_port, NFS_PROG, NFS_VERS, 5000) < 0)
        return NULL;

    fhandle_t pdb_fh;
    fattr_t   pdb_attr;
    if (nfs_path_lookup(&nfs, &fhs.directory,
                        "PIONEER/rekordbox/export.pdb",
                        &pdb_fh, &pdb_attr) != 0) {
        fprintf(stderr, "%s: export.pdb not found\n", host);
        rpc_client_destroy(&nfs);
        return NULL;
    }

    uint32_t total = pdb_attr.size;
    uint8_t *buf   = malloc(total);
    if (!buf) {
        rpc_client_destroy(&nfs);
        return NULL;
    }

    uint32_t offset = 0;
    while (offset < total) {
        readargs_t ra = { .file = pdb_fh, .offset = offset,
                          .count = READ_CHUNK, .totalcount = 0 };
        readres_t rr;
        if (nfs_read(&nfs, &ra, &rr) < 0 || rr.status != NFS_OK) {
            nfs_readres_free(&rr);
            free(buf);
            rpc_client_destroy(&nfs);
            return NULL;
        }
        if (rr.data_len == 0) { nfs_readres_free(&rr); break; }
        memcpy(buf + offset, rr.data, rr.data_len);
        offset += rr.data_len;
        nfs_readres_free(&rr);
    }

    rpc_client_destroy(&nfs);
    *len_out = offset;
    return buf;
}

int main(int argc, char *argv[])
{
    if (argc < 2) {
        fprintf(stderr,
                "Usage: xdj-pdb <ip> [--count] [--dump-paths] [--dump-ids] [--full]\n");
        return 1;
    }

    const char   *host = argv[1];
    output_mode_t mode = MODE_TSV;

    for (int i = 2; i < argc; i++) {
        if      (strcmp(argv[i], "--count")      == 0) mode = MODE_COUNT;
        else if (strcmp(argv[i], "--dump-paths") == 0) mode = MODE_DUMP_PATHS;
        else if (strcmp(argv[i], "--dump-ids")   == 0) mode = MODE_DUMP_IDS;
        else if (strcmp(argv[i], "--full")       == 0) mode = MODE_FULL;
    }

    uint32_t len = 0;
    uint8_t *buf = fetch_pdb(host, &len);
    if (!buf) return 1;

    ctx_t ctx = { .mode = mode, .count = 0, .total_duration = 0 };
    int rc = pdb_parse(buf, len, track_cb, &ctx);
    free(buf);

    if (rc < 0) {
        fprintf(stderr, "%s: pdb_parse failed\n", host);
        return 1;
    }

    if (mode == MODE_COUNT)
        printf("%d\n", ctx.count);
    else
        fprintf(stderr, "%d tracks, total %us\n",
                ctx.count, ctx.total_duration);

    return 0;
}
