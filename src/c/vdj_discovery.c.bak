
/**
 * Manage initialization and keep-alives and verifying presence of link members.
 * 
 * @author teknopaul
 */

#include <pthread.h>
#include <stdatomic.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <ifaddrs.h>
#include <netpacket/packet.h>
#include <sys/ioctl.h>
#include <net/if.h>

#include "cdj.h"
#include "vdj.h"
#include "vdj_discovery.h"


static unsigned _Atomic vdj_keepalive_running = ATOMIC_VAR_INIT(0);

/**
 * verify the reply is about the current player id  0x24 == v->player_id
 * verify the reply is about the correct packet     0x25 == v->reqid, the id_use packet request id we just send out
 * if not this message is worng or delivered late from a previous attempt and should be ignored
 */
static int
vdj_is_id_in_use_resp(vdj_t* v, cdj_discovery_packet_t* d_pkt)
{
    return cdj_discovery_is_id_in_use(d_pkt, v->player_id, v->reqid);
}

/**
 * check to see if a player sends us a CDJ_ID_USE_RESP message, indicating the player_no we tried is already in use
 * waits 300ms on the unicast 50000 socket fd
 */
static void
vdj_check_for_id_reuse(vdj_t* v)
{
    uint8_t packet[1500];
    ssize_t len;

    if (v->auto_id) {
        // socket timeout set to CDJ_REPLY_WAIT
        len = recv(v->discovery_unicast_socket_fd, packet, 1500, 0);
        if (len > 0) {
            vdj_handle_managed_discovery_unicast_datagram(v, NULL, packet, len);
        } 
        // todo report error?  we expect errno to be EAGAIN or maybe EWOULDBLOCK and dont really care, not much we can do about errors
    } else {
        usleep(300000);
    }
}

static int
vdj_set_discovery_socket_timeouts(vdj_t* v)
{
    struct timeval sleep_time;
    sleep_time.tv_sec = 0;
    sleep_time.tv_usec = CDJ_REPLY_WAIT * 1000; // 300ms
    if ( setsockopt(v->discovery_unicast_socket_fd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&sleep_time, sizeof sleep_time) ) {
        fprintf(stderr, "unable to set discovery_unicast_socket timeout: '%s'\n", strerror(errno));
        return CDJ_ERROR;
    }

    return CDJ_OK;
}

void
vdj_expire_players(vdj_t* v, vdj_expired_h expired_h)
{
    time_t now;
    int i;
    vdj_link_member_t* m;

    // expire gone players
    if (v->backline) {
        now = time(0);
        for (i = 0; i < VDJ_MAX_BACKLINE; i++) {
            if ( (m = v->backline->link_members[i]) ) {
                if ( m->last_keepalive < now - 7 ) { // observed timeout from XDJs
                    // dont free() thread issues, just mark it as gone
                    m->gone = 1;
                    m->active = 0;
                    if (expired_h) expired_h(v, m);
                }
            }
        }
    }
}

/**
 * Send the initialization packets typical for CDJ which "claim" a player number.
 * 
 * This method blocks the thread and occupies v->discovery_unicast_socket_fd if we are doing automatic player_id discovery.
 */
int
vdj_exec_discovery(vdj_t* v)
{
    uint16_t length;
    int rv;

    if ( vdj_set_discovery_socket_timeouts(v) ) {
        return CDJ_ERROR;
    }

    if (v->auto_id) {
        v->player_id = 1;
        vdj_load_player_id(v);
    }

    // CDJ_DISCOVERY
    uint8_t* packet = cdj_create_initial_discovery_packet(&length, v->model);
    if (packet == NULL) return CDJ_ERROR;

    rv = vdj_sendto_discovery(v, packet, length);
    if (rv != CDJ_OK) {
        return CDJ_ERROR;
    }
    usleep(300000);
    vdj_sendto_discovery(v, packet, length);
    usleep(300000);
    vdj_sendto_discovery(v, packet, length);
    usleep(300000);
    free(packet);

    // CDJ_STAGE1_DISCOVERY
    packet = cdj_create_stage1_discovery_packet(&length, v->model, v->mac, 1);
    if (packet == NULL) return CDJ_ERROR;
    
    vdj_sendto_discovery(v, packet, length);
    usleep(300000);
    cdj_inc_stage1_discovery_packet(packet);
    vdj_sendto_discovery(v, packet, length);
    usleep(300000);
    cdj_inc_stage1_discovery_packet(packet);
    vdj_sendto_discovery(v, packet, length);
    usleep(300000);
    free(packet);

    // CDJ_ID_USE_REQ/RESP
    packet = cdj_create_id_use_req_packet(&length, v->model, v->ip, v->mac, v->player_id, v->reqid = 1);
    if (packet == NULL) return CDJ_ERROR;

    vdj_sendto_discovery(v, packet, length);
    vdj_check_for_id_reuse(v);
    cdj_mod_id_use_req_packet_player_id(packet, v->player_id);

    // n = 2
    v->reqid = cdj_inc_id_use_req_packet(packet);
    vdj_sendto_discovery(v, packet, length);
    vdj_check_for_id_reuse(v);
    cdj_mod_id_use_req_packet_player_id(packet, v->player_id);

    // n = 3
    v->reqid = cdj_inc_id_use_req_packet(packet);
    vdj_sendto_discovery(v, packet, length);
    vdj_check_for_id_reuse(v);

    free(packet);

    // CDJ_ID_SET_REQ TODO should listen for collisions and restart, not not sure what we would expect to achieve
    // TODO could listen for OKs packets too
    packet = cdj_create_id_set_req_packet(&length, v->model, v->player_id, v->reqid = 1);
    if (packet == NULL) return CDJ_ERROR;

    vdj_sendto_discovery(v, packet, length);
    usleep(300000);
    v->reqid = cdj_inc_id_set_req_packet(packet);
    vdj_sendto_discovery(v, packet, length);
    usleep(300000);
    v->reqid = cdj_inc_id_set_req_packet(packet);
    vdj_sendto_discovery(v, packet, length);
    usleep(300000);

    v->have_id = 1;

    vdj_save_player_id(v);

    return CDJ_OK;
}



