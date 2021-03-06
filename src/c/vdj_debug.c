#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <sys/socket.h>
#include <sys/prctl.h>
#include <arpa/inet.h>
#include <pthread.h>

#include "cdj.h"
#include "vdj.h"
#include "vdj_net.h"
#include "vdj_discovery.h"

/**
 * Debug tool, carefull this code does not behave like a well mannered CDJ, it does not necessarily maintain the 
 * member count for example so XDJs will always complain about its prescence on the network in the form of id_use packets.
 */

static void handle_discovery_datagram(vdj_t* v, uint8_t* packet, uint16_t len);
static void handle_update_datagram(uint8_t* packet, uint16_t len);
static void handle_beat_datagram(uint8_t* packet, uint16_t len);
static void* vdj_debug_discoverys(void* v);
static void* vdj_debug_updates(void* v);
static void* vdj_debug_beats(void* v);

static void usage()
{
    printf("options:\n");
    printf("    -i - network interface to use, required if pc has more than one\n");
    printf("    -d - debug discovery frames on port 50000\n");
    printf("    -u - debug update frames on port 50002\n");
    printf("    -b - debug beat frames on port 50001\n");
    printf("    -k - just send keepalives\n");
    printf("    -c - mimic CDJ\n");
    printf("    -x - mimic XDJ\n");
    printf("    -p - specific player number (default is 5)\n");
    printf("    -a - auto assign player number\n");
    printf("    -s - suppress discovery phase\n");
    printf("    -S - suppress sending status updates\n");
    printf("    -K - suppress sending keepalives\n");
    printf("    -h - display this text\n");
    exit(0);
}

/**
 * Virtual CDJ application entry point.
 */
int main (int argc, char* argv[])
{
    uint32_t flags = 0;
    uint8_t player_id;
    uint8_t debug_discovery = 0;
    uint8_t debug_update = 0;
    uint8_t debug_beat = 0;
    uint8_t just_keepalives = 0;
    uint8_t suppress_discovery = 0;
    uint8_t suppress_keepalive = 0;
    uint8_t suppress_status = 0;
    char* iface = NULL;
    pthread_t thread_id;

    // we need to know which interface to use
    // likely to have wifi and LAN for CDJs at home
    // TODO likely not to have DHCPd in a club
    int c;
    while ( ( c = getopt(argc, argv, "p:i:dubkKShmascx") ) != EOF) {
        switch (c) {
            case 'h':
                usage();
                break;
            case 'd':
                debug_discovery = 1;
                break;
            case 'u':
                debug_update = 1;
                break;
            case 'b':
                debug_beat = 1;
                break;
            case 'k':
                just_keepalives = 1;
                break;
            case 's':
                suppress_discovery = 1;
                break;
            case 'K':
                suppress_keepalive = 1;
                break;
            case 'S':
                suppress_status = 1;
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
            case 'a':
                flags |= VDJ_FLAG_AUTO_ID;
                break;
            case 'i':
                iface = optarg;
                break;
        }
    }

    /*
     * init the vcdj
     */
    vdj_t* v = vdj_init_iface(iface, flags);
    if (v == NULL) {
        fprintf(stderr, "error: creating virtual cdj\n");
        return 1;
    }

    if (vdj_open_sockets(v) != CDJ_OK) {
        fprintf(stderr, "error: failed to open sockets\n");
        vdj_destroy(v);
        return 1;
    }

    if (debug_update) {
        pthread_create(&thread_id, NULL, vdj_debug_updates, v);
    }

    if (debug_beat) {
        pthread_create(&thread_id, NULL, vdj_debug_beats, v);
    }

    if (debug_discovery) {
        pthread_create(&thread_id, NULL, vdj_debug_discoverys, v);
    }

    sleep(2);

    //printf("\nEXEC DISCOVERY INITIAL PLAYER ID: %i\n\n", v->player_id);

    if ( ! suppress_discovery && vdj_exec_discovery(v) != CDJ_OK) {
        fprintf(stderr, "error: discovery\n");
        vdj_destroy(v);
        return 1;
    }

    //printf("\nFINISHED DISCOVERY FINAL PLAYER ID: %i\n\n", v->player_id);

    if ( ! suppress_keepalive && vdj_init_keepalive_thread(v) != CDJ_OK ) {
        fprintf(stderr, "error: init keepalive thread\n");
        vdj_destroy(v);
        return 1;
    }

    if ( ! suppress_status && vdj_init_status_thread(v) != CDJ_OK ) {
        fprintf(stderr, "error: init status thread\n");
        vdj_destroy(v);
        return 1;
    }

    if (debug_discovery | debug_update | debug_beat | just_keepalives) {
        //printf("main() loop\n");
        while (1) sleep(1);
    }

    else {
        fprintf(stderr, "nothing to do, try one of (-d -b -u -k)\n");
        usage();
    }

    return 0;
}

/**
 * Take a VirtualCDJ and dump info from other devices to stdout
 */
