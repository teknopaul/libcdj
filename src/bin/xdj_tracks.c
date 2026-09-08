/*
 * xdj-tracks: list audio track filenames from Pioneer CDJ/XDJ USB drives via NFS.
 *
 * Usage: xdj-tracks <ip> [<ip2> ...]
 *
 * Connects to each deck's portmapper, discovers mount/NFS ports, mounts the
 * export, then recursively walks the USB drive directories (C, P, S, K) and
 * prints one path per line for each audio file found.
 *
 * Output format:   <player-ip>:<drive>/<path>/<filename.ext>
 *
 * Requires the deck to be powered on and visible on the local network.
 * Does not require joining the ProLink network as a virtual CDJ.
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

#define MAX_DEPTH    12
#define READDIR_COUNT 4096

/* Audio file extensions recognised as track files. */
static const char *audio_exts[] = {
    ".mp3", ".MP3",
    ".wav", ".WAV",
    ".flac", ".FLAC",
    ".aiff", ".AIFF", ".aif", ".AIF",
    ".ogg", ".OGG",
    ".m4a", ".M4A",
    ".mp4", ".MP4",
    ".aac", ".AAC",
    NULL
};

static int is_audio_name(const char *name)
{
    const char *dot = strrchr(name, '.');
    if (!dot) return 0;
    for (int i = 0; audio_exts[i]; i++) {
        if (strcmp(dot, audio_exts[i]) == 0) return 1;
    }
    return 0;
}

/* Return non-zero if name contains a '.' (has any extension). */
static int has_any_extension(const char *name)
{
    return strrchr(name, '.') != NULL;
}

/*
 * Recursively walk a directory, printing audio file paths.
 * Uses extension heuristic: no-extension names are treated as directories
 * (CDJ USB drives follow this convention consistently).
 */
static void walk_dir(rpc_client_t *nfs, fhandle_t *dir,
                     const char *prefix, int depth)
{
    if (depth > MAX_DEPTH) return;

    uint8_t cookie[COOKIESIZE] = {0};

    for (;;) {
        readdirres_t rdr;
        if (nfs_readdir(nfs, dir, cookie, READDIR_COUNT, &rdr) < 0) return;
        if (rdr.status != NFS_OK) { nfs_readdirres_free(&rdr); return; }

        for (dir_entry_t *e = rdr.entries; e; e = e->next) {
            char name[MAXNAMLEN + 1];
            if (utf16le_to_utf8(e->name.val, e->name.len, name, sizeof(name)) < 0) {
                memcpy(cookie, e->cookie, COOKIESIZE);
                continue;
            }

            if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
                memcpy(cookie, e->cookie, COOKIESIZE);
                continue;
            }

            char path[NFSMAXPATHLEN + 1];
            snprintf(path, sizeof(path), "%s/%s", prefix, name);

            if (is_audio_name(name)) {
                printf("%s\n", path);
            } else if (!has_any_extension(name)) {
                /* Likely a directory — LOOKUP to get its file handle. */
                uint8_t  name16[MAXNAMLEN * 2];
                uint32_t name16_len = 0;
                if (utf8_to_utf16le(name, name16, sizeof(name16), &name16_len) < 0) {
                    memcpy(cookie, e->cookie, COOKIESIZE);
                    continue;
                }

                diropargs_t args;
                args.dir      = *dir;
                args.name.val = name16;
                args.name.len = name16_len;

                diropres_t res;
                if (nfs_lookup(nfs, &args, &res) == 0 && res.status == NFS_OK) {
                    if (res.attributes.type == NFDIR) {
                        walk_dir(nfs, &res.file, path, depth + 1);
                    }
                }
            }

            memcpy(cookie, e->cookie, COOKIESIZE);
        }

        int eof = rdr.eof;
        nfs_readdirres_free(&rdr);
        if (eof) break;
    }
}

/*
 * Top-level drive directories to walk.  Pioneer CDJs typically use single-letter
 * names matching the USB drive letter assigned by rekordbox (C, P, S, K, etc.).
 * PIONEER/ contains metadata and is intentionally excluded.
 */
static const char *drive_dirs[] = { "C", "P", "S", "K", "D", "E", NULL };

static void list_tracks_for_host(const char *host)
{
    uint16_t mount_port = 0, nfs_port = 0;

    if (portmap_getport(host, MOUNT_PROG, MOUNT_VERS, &mount_port) != 0) {
        fprintf(stderr, "%s: portmapper did not return a mountd port\n", host);
        return;
    }
    if (nfs_getport(host, &nfs_port) != 0) {
        fprintf(stderr, "%s: portmapper did not return NFS port, trying 2049\n", host);
        nfs_port = 2049;
    }

    /* Fetch the export list. */
    rpc_client_t mc;
    if (rpc_client_init(&mc, host, mount_port, MOUNT_PROG, MOUNT_VERS, 5000) < 0) {
        fprintf(stderr, "%s: cannot connect to mountd on port %u\n", host, mount_port);
        return;
    }
    export_entry_t *exports = NULL;
    int erc = mount_export(&mc, &exports);
    rpc_client_destroy(&mc);
    if (erc < 0) {
        fprintf(stderr, "%s: EXPORT RPC failed\n", host);
        return;
    }
    if (!exports) {
        fprintf(stderr, "%s: no exports\n", host);
        return;
    }

    for (export_entry_t *exp = exports; exp; exp = exp->next) {
        /* Mount this export. */
        rpc_client_t mc2;
        if (rpc_client_init(&mc2, host, mount_port, MOUNT_PROG, MOUNT_VERS, 5000) < 0) continue;

        dirpath_t dp = { exp->filesystem.val, exp->filesystem.len };
        fhstatus_t fhs;
        int mrc = mount_mnt(&mc2, &dp, &fhs);
        rpc_client_destroy(&mc2);

        if (mrc < 0 || fhs.status != 0) continue;

        /* NFS client for this export. */
        rpc_client_t nfs;
        if (rpc_client_init(&nfs, host, nfs_port, NFS_PROG, NFS_VERS, 5000) < 0) continue;

        /* Walk each top-level drive directory. */
        for (int i = 0; drive_dirs[i]; i++) {
            fhandle_t sub_fh;
            fattr_t   sub_attr;
            int rc = nfs_path_lookup(&nfs, &fhs.directory, drive_dirs[i], &sub_fh, &sub_attr);
            if (rc == 0 && sub_attr.type == NFDIR) {
                char prefix[64];
                snprintf(prefix, sizeof(prefix), "%s:%s", host, drive_dirs[i]);
                walk_dir(&nfs, &sub_fh, prefix, 0);
            }
        }

        rpc_client_destroy(&nfs);
    }

    mount_export_free(exports);
}

int main(int argc, char *argv[])
{
    if (argc < 2) {
        fprintf(stderr, "Usage: xdj-tracks <ip> [<ip2> ...]\n");
        return 1;
    }
    for (int i = 1; i < argc; i++) {
        list_tracks_for_host(argv[i]);
    }
    return 0;
}
