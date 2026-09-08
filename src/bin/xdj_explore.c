/*
 * xdj-explore: Connect to a Pioneer XDJ/CDJ NFS server and list/fetch files.
 *
 * Usage: xdj-explore <ip> [--fetch-pdb <out.pdb>] [--verbose]
 *                    [--mount-port PORT] [--nfs-port PORT]
 *
 * Steps:
 *   1. Portmapper query → mountd port
 *   2. Portmapper query → NFS port
 *   3. MOUNTPROC_EXPORT → print exports
 *   4. MOUNTPROC_MNT    → root file handle per export
 *   5. NFSPROC_READDIR  → list root directory entries
 *   6. NFSPROC_LOOKUP   → PIONEER/rekordbox/export.pdb
 *   7. NFSPROC_GETATTR  → print file size
 *   8. NFSPROC_READ     → [optional] save export.pdb locally
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>

#include "rpc.h"
#include "nfs_types.h"
#include "utf16.h"
#include "mount_client.h"
#include "nfs_client.h"
#include "portmap_client.h"

#define READ_CHUNK 2048   /* match crate-digger's chunk size */

static int verbose = 0;

static void print_utf16le_path(const uint8_t *val, uint32_t len) {
    if (verbose) {
        printf("[%u bytes:", len);
        for (uint32_t i = 0; i < len && i < 16; i++) printf(" %02x", val[i]);
        if (len > 16) printf("...");
        printf("] ");
    }
    char buf[MNTPATHLEN + 1];
    if (utf16le_to_utf8(val, len, buf, sizeof(buf)) < 0)
        printf("<decode-error>");
    else
        printf("%s", buf);
}

static long ms_elapsed(struct timespec *start) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (long)(now.tv_sec - start->tv_sec) * 1000
         + (long)(now.tv_nsec - start->tv_nsec) / 1000000;
}

static int fetch_pdb(rpc_client_t *nfs, fhandle_t *fh, fattr_t *attr,
                     const char *out_path) {
    FILE *f = fopen(out_path, "wb");
    if (!f) { perror(out_path); return -1; }

    uint32_t offset = 0;
    uint32_t total  = attr->size;
    printf("  Fetching %u bytes → %s\n", total, out_path);

    while (offset < total) {
        readargs_t ra;
        ra.file       = *fh;
        ra.offset     = offset;
        ra.count      = READ_CHUNK;
        ra.totalcount = 0;

        readres_t rr;
        if (nfs_read(nfs, &ra, &rr) < 0) {
            fprintf(stderr, "  READ error at offset %u\n", offset);
            fclose(f);
            return -1;
        }
        if (rr.status != NFS_OK) {
            fprintf(stderr, "  NFS READ error %u at offset %u\n", rr.status, offset);
            nfs_readres_free(&rr);
            fclose(f);
            return -1;
        }
        if (rr.data_len == 0) {
            nfs_readres_free(&rr);
            break;  /* EOF */
        }
        fwrite(rr.data, 1, rr.data_len, f);
        offset += rr.data_len;
        nfs_readres_free(&rr);

        if (verbose) printf("  %u / %u bytes\r", offset, total);
    }
    if (verbose) printf("\n");
    fclose(f);
    return 0;
}

/* Print all entries in a directory handle, following READDIR cookies for pagination. */
static void list_dir(rpc_client_t *nfs, fhandle_t *dir, const char *indent) {
    uint8_t cookie[COOKIESIZE] = {0};
    int     round = 0;
    for (;;) {
        readdirres_t rdr;
        if (nfs_readdir(nfs, dir, cookie, 8192, &rdr) < 0) {
            printf("%sREADDIR transport error\n", indent); return;
        }
        if (rdr.status != NFS_OK) {
            printf("%sREADDIR NFS error %u\n", indent, rdr.status);
            nfs_readdirres_free(&rdr); return;
        }
        if (!rdr.entries && round == 0) { printf("%s(empty)\n", indent); }
        for (dir_entry_t *e = rdr.entries; e; e = e->next) {
            printf("%s[%u] ", indent, e->fileid);
            print_utf16le_path(e->name.val, e->name.len);
            printf("\n");
            memcpy(cookie, e->cookie, COOKIESIZE);
        }
        int eof = rdr.eof;
        nfs_readdirres_free(&rdr);
        if (eof) break;
        round++;
        if (round > 20) { printf("%s... (truncated)\n", indent); break; }
    }
}

