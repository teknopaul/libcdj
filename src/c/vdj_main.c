#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>

#include "cdj.h"
#include "vdj.h"
#include "vdj_net.h"
#include "vdj_beatout.h"
#include "vdj_discovery.h"

/**
 * This app does nothing other than join the ProLink networks as a player and tries to be
 * as correct as possible in the protocol.
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

static void
vdj_main_discovery_ph(vdj_t* v, cdj_discovery_packet_t* d_pkt)
{
    char ip_s[INET_ADDRSTRLEN];

    if ( d_pkt->player_id && v->backline->link_members[d_pkt->player_id] ) {
        
        struct sockaddr_in* ip_addr = v->backline->link_members[d_pkt->player_id]->ip_addr;
        if (ip_addr) {
            inet_ntop(AF_INET, &ip_addr->sin_addr.s_addr, ip_s, INET_ADDRSTRLEN);
            printf("link member: %02i [%s] %s\n", d_pkt->player_id, cdj_discovery_model(d_pkt), ip_s);
        }
    }
}

static void
vdj_main_update_ph(vdj_t* v, cdj_cdj_status_packet_t* cs_pkt)
{
    if ( cs_pkt->player_id && v->backline->link_members[cs_pkt->player_id] ) {
        if ( cdj_status_counter(cs_pkt) % 8 == 0) {
            printf("link member: %02i alive\n", cs_pkt->player_id);
        }
    }
}

/**
 * Virtual CDJ application entry point.
 */
int main(int argc, char *argv[])
{
    unsigned int flags = 0;
    uint8_t player_id = 0;
    char* iface = NULL;
    float bpm = 0.0;
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

    sleep(1);

    if ( vdj_init_managed_discovery_thread(v, vdj_main_discovery_ph) != CDJ_OK ) {
        fprintf(stderr, "error: init managed discovery thread\n");
        sleep(1);
        vdj_destroy(v);
        return 1;
    }

    // TODO init these before the discovery
    sleep(2);
    if ( vdj_init_managed_update_thread(v, vdj_main_update_ph) != CDJ_OK ) {
        fprintf(stderr, "error: init managed update thread\n");
        sleep(1);
        vdj_destroy(v);
        return 1;
    }

    if ( vdj_init_status_thread(v) != CDJ_OK ) {
        fprintf(stderr, "error: init status thread\n");
        sleep(1);
        vdj_destroy(v);
        return 1;
    }

    if ( bpm > 0.0 ) {
        if ( vdj_init_beatout_thread(v) != CDJ_OK )  {
            fprintf(stderr, "error: init beatout thread\n");
            sleep(1);
            vdj_destroy(v);
            return 1;
       }
       vdj_start_beatout_thread(v);
    }


    while (1) sleep(1);

    vdj_destroy(v);

    return 0;
}

