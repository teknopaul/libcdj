#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <signal.h>
#include <errno.h>
#include <unistd.h>
#include <sys/socket.h>

#include "cdj.h"
#include "vdj.h"
#include "vdj_discovery.h"

// N.B. including .c
#include "cdj_mon_tui.c"


/**
 * Monitor app that creates a VDJ an joins the prolink network, this requires an  player_id, n.b. max 4 on the lan.
 *
 * This is an example of using the api where we recieve only interesting packets.
 */

static void discovery_ph(vdj_t* v, cdj_discovery_packet_t* d_pkt);
static void update_ph(vdj_t* v, cdj_cdj_status_packet_t* cs_pkt);

static void signal_exit(int sig)
{
    tui_exit();
    exit(0);
}
static void usage()
{
    printf("options:\n");
    printf("    -i - network interface to use, required if pc has more than one\n");
    printf("    -c - mimic CDJ\n");
    printf("    -x - mimic XDJ\n");
    printf("    -p - specific player number (default is 5)\n");
    printf("    -a - auto assign player number\n");
    printf("    -h - display this text\n");
    exit(0);
}

// posisitons on screen for discovered players, mixers and rekordbox instances
uint8_t id_map[127];
int next_slot = 1;

static void
init_ui(uint8_t player_id, char* model_name, uint32_t ip)
{
    tui_cdj_init(id_map[player_id], player_id, model_name, ip);
}

static void
update_ui(uint8_t player_id, float bpm, char* emojis)
{
    char data[2048];
    snprintf(data, 2047, "%s %06.2fbpm", emojis, bpm);
    tui_cdj_update(id_map[player_id], data);
}

/**
 * VDJ monitor tool
 */
int main (int argc, char* argv[])
{
    char title[128];
    char* iface = NULL;
    uint32_t flags = 0;
    uint8_t player_id;
    memset(id_map, 0, 127);

    int c;
    while ( ( c = getopt(argc, argv, "p:i:chax") ) != EOF) {
        switch (c) {
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
            case 'a':
                flags |= VDJ_FLAG_AUTO_ID;
                break;
            case 'h':
                usage();
                break;
            case 'i':
                iface = optarg;
                break;
        }
    }

    /*
     * init a vcdj
     */
    vdj_t* v = vdj_init_iface(iface, flags);
    if (v == NULL) {
        fprintf(stderr, "error: creating virtual cdj\n");
        return 1;
    }

    /*
     * open all the networks sockets
     */
    if (vdj_open_sockets(v) != CDJ_OK) {
        fprintf(stderr, "error: failed to open sockets\n");
        vdj_destroy(v);
        return 1;
    }

    // join the network, and fetch a plyer ID
    if ( vdj_exec_discovery(v) != CDJ_OK) {
        fprintf(stderr, "error: discovery\n");
        vdj_destroy(v);
        return 1;
    }

    // as long as we send keepalive packets we will get status packets
    if ( vdj_init_managed_discovery_thread(v, discovery_ph) != CDJ_OK ) {
        fprintf(stderr, "error: init keepalive thread\n");
        vdj_destroy(v);
        return 1;
    }

    // because we are on the network we should get update from other CDJs
    if ( vdj_init_managed_update_thread(v, update_ph) != CDJ_OK ) {
        fprintf(stderr, "error: init update thread\n");
        vdj_destroy(v);
        return 1;
    }

    signal(SIGINT, signal_exit);
    tui_init(8);
    snprintf(title, 127, "VDJ monitor [%02i]", v->player_id);
    tui_set_window_title(title);

    while (1) sleep(1);

    return 0;
}


static void
discovery_ph(vdj_t* v, cdj_discovery_packet_t* d_pkt)
{
    if ( d_pkt->type == CDJ_KEEP_ALIVE ) {
        int slot = id_map[d_pkt->player_id];
        //tui_set_cursor_pos(0, 0);
        //printf("slot: %i pid=%i pid=%i", slot, d_pkt->player_id, packet[0x24]);
        if (slot == 0) {
            id_map[d_pkt->player_id] = next_slot;
            slot = next_slot++;
            init_ui(d_pkt->player_id, cdj_discovery_model(d_pkt), d_pkt->ip);
        }
    }
}

static void
update_ph(vdj_t* v, cdj_cdj_status_packet_t* cs_pkt)
{
    char* emojis;

    if (id_map[cs_pkt->player_id]) {
        if ( (emojis = cdj_flags_to_emoji(cs_pkt->flags)) ) {
            update_ui(cs_pkt->player_id, cs_pkt->bpm, emojis);
            free(emojis);
        }
    }
}

