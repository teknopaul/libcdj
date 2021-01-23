
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

static void handle_discovery_packet(vdj_t* v, unsigned char* packet, uint16_t length);
static void handle_update_packet(vdj_t* v, unsigned char* packet, uint16_t length);

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
unsigned char id_map[127];
int next_slot = 1;

static void
init_ui(unsigned char player_id, char* model_name)
{
    tui_cdj_init(id_map[player_id], player_id, model_name);
}

static void
update_ui(unsigned char player_id, float bpm, char* emojis)
{
    char data[2048];
    snprintf(data, 2047, "[%02i] %s %06.2fbpm", player_id, emojis, bpm);
    tui_cdj_update(id_map[player_id], data);
}

/**
 * VDJ monitor tool
 */
int main (int argc, char* argv[])
{
    char title[128];
    char* iface = NULL;
    unsigned int flags = 0;
    unsigned char player_id;
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
    if ( vdj_init_keepalive_thread(v, handle_discovery_packet) != CDJ_OK ) {
        fprintf(stderr, "error: init keepalive thread\n");
        vdj_destroy(v);
        return 1;
    }

    // because we are on the network we should get update from other CDJs
    if ( vdj_init_update_thread(v, handle_update_packet) != CDJ_OK ) {
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
handle_discovery_packet(vdj_t* v, unsigned char* packet, uint16_t length)
{
    cdj_discovery_packet_t* d_pkt;
    char* model;

    if ( cdj_packet_type(packet, length) == CDJ_KEEP_ALIVE ) {

        d_pkt = cdj_new_discovery_packet(packet, length);
        if (d_pkt) {
            int slot = id_map[d_pkt->player_id];
            //tui_set_cursor_pos(0, 0);
            //printf("slot: %i pid=%i pid=%i", slot, d_pkt->player_id, packet[0x24]);
            if (slot == 0) {
                id_map[d_pkt->player_id] = next_slot;
                slot = next_slot++;
                model = cdj_model_name(packet, length, CDJ_DISCOVERY_PORT);
                if (model) {
                    init_ui(d_pkt->player_id, model);
                    free(model);
                }
            }
            free(d_pkt);
        }

    }
}

static void
handle_update_packet(vdj_t* v, unsigned char* packet, uint16_t length)
{
    cdj_cdj_status_packet_t* cs_pkt = cdj_new_cdj_status_packet(packet, length);
    if (cs_pkt) {
        if (id_map[cs_pkt->player_id]) {
            char* emojis = cdj_flags_to_emoji(cs_pkt->flags);
            if (emojis) {
                update_ui(cs_pkt->player_id, cs_pkt->bpm, emojis);
                free(emojis);
            }
            else {

            }
        }
        free(cs_pkt);
    }
}

