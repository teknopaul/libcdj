#include <rpc/rpc.h>

#include "nfs.h"
#include "mount.h"

/**
 * Have a look inside the XDJ's nfs server.
 * Does not wrok, mount gets 13 NFSERR_ACCES.
 */

void vdj_iterate_exports(char *server);

int main (int argc, char* argv[])
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

    client = clnt_create(server, MOUNTPROG, MOUNTVERS, "udp");
    if (client == NULL) {
        fprintf(stderr, "error: could not connect to server.\n");
        return;
    }


    DirPath argp;
    argp.DirPath_len = 0;
    argp.DirPath_val = (char *)L"/C/PIONEER/rekordbox/";  // apparently CDJs use utf-16LE??  /B/ SD slot, or /C/ USB slot.
printf("len '%ls' \n", L"/C/");

    FHStatus* status = mountproc_mnt_1(&argp, client);
    if (status->status == NFS_OK) {
        root = &status->FHStatus_u.directory;
    } else {
        printf("BORK: %i\n", status->status);
        return;
    }
    printf("Mounted\n");


    ReadDirArgs args;
    memset(&args, 0, sizeof(ReadDirArgs));
    memcpy(&args.dir, root, FHSIZE);
    args.count = 1;

    ReadDirRes* dir_res = nfsproc_readdir_2(&args, client);
    if (dir_res == (ReadDirRes *) NULL || dir_res->status != NFS_OK) {
        fprintf(stderr, "nfsproc_readdir_2 error: %i\n", dir_res->status);
        clnt_perror (client, "call failed");
    }
    else {
        Entry *entry = dir_res->ReadDirRes_u.readdirok.entries;
        printf("%s dirs:\n", server);
        while (entry) {

            printf("  %s\n", entry->name.Filename_val);

            entry = entry->next;
        }
    }

    clnt_destroy (client);

}