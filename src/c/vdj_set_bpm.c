
#include <unistd.h>
#include <arpa/inet.h>

#include "cdj.h"
#include "vdj.h"
#include "vdj_net.h"


static void vdj_usage()
{
    printf("set the bpm on any synced CDJs on the network\n");
    printf("usage:  vdj-set-bpm [bpm]\n");
    printf("options:\n");
    printf("    -i - network interface to use, required if pc has more than one\n");
    printf("    -p - player id \n");
    printf("    -h - display this text\n");
    exit(0);
}

/**
 * Virtual CDJ application entry point.
 */
int main(int argc, char *argv[])
{
    unsigned int flags = VDJ_FLAG_DEV_XDJ;
    uint8_t player_id;
    char* iface = NULL;
    float bpm = 0.0;

    // we need to know which interface to use
    // likely to have wifi and LAN for CDJs at home
    // TODO likely not to have DHCPd in a club

    // TODO -f force by sending a message to all CDJs to turn on sync !

    int c;
    while ( ( c = getopt(argc, argv, "p:i:b:hmxc") ) != EOF) {
        switch (c) {
            case 'h':
                vdj_usage();
                break;
            case 'p':
                player_id = atoi(optarg);
                if (player_id < 0xf) flags |= player_id;
                break;
            case 'i':
                iface = optarg;
                break;
        }
    }

    if (optind < argc) {
        bpm = strtof(argv[optind], NULL);
    }
    else vdj_usage();

    /**
     * init the vdj
     */
    vdj_t* v = vdj_init_iface(iface, flags);
    if (v == NULL) {
        fprintf(stderr, "error: creating virtual cdj\n");
        return 1;
    }
    if (bpm) v->bpm = bpm;

    if (vdj_open_sockets(v) != CDJ_OK) {
        fprintf(stderr, "error: failed to open sockets\n");
        vdj_destroy(v);
        return 1;
    }

    if (vdj_exec_discovery(v) != CDJ_OK) {
        fprintf(stderr, "error: cdj initialization\n");
        vdj_destroy(v);
        return 1;
    }

    if ( vdj_init_keepalive_thread(v) != CDJ_OK ) {
        fprintf(stderr, "error: init keepalive thread\n");
        vdj_destroy(v);
        return 1;
    }

    // TODO

    find all devices on the network

    start a bpm loop
    sync that loop to current master
    re-negotiate master
    adjust local bpm loop to new bom
    sign off nicely


    vdj_destroy(v);

    return 0;
}

