#include <rpc/rpc.h>
#include <string.h>
#include <iconv.h>

#include "nfs.h"
#include "mount.h"

/**
 * Have a look inside the XDJ's nfs server.
 * Does not wrok, mount gets 13 NFSERR_ACCES.
 */

void vdj_iterate_exports(char *server);


static char*
fromutf16(size_t len, char* src)
{
    size_t dest_len = (len * 2) + 1;

    char* dest = calloc(1, dest_len);
    char* d = dest; // iconv messes with dest*
    iconv_t conv = iconv_open("UTF-8", "UTF-16");
    iconv(conv, &src, &len, &dest, &dest_len);
    iconv_close(conv);
    return d;
}

static char*
toutf16(size_t len, char* src)
{
    size_t dest_len = (len * 2) + 1;

    char* dest = calloc(1, dest_len);
    char* d = dest; // iconv messes with dest*
    iconv_t conv = iconv_open("UTF-16", "UTF-8");
    iconv(conv, &src, &len, &dest, &dest_len);
    iconv_close(conv);
    return d;
}


int main(int argc, char* argv[])
{
    if (argc > 1) {
        vdj_iterate_exports(argv[1]);
        return 0;
    }
}

void
vdj_iterate_exports(char *server)
{
    CLIENT* client;
    FHandle* root;
    char* dir;

    client = clnt_create(server, MOUNTPROG, MOUNTVERS, "udp");
    if (client == NULL) {
        fprintf(stderr, "error: could not connect to server.\n");
        return;
    }


    ExportListRes* exports = mountproc_export_1(NULL, client);

    if (exports) {
        ExportList* export = exports->next;
        while (export) {

            dir = fromutf16(export->fileSystem.DirPath_len, export->fileSystem.DirPath_val);
            printf("  %s\n", dir);
            FHStatus* status = mountproc_mnt_1(&export->fileSystem, client);
            if (status->status == NFS_OK) {
                root = &status->FHStatus_u.directory;
            } else {
                printf("BORK: %i\n", status->status);
                return;
            }
            printf("Mounted %s\n", dir);

            free(dir);

            ReadDirArgs args;
            memset(&args, 0, sizeof(ReadDirArgs));
            memcpy(&args.dir, root, FHSIZE);
            args.count = 1;
            ReadDirRes* dir_res = nfsproc_readdir_2(&args, client);
            if (dir_res == NULL) {
                clnt_perror(client, "call failed"); // prints call failed: RPC: Procedure unavailable
            }
            else if (dir_res->status != NFS_OK) {
                fprintf(stderr, "nfsproc_readdir_2 error: %i\n", dir_res->status);
                clnt_perror(client, "call failed");
            }
            else {
                Entry *entry = dir_res->ReadDirRes_u.readdirok.entries;
                printf("%s dirs:\n", server);
                while (entry) {

                    printf("  %s\n", fromutf16(entry->name.Filename_len, entry->name.Filename_val));

                    entry = entry->next;
                }
            }

            export = export->next;
        }
    }

    clnt_destroy(client);

}