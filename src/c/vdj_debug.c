#include <errno.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <sys/socket.h>
#include <sys/prctl.h>
#include <arpa/inet.h>

#include "cdj.h"
#include "vdj.h"
#include "vdj_net.h"

static void handle_discovery_datagram(vdj_t* v, unsigned char* packet, ssize_t len);
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
    printf("    -c - mimic CDJ\n");
    printf("    -x - mimic XDJ\n");
    printf("    -p - specific player number (default is 5)\n");
    printf("    -s - suppress sending discovery\n");
    printf("    -K - suppress sending keepalives\n");
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
    unsigned char suppress_discovery = 0;
    unsigned char suppress_keepalive = 0;
    unsigned char suppress_status = 0;
    char* iface = NULL;

    // we need to know which interface to use
    // likely to have wifi and LAN for CDJs at home
    // TODO likely not to have DHCPd in a club
    int c;
    while ( ( c = getopt(argc, argv, "p:i:dkKShubmscx") ) != EOF) {
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

    if ( ! suppress_discovery && vdj_exec_discovery(v) != CDJ_OK) {
        fprintf(stderr, "error: discovery\n");
        vdj_destroy(v);
        return 1;
    }

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
            handle_discovery_datagram(v, packet, len);
        }
    }
}

static void
handle_discovery_datagram(vdj_t* v, unsigned char* packet, ssize_t len)
{
    char* model;
    int type = cdj_packet_type(packet, len);
    cdj_discovery_packet_t* d_pkt;

    switch (type)
    {
        
        case CDJ_DISCOVERY:
        case CDJ_STAGE1_DISCOVERY:
        //case CDJ_FINAL_DISCOVERY:
        case CDJ_KEEP_ALIVE: {

            d_pkt = cdj_new_discovery_packet(packet, len);

            printf("%li [%-20s] %s: player_id=%i, ip=%i.%i.%i.%i, mac=%02x:%02x:%02x:%02x:%02x:%02x\n",
                time(NULL),
                model = cdj_model_name(packet, len, CDJ_DISCOVERY_PORT),
                cdj_type_to_string(CDJ_DISCOVERY_PORT, type),
                d_pkt->player_id, 
                (unsigned char) (d_pkt->ip >> 24),
                (unsigned char) (d_pkt->ip >> 16),
                (unsigned char) (d_pkt->ip >> 8),
                (unsigned char) d_pkt->ip,
                d_pkt->mac[0],
                d_pkt->mac[1],
                d_pkt->mac[2],
                d_pkt->mac[3],
                d_pkt->mac[4],
                d_pkt->mac[5]
                );
            free(model);
            free(d_pkt);
            cdj_print_packet(packet, len, CDJ_DISCOVERY_PORT);
            break;
        }
        case CDJ_STAGE2_DISCOVERY: {

            d_pkt = cdj_new_discovery_packet(packet, len);
            printf("%li [%-20s] %s: player_id=%i, ip=%i.%i.%i.%i\n",
                time(NULL),
                model = cdj_model_name(packet, len, CDJ_DISCOVERY_PORT),
                cdj_type_to_string(CDJ_DISCOVERY_PORT, type),
                d_pkt->player_id,
                (unsigned char) (d_pkt->ip >> 24),
                (unsigned char) (d_pkt->ip >> 16),
                (unsigned char) (d_pkt->ip >> 8),
                (unsigned char) d_pkt->ip
                );
            cdj_print_packet(packet, len, CDJ_DISCOVERY_PORT);

            // does not help  but here is where nwe need to DM the plaer with a solution
            if (d_pkt->player_id == v->player_id && v->ip[3] != (unsigned char) d_pkt->ip ) {
                printf("ID CLASH!! SENDING: IDUSE\n");
                int length;
                unsigned char* packet = cdj_create_id_use_response_packet(&length, 'C', v->player_id, v->ip);
                struct sockaddr_in* dest = cdj_ip_decode(d_pkt->ip);
                dest->sin_port = htons(CDJ_UPDATE_PORT);
                vdj_sendto_update(v, dest, packet, length);
                //vdj_send_keepalive(v);
            }
            



            free(model);
            free(d_pkt);
            break;

        }
        case CDJ_FINAL_DISCOVERY:{

            d_pkt = cdj_new_discovery_packet(packet, len);
            printf("%li [%-20s] %s: player_id=%i, ip=%i.%i.%i.%i\n",
                time(NULL),
                model = cdj_model_name(packet, len, CDJ_DISCOVERY_PORT),
                cdj_type_to_string(CDJ_DISCOVERY_PORT, type),
                d_pkt->player_id,
                (unsigned char) (d_pkt->ip >> 24),
                (unsigned char) (d_pkt->ip >> 16),
                (unsigned char) (d_pkt->ip >> 8),
                (unsigned char) d_pkt->ip
                );
            cdj_print_packet(packet, len, CDJ_DISCOVERY_PORT);
            /*
            if (d_pkt->player_id == v->player_id && v->ip[3] != (unsigned char) d_pkt->ip ) {
                int length;
                unsigned char* packet = cdj_create_id_use_response_packet(&length, 'C', v->player_id, v->ip);
                vdj_sendto_discovery(v, packet, length);
                free(packet);
                printf("SENDING: ID_USE PACKET\n");
            }
            */
            
            free(model);
            free(d_pkt);
            break;
        }
        case CDJ_ID_USE: {
            d_pkt = cdj_new_discovery_packet(packet, len);
            printf("%li [%-20s] %s: player_id=%i, ip=%i.%i.%i.%i\n",
                time(NULL),
                model = cdj_model_name(packet, len, CDJ_DISCOVERY_PORT),
                cdj_type_to_string(CDJ_DISCOVERY_PORT, type),
                d_pkt->player_id,
                (unsigned char) (d_pkt->ip >> 24),
                (unsigned char) (d_pkt->ip >> 16),
                (unsigned char) (d_pkt->ip >> 8),
                (unsigned char) d_pkt->ip
                );
            cdj_print_packet(packet, len, CDJ_DISCOVERY_PORT);
            free(model);
            free(d_pkt);
            break;
        }
        
        default : {
            printf("%li [%-20s] type=%02i:\n",
                time(NULL),
                model = cdj_model_name(packet, len, CDJ_DISCOVERY_PORT),
                type);
            free(model);
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
    char* model;

    int type = cdj_packet_type(packet, len);
    if (type == CDJ_BEAT) {
        cdj_beat_packet_t* b_pkt = cdj_new_beat_packet(packet, len);
        printf("%s: %-10s [%i] beat=%i, %06.2fbpm\n", 
            cdj_type_to_string(CDJ_BROADCAST_PORT, type),
            model = cdj_model_name(packet, len, CDJ_BROADCAST_PORT),
            b_pkt->player_id, 
            b_pkt->bar_pos, b_pkt->bpm);
        free(model);
        free(b_pkt);
    }

    //cdj_print_packet(packet, len, CDJ_BROADCAST_PORT);
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
    char* model;
    int type = cdj_packet_type(packet, len);

    switch (type)
    {
        case CDJ_STATUS : {
            cdj_cdj_status_packet_t* cs_pkt = cdj_new_cdj_status_packet(packet, len);
            printf("%s: %-10s [%02i] %06.2fbpm state=%s time=%li\n",
                cdj_type_to_string(CDJ_UPDATE_PORT, type),
                model = cdj_model_name(packet, len, CDJ_UPDATE_PORT),
                cs_pkt->player_id, 
                cs_pkt->bpm,
                cdj_state_to_chars(cdj_status_sync_flags(cs_pkt)),
                time(NULL)
                );
            free(model);
            free(cs_pkt);
            break;
        }
        default : {
            cdj_print_packet(packet, len, CDJ_UPDATE_PORT);
        }
    }

}
