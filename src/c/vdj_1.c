
#include <unistd.h>
#include <arpa/inet.h>

#include "cdj.h"
#include "vdj.h"
#include "vdj_net.h"
#include "vdj_beatout.h"
#include "vdj_discovery.h"
#include "vdj_pselect.h"

/**
 * This app uses a single thread (vdj_pselect.h) for all I/O
 */

static void
vdj_usage()
{
    printf("options:\n");
    printf("    -i - network interface to use, required if pc has more than one\n");
    printf("    -p - player id (use one that no CDJ is currently displaying)\n");
    printf("    -a - auto assign player number\n");
    printf("    -c - mimic CDJ-1000\n");
    printf("    -x - mimic XDJ-1000\n");
    printf("    -b - bpm, if set vdj broadcasts beat info\n");
    printf("    -M - start as master\n");
    printf("    -h - display this text\n");
    exit(0);
}

/**
 * Virtual CDJ application entry point.
 */
int main(int argc, char *argv[])
{
    unsigned int flags = 0;
    uint8_t player_id = 0;
    char* iface = NULL;
    float bpm = 120.0;
    char master = 0;

    // we need to know which interface to use
    // likely to have wifi and LAN for CDJs at home
    // TODO likely not to have DHCPd in a club
    int c;
    while ( ( c = getopt(argc, argv, "p:i:b:hamxcM") ) != EOF) {
        switch (c) {
            case 'h':
                vdj_usage();
                break;
            case 'b':
                bpm = strtof(optarg, NULL);
                break;
            case 'x':
                flags |= VDJ_FLAG_DEV_XDJ;
                break;
            case 'c':
                flags |= VDJ_FLAG_DEV_CDJ;
                break;
            case 'M':
                master = 1;
                break;
            case 'a':
                flags |= VDJ_FLAG_AUTO_ID;
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

    if (player_id == 0) flags |= VDJ_FLAG_AUTO_ID;
    /**
     * init the vdj
     */
    vdj_t* v;
    if ( ! (v = vdj_init_iface(iface, flags)) ) {
        fprintf(stderr, "error: creating virtual cdj\n");
        return 1;
    }
    if (bpm) v->bpm = bpm;
    if (master) v->master = 1;

    if ( vdj_open_sockets(v) != CDJ_OK ) {
        fprintf(stderr, "error: failed to open sockets\n");
        vdj_destroy(v);
        return 1;
    }

    if ( vdj_exec_discovery(v) != CDJ_OK ) {
        fprintf(stderr, "error: cdj initialization\n");
        vdj_destroy(v);
        return 1;
    }

    printf("became link member as player %02i\n", v->player_id);

    if ( vdj_pselect_init(v, NULL, NULL, NULL, NULL, NULL) != CDJ_OK) {
        fprintf(stderr, "error: pselect initialization\n");
        vdj_destroy(v);
        return 1;
    }

    if ( bpm > 0.0 ) {
        if ( vdj_init_beatout_thread(v) != CDJ_OK )  {
            fprintf(stderr, "error: init beatout thread\n");
            vdj_pselect_stop(v);
            usleep(100000);
            vdj_destroy(v);
            return 1;
       }
       vdj_start_beatout_thread(v);
    }


    while (1) sleep(1);

    vdj_destroy(v);

    return 0;
}