/* LOOKUP an ASCII path component from dir, print result, return 0 on success. */
static int lookup_and_show(rpc_client_t *nfs, fhandle_t *dir,
                            const char *name_ascii, const char *indent,
                            fhandle_t *fh_out, fattr_t *attr_out) {
    uint8_t  name16[MAXNAMLEN * 2];
    uint32_t name16_len = 0;
    if (utf8_to_utf16le(name_ascii, name16, sizeof(name16), &name16_len) < 0) return -1;

    diropargs_t args;
    args.dir      = *dir;
    args.name.val = name16;
    args.name.len = name16_len;

    diropres_t res;
    int rc = nfs_lookup(nfs, &args, &res);
    if (rc < 0) {
        printf("%sLOOKUP '%s': transport error\n", indent, name_ascii); return -1;
    }
    if (res.status != NFS_OK) {
        printf("%sLOOKUP '%s': NFS error %u\n", indent, name_ascii, res.status); return (int)res.status;
    }
    const char *type = (res.attributes.type == NFDIR) ? "dir" : "file";
    printf("%sLOOKUP '%s': %s, %u bytes\n", indent, name_ascii, type, res.attributes.size);
    if (fh_out)   *fh_out   = res.file;
    if (attr_out) *attr_out = res.attributes;
    return 0;
}

