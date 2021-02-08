#include <rpc/rpc.h>
#include <string.h>
#include <iconv.h>

#include "mount.h"
#include "nfs.h"

/**
 * Prints the NFS exports on an CDJ player.
 * This just prints "/C/" on my XDJ-1000s
 */

void vdj_list_exports(char *server);

int main (int argc, char* argv[])
{
    if (argc > 1) {
        vdj_list_exports(argv[1]);
        return 0;
    }
}

static char*
conv(size_t len, char* src)
{
    size_t dest_len = (len * 2) + 1;

    char* dest = calloc(1, dest_len);
    char* d = dest; // iconv messes with dest*
    iconv_t conv = iconv_open("UTF-8", "UTF-16");
    iconv(conv, &src, &len, &dest, &dest_len);
    iconv_close(conv);
    return d;
}

void
vdj_list_exports(char *server)
{
    //int i;
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
        while (export) {

            printf("  %s\n", conv(export->fileSystem.DirPath_len, export->fileSystem.DirPath_val));

            export = export->next;
        }
    }

    clnt_destroy(client);

}