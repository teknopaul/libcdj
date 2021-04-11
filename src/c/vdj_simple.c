#include <stdio.h>


#include "vdj.h"
#include "vdj_simple.h"
#include "vdj_discovery.h"

/**
 * The idea of this code is that it manages all the VDJ messages for you and you just have to 
 * implement functional handlers.
 */

static void discovery_ph(vdj_t* v, cdj_discovery_packet_t* d_pkt);
static void update_ph(vdj_t* v, cdj_cdj_status_packet_t* cs_pkt);
static void beat_ph(vdj_t* v, cdj_beat_packet_t* b_pkt);

static vdj_ext_client* client;
static uint8_t id_map[VDJ_MAX_BACKLINE];
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
    if ( vdj_init_managed_discovery_thread(v, discovery_ph) != CDJ_OK ) {
        fprintf(stderr, "error: init keepalive thread\n");
        vdj_destroy(v);
        return CDJ_ERROR;
    }

    // because we are on the network we should get update from other CDJs
    if ( vdj_init_managed_update_thread(v, update_ph) != CDJ_OK ) {
        fprintf(stderr, "error: init update thread\n");
        vdj_destroy(v);
        return CDJ_ERROR;
    }

    // everyone gets beats
    if ( vdj_init_managed_beat_thread(v, beat_ph) != CDJ_OK ) {
        fprintf(stderr, "error: init beat thread\n");
        vdj_destroy(v);
        return CDJ_ERROR;
    }

    client = c;
    c->v = v;
    return CDJ_OK;
}


static void
discovery_ph(vdj_t* v, cdj_discovery_packet_t* d_pkt)
{
    vdj_link_member_t* m;

    if ( d_pkt->type == CDJ_KEEP_ALIVE ) {

        if (! id_map[d_pkt->player_id]) {
            id_map[d_pkt->player_id] = next_slot++;
            if ( (m = vdj_get_link_member(v, d_pkt->player_id)) ) {
                client->new_member(client, m);
            } else {
                fprintf(stderr, "BUG, member should be created _before_ chained handler gets a discovery packet\n");
            }
        }

    }
}

static void
update_ph(vdj_t* v, cdj_cdj_status_packet_t* cs_pkt)
{
    if (id_map[cs_pkt->player_id]) {
        client->status(client, cs_pkt);
    }
}

static void
beat_ph(vdj_t* v, cdj_beat_packet_t* b_pkt)
{
    if (id_map[b_pkt->player_id]) {
        client->beat(client, b_pkt);
    }
}