static void*
vdj_managed_discovery_loop(void* arg)
{
    vdj_thread_info* tinfo = arg;
    vdj_discovery_ph discovery_ph = tinfo->handler;
    vdj_t* v = tinfo->v;

    ssize_t len;
    uint8_t packet[1500];

    vdj_keepalive_running = 1;
    while (vdj_keepalive_running) {

        usleep(CDJ_KEEPALIVE_INTERVAL * 1000);
        // read all messages off the queue once per loop
        do {
            len = recv(tinfo->v->discovery_socket_fd, packet, 1500, MSG_DONTWAIT);
            if (len == -1) {
                if (errno == EAGAIN) break;
                fprintf(stderr, "error: socket read '%s'", strerror(errno));
                return NULL;
            } else if (len > 0) {
                if ( ! cdj_validate_header(packet, len) )  {
                    vdj_handle_managed_discovery_datagram(v, discovery_ph, packet, len);
                }
            }
        } while (len > 0);

        vdj_expire_players(v, NULL);

        vdj_send_keepalive(v);

        // drain_unicast
        while ( recv(tinfo->v->discovery_unicast_socket_fd, packet, 1500, MSG_DONTWAIT) > 0 );
    }

    return NULL;
}


void
vdj_stop_managed_discovery_thread(vdj_t* v)
{
    vdj_keepalive_running = 0;
}

int
vdj_init_managed_discovery_thread(vdj_t* v, vdj_discovery_ph discovery_ph)
{
    uint8_t packet[1500];

    if (vdj_keepalive_running) return CDJ_ERROR;
    vdj_keepalive_running = 1;


    // drain_multicast
    while ( recv(v->discovery_socket_fd, packet, 1500, MSG_DONTWAIT) > 0 );

    pthread_t thread_id;
    vdj_thread_info* tinfo = (vdj_thread_info*) calloc(1, sizeof(vdj_thread_info));
    tinfo->v = v;
    tinfo->handler = discovery_ph;
    return pthread_create(&thread_id, NULL, &vdj_managed_discovery_loop, tinfo);
}


// unmanaged, N.B. keepalive message will have wrong member count
static void*
vdj_keepalive_loop(void* arg)
{
    vdj_t* v = arg;

    vdj_keepalive_running = 1;
    while (vdj_keepalive_running) {

        usleep(CDJ_KEEPALIVE_INTERVAL * 1000);

        vdj_send_keepalive(v);

    }

    return NULL;
}

void
vdj_stop_keepalive_thread()
{
    vdj_keepalive_running = 0;
}

int
vdj_init_keepalive_thread(vdj_t* v)
{
    if (vdj_keepalive_running) return CDJ_ERROR;
    vdj_keepalive_running = 1;

    pthread_t thread_id;
    return pthread_create(&thread_id, NULL, &vdj_keepalive_loop, v);
}

// handle discovery of other link members,
// this occupies recv on the discovery_socket_fd

static int
vdj_match_ip(vdj_t* v, unsigned int ip)
{
    return
        ( v->ip[0] == (uint8_t) (ip >> 24) ) &&
        ( v->ip[1] == (uint8_t) (ip >> 16) ) &&
        ( v->ip[2] == (uint8_t) (ip >> 8) ) &&
        ( v->ip[3] == (uint8_t) ip );
}


