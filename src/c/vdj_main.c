
#include <unistd.h>
#include <arpa/inet.h>

#include "cdj.h"
#include "vdj.h"
#include "vdj_net.h"


static void vdj_usage()
{
    printf("options:\n");
    printf("    -i - network interface to use, required if pc has more than one\n");
    printf("    -c - mimic CDJ-1000\n");
    printf("    -x - mimic XDJ-1000\n");
    printf("    -m - mimic DJM\n");
    printf("    -b - bpmm, if set vedj broadcasts beat info\n");
    printf("    -h - display this text\n");
    exit(0);
}

static void vdj_main_discovery_handler(vdj_t* v, unsigned char* packet, int len)
{
    cdj_discovery_packet_t* d_pkt = cdj_new_discovery_packet(packet, len, CDJ_DISCOVERY_PORT); // TODO redundant info
    if ( d_pkt &&
        d_pkt->player_id &&
        v->backline->link_members[d_pkt->player_id]
        ) {
        
        struct sockaddr_in* mip_addr = v->backline->link_members[d_pkt->player_id]->mip_addr;
        if (mip_addr) {
            char ip_s[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &mip_addr->sin_addr.s_addr, ip_s, INET_ADDRSTRLEN);
            printf("link member: %02i %s\n", d_pkt->player_id, ip_s); // TODO model name
        }
    }
}

/**
 * Virtual CDJ application entry point.
 */
int main(int argc, char *argv[])
{
    unsigned int flags = 0;
    unsigned char player_id;
    char* iface = NULL;
    float bpm = 0.0;

    // we need to know which interface to use
    // likely to have wifi and LAN for CDJs at home
    // TODO likely not to have DHCPd in a club
    int c;
    while ( ( c = getopt(argc, argv, "p:i:b:hmxc") ) != EOF) {
        switch (c) {
            case 'h':
                vdj_usage();
                break;
            case 'b':
                bpm = strtof(optarg, NULL);
                break;
            case 'm':
                flags |= VDJ_FLAG_DEV_DJM;
                break;
            case 'x':
                flags |= VDJ_FLAG_DEV_XDJ;
                break;
            case 'c':
                flags |= VDJ_FLAG_DEV_CDJ;
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

    if (vdj_handshake(v) != CDJ_OK) {
        fprintf(stderr, "error: cdj initialization\n");
        vdj_destroy(v);
        return 1;
    }

    if ( vdj_init_keepalive_thread(v) != CDJ_OK ) {
        fprintf(stderr, "error: init keepalive thread\n");
        vdj_destroy(v);
        return 1;
    }

    if ( vdj_init_managed_discovery_thread(v, vdj_main_discovery_handler) != CDJ_OK ) {
        fprintf(stderr, "error: init managed discovery thread\n");
        sleep(1);
        vdj_destroy(v);
        return 1;
    }

    if ( vdj_init_status_thread(v) != CDJ_OK ) {
        fprintf(stderr, "error: init managed discovery thread\n");
        sleep(1);
        vdj_destroy(v);
        return 1;
    }

    while (1) sleep(1);

    vdj_destroy(v);

    return 0;
}