static void*
vdj_debug_discoverys(void* arg)
{
    int socket;
    ssize_t len;
    uint8_t packet[1500];
    vdj_t* v = arg;

    socket = v->discovery_socket_fd;
    fprintf(stderr, "vdj_debug_discoverys\n");

    while (1) {
        len = recv(socket, packet, 1500, 0);
        if (len == -1) {
            fprintf(stderr, "socket read error: %s", strerror(errno));
            return NULL;
        } else {
            handle_discovery_datagram(v, packet, len);
        }
    }
}

static void
handle_discovery_datagram(vdj_t* v, uint8_t* packet, uint16_t len)
{
    uint8_t type = cdj_packet_type(packet, len);
    uint8_t sub_type;
    char* model;
    cdj_discovery_packet_t* d_pkt;

    switch (type)
    {
        
        case CDJ_DISCOVERY:
        case CDJ_STAGE1_DISCOVERY:
        case CDJ_ID_USE_REQ:
        case CDJ_ID_USE_RESP:
        case CDJ_ID_SET_REQ:
        case CDJ_KEEP_ALIVE: {

            if ( (d_pkt = cdj_new_discovery_packet(packet, len)) ) {

                sub_type = cdj_discovery_sub_type(d_pkt);

                printf("%li [%-20s] %s: player_id=%i, ip=%i.%i.%i.%i, mac=%02x:%02x:%02x:%02x:%02x:%02x\n",
                    time(NULL),
                    cdj_discovery_model(d_pkt),
                    cdj_type_to_string(CDJ_DISCOVERY_PORT, type, sub_type),
                    d_pkt->player_id,
                    (uint8_t) (d_pkt->ip >> 24),
                    (uint8_t) (d_pkt->ip >> 16),
                    (uint8_t) (d_pkt->ip >> 8),
                    (uint8_t) d_pkt->ip,
                    d_pkt->mac[0],
                    d_pkt->mac[1],
                    d_pkt->mac[2],
                    d_pkt->mac[3],
                    d_pkt->mac[4],
                    d_pkt->mac[5]
                    );
                free(d_pkt);
            }
            cdj_print_packet(packet, len, CDJ_DISCOVERY_PORT);
            break;
        }

        default : {
            printf("%li [%-20s] NEW PACKET TYPE=%02i:\n",
                time(NULL),
                model = cdj_model_name(packet, len, CDJ_DISCOVERY_PORT),
                type);
            free(model);
            cdj_print_packet(packet, len, CDJ_DISCOVERY_PORT);
        }
    }

}



static void*
vdj_debug_beats(void* arg)
{
    int socket;
    ssize_t len;
    uint8_t packet[1500];
    vdj_t* v = arg;

    socket = v->beat_socket_fd;
    fprintf(stderr, "vdj_debug_beats\n");

    while (1) {
        len = recv(socket, packet, 1500, 0);
        if (len == -1) {
            fprintf(stderr, "socket read error: %s", strerror(errno));
            return NULL;
        } else {
            handle_beat_datagram(packet, len);
        }
    }
}

static void
handle_beat_datagram(uint8_t* packet, uint16_t len)
{
    cdj_beat_packet_t* b_pkt;

    uint16_t type = cdj_packet_type(packet, len);
    if (type == CDJ_BEAT) {
        if ( (b_pkt = cdj_new_beat_packet(packet, len)) ) {

            printf("%s: %-10s [%i] beat=%i, %06.2fbpm\n", 
                cdj_type_to_string(CDJ_BEAT_PORT, type, 0),
                cdj_beat_model(b_pkt),
                b_pkt->player_id, 
                b_pkt->bar_pos, b_pkt->bpm);
            free(b_pkt);
        }
    }

    cdj_print_packet(packet, len, CDJ_BEAT_PORT);
}


/**
 * Take a VirtualCDJ and dump info from other devices to stdout
 */
static void*
vdj_debug_updates(void* arg)
{
    int socket;
    ssize_t len;
    uint8_t packet[1500];
    vdj_t* v = arg;

    socket = v->update_socket_fd;
    fprintf(stderr, "vdj_debug_updates\n");

    while (1) {
        len = recv(socket, packet, 1500, 0);
        if (len == -1) {
            fprintf(stderr, "socket read error: %s", strerror(errno));
            return NULL;
        } else {
            handle_update_datagram(packet, len);
        }
    }
}

static void
handle_update_datagram(uint8_t* packet, uint16_t len)
{
    cdj_cdj_status_packet_t* cs_pkt;
    uint8_t type = cdj_packet_type(packet, len);
    char* flags_s;

    switch (type)
    {
        case CDJ_STATUS : {
            if ( (cs_pkt = cdj_new_cdj_status_packet(packet, len)) ) {

                printf("%s: %-10s [%02i] %06.2fbpm state=%s time=%li\n",
                    cdj_type_to_string(CDJ_UPDATE_PORT, type, 0),
                    cdj_status_model(cs_pkt),
                    cs_pkt->player_id,
                    cs_pkt->bpm,
                    flags_s = cdj_flags_to_chars(cs_pkt->flags),
                    time(NULL)
                    );
                if (flags_s) free(flags_s);
                free(cs_pkt);
            }
            cdj_print_packet(packet, len, CDJ_UPDATE_PORT);
            break;
        }
        default : {
            cdj_print_packet(packet, len, CDJ_UPDATE_PORT);
        }
    }

}