static void explore_export(const char *host, uint8_t *path_val, uint32_t path_len,
                            uint16_t mount_port, uint16_t nfs_port,
                            const char *fetch_pdb_path,
                            const char *fetch_ext_pdb_path) {
    char path_ascii[MNTPATHLEN + 1];
    utf16le_to_utf8(path_val, path_len, path_ascii, sizeof(path_ascii));
    printf("\nExport: %s\n", path_ascii);

    /* Mount the export. */
    rpc_client_t mount_c;
    if (rpc_client_init(&mount_c, host, mount_port, MOUNT_PROG, MOUNT_VERS, 3000) < 0) {
        fprintf(stderr, "  Cannot connect to mountd\n"); return;
    }

    dirpath_t dp = { path_val, path_len };
    fhstatus_t fhs;
    if (mount_mnt(&mount_c, &dp, &fhs) < 0) {
        fprintf(stderr, "  MNT RPC error\n");
        rpc_client_destroy(&mount_c);
        return;
    }
    rpc_client_destroy(&mount_c);

    if (fhs.status != 0) {
        fprintf(stderr, "  MNT error %u (NFSERR_ACCES=13 means network/auth issue)\n",
                fhs.status);
        return;
    }
    printf("  Mounted OK, root handle obtained\n");

    /* NFS client. */
    rpc_client_t nfs_c;
    if (rpc_client_init(&nfs_c, host, nfs_port, NFS_PROG, NFS_VERS, 3000) < 0) {
        fprintf(stderr, "  Cannot connect to NFS\n"); return;
    }

    /* NFSPROC_NULL ping. */
    if (nfs_null(&nfs_c) == 0) printf("  NFS NULL: OK\n");

    /* STATFS on root. */
    uint32_t tsize = 0;
    if (nfs_statfs(&nfs_c, &fhs.directory, &tsize) == 0)
        printf("  STATFS: tsize=%u\n", tsize);

    /* READDIR root — paginated. */
    printf("  Root directory:\n");
    list_dir(&nfs_c, &fhs.directory, "    ");

    /* Walk root entries: LOOKUP each 1-letter name and list its contents. */
    const char *top_dirs[] = { "C", "P", "S", "K", NULL };
    for (int i = 0; top_dirs[i]; i++) {
        fhandle_t sub_fh;
        fattr_t   sub_attr;
        if (lookup_and_show(&nfs_c, &fhs.directory, top_dirs[i], "  ", &sub_fh, &sub_attr) == 0
                && sub_attr.type == NFDIR) {
            printf("  Contents of '%s':\n", top_dirs[i]);
            list_dir(&nfs_c, &sub_fh, "    ");
        }
    }

    /* Walk PIONEER/ and try all known Pioneer subdirectory names. */
    static const char *known_pioneer_dirs[] = {
        "rekordbox", "USBANLZ", "DevLink", "METADATA",
        "MASTERDB", "ContentsDB", "ANLZ", "MyTag", NULL
    };
    static const char *known_rb_files[] = {
        "export.pdb", "exportExt.pdb", NULL
    };

    fhandle_t pioneer_fh, rb_fh, pdb_fh;
    fattr_t   pioneer_attr, rb_attr, pdb_attr;

    printf("  Exploring PIONEER/rekordbox ...\n");
    if (nfs_path_lookup(&nfs_c, &fhs.directory, "PIONEER", &pioneer_fh, &pioneer_attr) == 0) {
        printf("  PIONEER/ raw READDIR:\n");
        list_dir(&nfs_c, &pioneer_fh, "    ");
        printf("  PIONEER/ known-name lookups:\n");
        for (int i = 0; known_pioneer_dirs[i]; i++) {
            fhandle_t sub_fh; fattr_t sub_attr;
            lookup_and_show(&nfs_c, &pioneer_fh, known_pioneer_dirs[i], "    ", &sub_fh, &sub_attr);
        }

        if (nfs_path_lookup(&nfs_c, &fhs.directory, "PIONEER/rekordbox", &rb_fh, &rb_attr) == 0) {
            printf("  rekordbox/ raw READDIR:\n");
            list_dir(&nfs_c, &rb_fh, "    ");
            printf("  rekordbox/ known-name lookups:\n");
            for (int i = 0; known_rb_files[i]; i++) {
                fhandle_t f_fh; fattr_t f_attr;
                lookup_and_show(&nfs_c, &rb_fh, known_rb_files[i], "    ", &f_fh, &f_attr);
            }
        }

        /* Try USBANLZ first directory level. */
        fhandle_t anlz_fh; fattr_t anlz_attr;
        if (nfs_path_lookup(&nfs_c, &fhs.directory, "PIONEER/USBANLZ", &anlz_fh, &anlz_attr) == 0) {
            printf("  USBANLZ/ raw READDIR (first page):\n");
            list_dir(&nfs_c, &anlz_fh, "    ");

            /* Probe subdirectory naming patterns. */
            static const char *anlz_guesses[] = {
                "P001", "P0001", "0001", "00000001", "P001/00000001",
                "P001/00000001/ANLZ0000.DAT", NULL
            };
            printf("  USBANLZ/ subdirectory probes:\n");
            for (int i = 0; anlz_guesses[i]; i++) {
                fhandle_t sub_fh; fattr_t sub_attr;
                lookup_and_show(&nfs_c, &anlz_fh, anlz_guesses[i], "    ", &sub_fh, &sub_attr);
            }
        }
    }

    /* Walk PIONEER/rekordbox/export.pdb. */
    printf("  Looking up PIONEER/rekordbox/export.pdb ...\n");
    int rc = nfs_path_lookup(&nfs_c, &fhs.directory,
                             "PIONEER/rekordbox/export.pdb",
                             &pdb_fh, &pdb_attr);
    if (rc < 0) {
        fprintf(stderr, "  LOOKUP transport error\n");
        rpc_client_destroy(&nfs_c);
        return;
    }
    if (rc > 0) {
        fprintf(stderr, "  LOOKUP NFS error %d\n", rc);
        rpc_client_destroy(&nfs_c);
        return;
    }
    printf("  Found export.pdb: %u bytes, mtime=%u\n",
           pdb_attr.size, pdb_attr.mtime_sec);

    if (fetch_pdb_path) {
        struct timespec t0;
        clock_gettime(CLOCK_MONOTONIC, &t0);
        fetch_pdb(&nfs_c, &pdb_fh, &pdb_attr, fetch_pdb_path);
        printf("  Fetch completed in %ld ms\n", ms_elapsed(&t0));
    }

    if (fetch_ext_pdb_path) {
        fhandle_t ext_fh; fattr_t ext_attr;
        int erc = nfs_path_lookup(&nfs_c, &fhs.directory,
                                  "PIONEER/rekordbox/exportExt.pdb",
                                  &ext_fh, &ext_attr);
        if (erc == 0) {
            printf("  Fetching exportExt.pdb (%u bytes) ...\n", ext_attr.size);
            struct timespec t0;
            clock_gettime(CLOCK_MONOTONIC, &t0);
            fetch_pdb(&nfs_c, &ext_fh, &ext_attr, fetch_ext_pdb_path);
            printf("  exportExt.pdb fetch completed in %ld ms\n", ms_elapsed(&t0));
        } else {
            fprintf(stderr, "  exportExt.pdb LOOKUP failed: %d\n", erc);
        }
    }

    rpc_client_destroy(&nfs_c);
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr,
            "Usage: xdj-explore <ip> [--fetch-pdb <out.pdb>] [--verbose]\n"
            "                   [--mount-port PORT] [--nfs-port PORT]\n");
        return 1;
    }

    const char *host              = argv[1];
    const char *fetch_pdb_path    = NULL;
    const char *fetch_ext_pdb_path = NULL;
    uint16_t    mount_port        = 0;
    uint16_t    nfs_port          = 0;

    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--fetch-pdb") == 0 && i + 1 < argc) {
            fetch_pdb_path = argv[++i];
        } else if (strcmp(argv[i], "--fetch-ext-pdb") == 0 && i + 1 < argc) {
            fetch_ext_pdb_path = argv[++i];
        } else if (strcmp(argv[i], "--verbose") == 0) {
            verbose = 1;
        } else if (strcmp(argv[i], "--mount-port") == 0 && i + 1 < argc) {
            mount_port = (uint16_t)atoi(argv[++i]);
        } else if (strcmp(argv[i], "--nfs-port") == 0 && i + 1 < argc) {
            nfs_port = (uint16_t)atoi(argv[++i]);
        }
    }

    struct timespec t_start;
    clock_gettime(CLOCK_MONOTONIC, &t_start);

    /* Discover ports via portmapper unless overridden. */
    if (mount_port == 0) {
        printf("Querying portmapper for mountd port ...\n");
        if (portmap_getport(host, MOUNT_PROG, MOUNT_VERS, &mount_port) != 0) {
            fprintf(stderr, "Cannot find mountd via portmapper on %s\n", host);
            return 1;
        }
        printf("  mountd port: %u\n", mount_port);
    } else {
        printf("Using fixed mount-port: %u\n", mount_port);
    }

    if (nfs_port == 0) {
        printf("Querying portmapper for NFS port ...\n");
        if (nfs_getport(host, &nfs_port) != 0) {
            fprintf(stderr, "Cannot find NFS via portmapper on %s (trying 2049)\n", host);
            nfs_port = 2049;
        }
        printf("  NFS port: %u\n", nfs_port);
    } else {
        printf("Using fixed nfs-port: %u\n", nfs_port);
    }

    /* Get exports. */
    rpc_client_t mount_c;
    if (rpc_client_init(&mount_c, host, mount_port, MOUNT_PROG, MOUNT_VERS, 3000) < 0) {
        fprintf(stderr, "Cannot connect to mountd at %s:%u\n", host, mount_port);
        return 1;
    }

    printf("Fetching export list ...\n");
    export_entry_t *exports = NULL;
    if (mount_export(&mount_c, &exports) < 0) {
        fprintf(stderr, "EXPORT RPC failed\n");
        rpc_client_destroy(&mount_c);
        return 1;
    }
    rpc_client_destroy(&mount_c);

    if (!exports) {
        printf("No exports found.\n");
        return 0;
    }

    for (export_entry_t *e = exports; e; e = e->next)
        explore_export(host, e->filesystem.val, e->filesystem.len,
                       mount_port, nfs_port, fetch_pdb_path, fetch_ext_pdb_path);

    mount_export_free(exports);

    printf("\nTotal time: %ld ms\n", ms_elapsed(&t_start));
    return 0;
}
