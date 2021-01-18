
#include <pthread.h>
#include <signal.h>
#include <errno.h>
#include <unistd.h>
#include <sys/socket.h>

#include "cdj.h"
#include "vdj.h"

// N.B. including .c TODO Makefile
#include "cdj_mon_tui.c"

/**
 * monitor app that shows the running devices on a network, lists only data available on the broadcast ports so this
 * can be run at the same time as sarting and stopping another local VCDJ.
 * If binding to port 50002 were added that would not be possible AFAIK.
 */

static void handle_discovery_datagram(unsigned char* packet, ssize_t len);
static void handle_broadcast_datagram(unsigned char* packet, ssize_t len);
static void* cdj_monitor_discoverys(void* arg);
static void* cdj_monitor_broadcasts(void* arg);

static void signal_exit(int sig)
{
    tui_exit();
    exit(0);
}
static void usage()
{
    printf("options:\n");
    printf("    -i - network interface to use, required if pc has more than one\n");
    printf("    -h - display this text\n");
    exit(0);
}

unsigned char id_map[127];
int next_slot = 1;

static void
update_ui(char* model_name, unsigned char player_id, unsigned char device_type, int state, float bpm)
{
    char data[2048];
    snprintf(data, 2047, "id=%02i type=%i playing=%i bpm=%f", player_id, device_type, state, bpm);
    tui_cdj_update(id_map[player_id], model_name, data);
}

/**
 * CDJ monitor tools
 */
int main (int argc, char* argv[])
{
    char* iface = NULL;
    unsigned int flags = 0;
    memset(id_map, 0, 127);

    int c;
    while ( ( c = getopt(argc, argv, "i:h") ) != EOF) {
        switch (c) {
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
     * open the networks sockets
     */
    if (vdj_open_broadcast_sockets(v) != CDJ_OK) {
        fprintf(stderr, "error: failed to open sockets\n");
        vdj_destroy(v);
        return 1;
    }

    // N.B. no handshake, CDJs and rekordbox do not know we are snooping the broadcast packets

    pthread_t thread_id;
    pthread_create(&thread_id, NULL, *cdj_monitor_discoverys, v);
    pthread_create(&thread_id, NULL, *cdj_monitor_broadcasts, v);

    printf("\n\n\n\n\n\n\n\n\n");

    signal(SIGINT, signal_exit);
    tui_init(8);
    tui_set_window_title("CDJ monitor");

    while (1) sleep(1);

    return 0;
}

static void*
cdj_monitor_discoverys(void* arg)
{
    vdj_t* v = arg;
    int socket;
    ssize_t len;
    unsigned char packet[1500];

    socket = v->discovery_socket_fd;

    while (1) {
        len = recv(socket, packet, 1500, 0);
        if (len == -1) {
            fprintf(stderr, "socket read error: %s", strerror(errno));
            return NULL;
        } else {
            handle_discovery_datagram(packet, len);
        }
    }

    return NULL;
}

static void
handle_discovery_datagram(unsigned char* packet, ssize_t len)
{
    cdj_discovery_packet_t* a_pkt;

    if (cdj_packet_type(packet, len) == CDJ_KEEP_ALIVE ) {

        a_pkt = cdj_new_discovery_packet(packet, len, CDJ_DISCOVERY_PORT);
        if (a_pkt) {
            int slot = id_map[a_pkt->player_id];
            if (!slot) {
                id_map[a_pkt->player_id] = next_slot;
                slot = next_slot++;
                update_ui(
                    cdj_model_name(packet, len, CDJ_DISCOVERY_PORT),
                    a_pkt->player_id, a_pkt->device_type, 0, 0.0);
            }
        }
    }
}


static void*
cdj_monitor_broadcasts(void* arg)
{
    vdj_t* v = arg;
    int socket;
    ssize_t len;
    unsigned char packet[1500];

    socket = v->broadcast_socket_fd;

    while (1) {
        len = recv(socket, packet, 1500, 0);
        if (len == -1) {
            fprintf(stderr, "socket read error: %s", strerror(errno));
            return NULL;
        } else {
            handle_broadcast_datagram(packet, len);
        }
    }

    return NULL;
}

static void
handle_broadcast_datagram(unsigned char* packet, ssize_t len)
{
    // cdj_print_packet(packet, len, v);

    int type = cdj_packet_type(packet, len);
    if (type == CDJ_BEAT) {
        cdj_beat_packet_t* b_pkt = cdj_new_beat_packet(packet, len, CDJ_BROADCAST_PORT);
        if (id_map[b_pkt->player_id]) {
            update_ui(
                cdj_model_name(packet, len, CDJ_BROADCAST_PORT),
                b_pkt->player_id, b_pkt->device_type, 1, b_pkt->bpm
                );
        }
    }
}

