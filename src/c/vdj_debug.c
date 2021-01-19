#include <errno.h>
#include <unistd.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/prctl.h>

#include "cdj.h"
#include "vdj.h"
#include "vdj_net.h"

static void handle_discovery_datagram(unsigned char* packet, ssize_t len);
static void handle_update_datagram(unsigned char* packet, ssize_t len);
static void handle_broadcast_datagram(unsigned char* packet, ssize_t len);
static int vdj_debug_discoverys(vdj_t* v);
static int vdj_debug_updates(vdj_t* v);
static int vdj_debug_broadcasts(vdj_t* v);

static void usage()
{
    printf("options:\n");
    printf("    -i - network interface to use, required if pc has more than one\n");
    printf("    -d - debug discovery frames on port 50000\n");
    printf("    -b - debug broadcast frames on port 50001\n");
    printf("    -u - debug update frames on port 50002\n");
    printf("    -k - just send keepalives\n");
    printf("    -m - mimic djm\n");
    printf("    -p - specific player number (default is 5)\n");
    printf("    -h - display this text\n");
    exit(0);
}

/**
 * Virtual CDJ application entry point.
 */
int main (int argc, char* argv[])
{
    unsigned int flags = 0;
    unsigned char player_id;
    unsigned char debug_discovery = 0;
    unsigned char debug_update = 0;
    unsigned char debug_broadcast = 0;
    unsigned char just_keepalives = 0;
    char* iface = NULL;

    // we need to know which interface to use
    // likely to have wifi and LAN for CDJs at home
    // TODO likely not to have DHCPd in a club
    int c;
    while ( ( c = getopt(argc, argv, "i:dkhubmp:") ) != EOF) {
        switch (c) {
            case 'h':
                usage();
                break;
            case 'd':
                debug_discovery = 1;
                break;
            case 'b':
                debug_broadcast = 1;
                break;
            case 'u':
                debug_update = 1;
                break;
            case 'k':
                just_keepalives = 1;
                break;
            case 'm':
                flags |= VDJ_FLAG_DEV_DJM;
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

    if ( vdj_exec_discovery(v) != CDJ_OK) {
        fprintf(stderr, "error: discovery\n");
        vdj_destroy(v);
    }

    if ( vdj_init_keepalive_thread(v) != CDJ_OK ) {
        fprintf(stderr, "error: init keepalive thread\n");
        usage();
    }


    if (debug_update) {
        vdj_debug_updates(v);
    }

    else if (debug_broadcast) {
        vdj_debug_broadcasts(v);
    }

    else if (debug_discovery) {
        vdj_debug_discoverys(v);
    }

    else if (just_keepalives) {
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
static int
vdj_debug_discoverys(vdj_t* v)
{
    int socket;
    ssize_t len;
    unsigned char packet[1500];

    socket = v->discovery_socket_fd;

    while (1) {
        len = recv(socket, packet, 1500, 0);
        if (len == -1) {
            fprintf(stderr, "socket read error: %s", strerror(errno));
            return CDJ_ERROR;
        } else {
            handle_discovery_datagram(packet, len);
        }
    }
}

static void
handle_discovery_datagram(unsigned char* packet, ssize_t len)
{
    int type = cdj_packet_type(packet, len);
    cdj_discovery_packet_t* a_pkt;

    switch (type)
    {
        
        case CDJ_PLAYER_NUM_DISCOVERY : {

            a_pkt = cdj_new_discovery_packet(packet, len, CDJ_DISCOVERY_PORT);
            printf("[%-20s] %s: device_type=%i, player_id=%i\n",
                cdj_model_name(packet, len, CDJ_DISCOVERY_PORT),
                cdj_type_to_string(CDJ_DISCOVERY_PORT, type),
                a_pkt->device_type, a_pkt->player_id);
            break;
        }
        
        case CDJ_DISCOVERY :
        case CDJ_KEEP_ALIVE : {

            a_pkt = cdj_new_discovery_packet(packet, len, CDJ_DISCOVERY_PORT);

            printf("[%-20s] %s: device_type=%i, player_id=%i, ip=%i.%i.%i.%i, mac=%02x:%02x:%02x:%02x:%02x:%02x\n",
                cdj_model_name(packet, len, CDJ_DISCOVERY_PORT),
                cdj_type_to_string(CDJ_DISCOVERY_PORT, type),
                a_pkt->device_type, a_pkt->player_id, 
                (unsigned char) (a_pkt->ip >> 24),
                (unsigned char) (a_pkt->ip >> 16),
                (unsigned char) (a_pkt->ip >> 8),
                (unsigned char) a_pkt->ip,
                a_pkt->mac[0],
                a_pkt->mac[1],
                a_pkt->mac[2],
                a_pkt->mac[3],
                a_pkt->mac[4],
                a_pkt->mac[5]
                );
            cdj_print_packet(packet, len, CDJ_DISCOVERY_PORT);
            break;
        }
        default : {
            cdj_print_packet(packet, len, CDJ_DISCOVERY_PORT);
        }
    }

}



static int
vdj_debug_broadcasts(vdj_t* v)
{
    int socket;
    ssize_t len;
    unsigned char packet[1500];

    socket = v->broadcast_socket_fd;

    while (1) {
        len = recv(socket, packet, 1500, 0);
        if (len == -1) {
            fprintf(stderr, "socket read error: %s", strerror(errno));
            return CDJ_ERROR;
        } else {
            handle_broadcast_datagram(packet, len);
        }
    }
}

static void
handle_broadcast_datagram(unsigned char* packet, ssize_t len)
{
    cdj_print_packet(packet, len, CDJ_BROADCAST_PORT);

    int type = cdj_packet_type(packet, len);
    if (type == CDJ_BEAT) {
        cdj_beat_packet_t* b_pkt = cdj_new_beat_packet(packet, len, CDJ_BROADCAST_PORT);
        printf("[%-20s] %s: player_id=%i, beat=%i, bpm=%f\n", 
            cdj_model_name(packet, len, CDJ_BROADCAST_PORT),
            cdj_type_to_string(CDJ_BROADCAST_PORT, type),
            b_pkt->player_id, b_pkt->bar_pos, b_pkt->bpm);
    }
}


/**
 * Take a VirtualCDJ and dump info from other devices to stdout
 */
static int
vdj_debug_updates(vdj_t* v)
{
    int socket;
    ssize_t len;
    unsigned char packet[1500];

    socket = v->update_socket_fd;

    while (1) {
        len = recv(socket, packet, 1500, 0);
        if (len == -1) {
            fprintf(stderr, "socket read error: %s", strerror(errno));
            return CDJ_ERROR;
        } else {
            handle_update_datagram(packet, len);
        }
    }
}

static void
handle_update_datagram(unsigned char* packet, ssize_t len)
{
    int type = cdj_packet_type(packet, len);

    switch (type)
    {
        case CDJ_STATUS : {
            cdj_cdj_status_packet_t* b_pkt = cdj_new_cdj_status_packet(packet, len, CDJ_UPDATE_PORT);
            printf("[%-20s] %s: player_id=%i, bpm=%f\n",
                cdj_model_name(packet, len, CDJ_UPDATE_PORT),
                cdj_type_to_string(CDJ_UPDATE_PORT, type),
                b_pkt->player_id, b_pkt->bpm);
            //break;
        }
        default : {
            cdj_print_packet(packet, len, CDJ_UPDATE_PORT);
        }
    }

}
