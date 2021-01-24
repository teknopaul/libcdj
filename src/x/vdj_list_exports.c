#include <rpc/rpc.h>

#include "mount.h"
#include "nfs.h"

/**
 * Prints the NFS exports on an CDJ player.
 * This just prints "/" on my XDJs
 */

void vdj_list_exports(char *server);

int main (int argc, char* argv[])
{
    if (argc > 1) {
        vdj_list_exports(argv[1]);
        return 0;
    }
}

void
vdj_list_exports(char *server)
{
    CLIENT *client;

    client = clnt_create(server, MOUNTPROG, MOUNTVERS, "udp");
    if (client == NULL) {
        fprintf(stderr, "error: could not connect to server.\n");
        return;
    }

    ExportListRes* exports = mountproc_export_1(NULL, client);

    if (exports) {
        printf("%s exports:\n", server);
        ExportList* export = exports->next;
        while (export)  {

            printf("  sz=%i '%s'\n", export->fileSystem.DirPath_len, export->fileSystem.DirPath_val);

            export = export->next;
        }
    }

    clnt_destroy (client);

}