void
vdj_handle_managed_discovery_datagram(vdj_t* v, vdj_discovery_ph discovery_ph, uint8_t* packet, ssize_t len)
{
    uint16_t length;
    uint8_t* resp;
    struct sockaddr_in* dest;
    vdj_link_member_t* m;
    cdj_discovery_packet_t* d_pkt;

    uint8_t type = cdj_packet_type(packet, len);

//cdj_print_packet(packet, len, CDJ_DISCOVERY_PORT);

    switch (type)
    {
        
        case CDJ_STAGE1_DISCOVERY:
            // never seen a response to this
            break;
        case CDJ_DISCOVERY: {
            // never seen a response to this
            break;
        }
        case CDJ_ID_USE_REQ: {
            // detect id use clashes
            if ( (d_pkt = cdj_new_discovery_packet(packet, len)) ) {

                if (d_pkt->player_id == v->player_id && ! vdj_match_ip(v, d_pkt->ip) ) {
                    //fprintf(stderr, "id in use, sending id_use_resp\n");
                    if ( (resp = cdj_create_id_use_resp_packet(&length, v->model, v->player_id, v->ip)) ) { 
                        if ( (dest = cdj_ip_decode(d_pkt->ip)) ) {
                            dest->sin_port = htons(CDJ_UPDATE_PORT);
                            vdj_sendto_update(v, dest, resp, length);
                            free(dest);
                        }
                        free(resp);
                    }
                }
                if (discovery_ph) discovery_ph(v, d_pkt);
                free(d_pkt);
            }
            break;
        }
        case CDJ_COLLISION: {
            //fprintf(stderr, "id collision alert\n");
            // TODO ought to restart discovery now
            if (discovery_ph) {
                if ( (d_pkt = cdj_new_discovery_packet(packet, len)) ) {
                    discovery_ph(v, d_pkt);
                    free(d_pkt);
                }
            }
            break;
        }
        case CDJ_ID_SET_REQ: {
            // detect unhandled clash, aka collission aka conflict
            /*
            TODO need to filter out packets sent by myself and I cant!! message does not have ip
            if ( (d_pkt = cdj_new_discovery_packet(packet, len)) ) {
                if (d_pkt->player_id == v->player_id && ! vdj_match_ip(v, d_pkt->ip) ) {
                    fprintf(stderr, "id collision: %02i\n", d_pkt->player_id);
                    if ( (resp = cdj_create_id_collision_packet(&length, v->model, v->player_id, v->ip)) ) { 
                        vdj_sendto_discovery(v, resp, length);
                        free(resp);
                    }
                }
                free(d_pkt);
            }
            */
            break;
        }
        case CDJ_KEEP_ALIVE: {
        
            if ( (d_pkt = cdj_new_discovery_packet(packet, len)) ) {

                // ignore messages from self (someone else might want it tho (adj does))
                if (d_pkt->player_id == v->player_id) {
                    if (discovery_ph) {
                        discovery_ph(v, d_pkt);
                        free(d_pkt);
                        break;
                    }
                }

                if (d_pkt->player_id == v->player_id && ! vdj_match_ip(v, d_pkt->ip) ) {
                    //fprintf(stderr, "id collision: %02i\n", d_pkt->player_id);
                    if ( (resp = cdj_create_id_collision_packet(&length, v->model, v->player_id, v->ip)) ) { 
                        vdj_sendto_discovery(v, resp, length);
                        free(resp);
                    }
                }

                // member upsert
                if ( ! (m = vdj_get_link_member(v, d_pkt->player_id)) ) {
                    if ( (m = vdj_new_link_member(v, d_pkt)) ) {
                        if (m->gone) {
                            // its back, update ip in case it changed
                            vdj_update_link_member(m, d_pkt->ip);
                        }
                    }
                }
                if (m) {
                    m->gone = 0;
                    m->active = 1;
                    m->last_keepalive = time(NULL);
                }

                if (discovery_ph) discovery_ph(v, d_pkt);
                free(d_pkt);
            }
            break;
        }
    }

}

/**
 * check to see if a player sends us a CDJ_ID_USE_RESP message, indicating the player_no we tried is already in use.
 * if number is in use, increment player_id for the next try.
 */
void
vdj_handle_managed_discovery_unicast_datagram(vdj_t* v, vdj_discovery_unicast_ph discovery_unicast_ph, uint8_t* packet, ssize_t len)
{
    cdj_discovery_packet_t* d_pkt;
    if ( ! cdj_validate_header(packet, len) ) {
        if ( (d_pkt = cdj_new_discovery_packet(packet, len)) ) {
            if (vdj_is_id_in_use_resp(v, d_pkt)) {
                // fprintf(stderr, "recv() CDJ_ID_USE_RESP, player_id was %i\n", v->player_id);
                v->player_id++;
                if (v->player_id > 4) v->player_id = 1;
            }
            if (discovery_unicast_ph) discovery_unicast_ph(v, d_pkt);
            free(d_pkt);
        }
    }
}