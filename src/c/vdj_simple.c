
#include "vdj.h"
#include "vdj_simple.h"
#include "vdj_discovery.h"

/**
 * The idea of this code is that it manages all the VDJ messages for you and you just have to 
 * implement functional handlers.
 */

static void handle_discovery_packet(vdj_t* v, unsigned char* packet, uint16_t length);
static void handle_update_packet(vdj_t* v, unsigned char* packet, uint16_t length);
static void handle_broadcast_packet(vdj_t* v, unsigned char* packet, uint16_t length);

static vdj_ext_client* client;
static unsigned char id_map[VDJ_MAX_BACKLINE];
static int next_slot = 1;

int
vdj_init_simple(uint32_t flags, vdj_ext_client* c)
{
    /*
     * init a vcdj
     */
    vdj_t* v = vdj_init_iface(NULL, flags);
    if (v == NULL) {
        fprintf(stderr, "error: creating virtual cdj\n");
        return CDJ_ERROR;
    }

    /*
     * open all the networks sockets
     */
    if (vdj_open_sockets(v) != CDJ_OK) {
        fprintf(stderr, "error: failed to open sockets\n");
        vdj_destroy(v);
        return CDJ_ERROR;
    }

    // join the network, and fetch a player ID
    if ( vdj_exec_discovery(v) != CDJ_OK) {
        fprintf(stderr, "error: discovery\n");
        vdj_destroy(v);
        return CDJ_ERROR;
    }

    // as long as we send keepalive packets we will get status packets
    if ( vdj_init_managed_discovery_thread(v, handle_discovery_packet) != CDJ_OK ) {
        fprintf(stderr, "error: init keepalive thread\n");
        vdj_destroy(v);
        return CDJ_ERROR;
    }

    // because we are on the network we should get update from other CDJs
    if ( vdj_init_managed_update_thread(v, handle_update_packet) != CDJ_OK ) {
        fprintf(stderr, "error: init update thread\n");
        vdj_destroy(v);
        return CDJ_ERROR;
    }

    // everyone gets beats
    if ( vdj_init_broadcast_thread(v, handle_broadcast_packet) != CDJ_OK ) {
        fprintf(stderr, "error: init broadcast thread\n");
        vdj_destroy(v);
        return CDJ_ERROR;
    }

    client = c;
    c->v = v;
    return CDJ_OK;
}


static void
handle_discovery_packet(vdj_t* v, unsigned char* packet, uint16_t length)
{
    cdj_discovery_packet_t* d_pkt;
    vdj_link_member_t* m;

    if ( cdj_packet_type(packet, length) == CDJ_KEEP_ALIVE ) {

        d_pkt = cdj_new_discovery_packet(packet, length);
        if (d_pkt) {
            if (! id_map[d_pkt->player_id]) {
                id_map[d_pkt->player_id] = next_slot++;
                m = vdj_get_link_member(v, d_pkt->player_id);
                if (m) {
                    client->new_member(client, m);
                } else {
                    fprintf(stderr, "BUG, member should be created _before_ chained handler gets a discovery packet\n");
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
            client->status(client, cs_pkt);
        }
        free(cs_pkt);
    }
}

static void
handle_broadcast_packet(vdj_t* v, unsigned char* packet, uint16_t length)
{
    cdj_beat_packet_t* b_pkt = cdj_new_beat_packet(packet, length);
    if (b_pkt) {
        if (id_map[b_pkt->player_id]) {
            client->beat(client, b_pkt);
        }
        free(b_pkt);
    }
}
