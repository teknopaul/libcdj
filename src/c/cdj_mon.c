
#include <pthread.h>
#include <signal.h>
#include <errno.h>
#include <unistd.h>
#include <sys/socket.h>

#include "cdj.h"
#include "vdj.h"

// N.B. including .c
#include "cdj_mon_tui.c"

/**
 * Monitor app that shows the running devices on a network, lists only data available on the broadcast ports so this
 * can be run at the same time as sarting and stopping another local VDJ.
 * If binding to port 50002 were added that would not be possible.
 *
 * This is an example of using only libvdj's socket and directly handlilng Pro Link messages.
 * i.e. this is the lower level api.  We have recv() methods in this application.
 */

static void handle_discovery_datagram(unsigned char* packet, uint16_t len);
static void handle_broadcast_datagram(unsigned char* packet, uint16_t len);
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

// posisitons on screen for discovered players, mixers and rekordbox instances
unsigned char id_map[127];
int next_slot = 1;

static void
init_ui(unsigned char player_id, char* model_name)
{
    tui_cdj_init(id_map[player_id], player_id, model_name);
}

static void
update_ui(unsigned char player_id, unsigned char beat, float bpm)
{
    char data[2048];
    snprintf(data, 2047, "[%02i] %i %06.2fbpm", player_id, beat, bpm);
    tui_cdj_update(id_map[player_id], data);
}

/**
 * CDJ monitor tools
 */
int main (int argc, char* argv[])
{
    char title[128];
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

    signal(SIGINT, signal_exit);
    tui_init(8);
    snprintf(title, 127, "CDJ monitor: %i", v->player_id);
    tui_set_window_title(title);

    // N.B. no handshake, CDJs and rekordbox do not know we are snooping the broadcast packets

    pthread_t thread_id;
    pthread_create(&thread_id, NULL, *cdj_monitor_discoverys, v);
    pthread_create(&thread_id, NULL, *cdj_monitor_broadcasts, v);

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
            tui_set_cursor_pos(0, 0);
            printf("socket read error: %s", strerror(errno));
            sleep(1);
        } else {
            //tui_set_cursor_pos(0, 0);
            //printf("  pkt: %li type=%02i %02i, pid=%i", len, packet[CDJ_PACKET_TYPE_OFFSET], packet[CDJ_PACKET_TYPE_OFFSET + 1], packet[0x24]);
            fflush(stdout);
            handle_discovery_datagram(packet, (uint16_t) len);
        }
    }

    return NULL;
}

static void
handle_discovery_datagram(unsigned char* packet, uint16_t len)
{
    cdj_discovery_packet_t* d_pkt;
    char* model;

    if ( cdj_packet_type(packet, len) == CDJ_KEEP_ALIVE ) {

        d_pkt = cdj_new_discovery_packet(packet, len);
        if (d_pkt) {
            int slot = id_map[d_pkt->player_id];
            //tui_set_cursor_pos(0, 0);
            //printf("  slot: %i pid=%i pid=%i", slot, d_pkt->player_id, packet[0x24]);
            if (slot == 0) {
                id_map[d_pkt->player_id] = next_slot;
                slot = next_slot++;
                model = cdj_model_name(packet, len, CDJ_DISCOVERY_PORT);
                if (model) {
                    init_ui(d_pkt->player_id, model);
                    free(model);
                }
            }
            free(d_pkt);
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
handle_broadcast_datagram(unsigned char* packet, uint16_t len)
{
    if ( cdj_packet_type(packet, len) == CDJ_BEAT ) {

        cdj_beat_packet_t* b_pkt = cdj_new_beat_packet(packet, len);
        //tui_set_cursor_pos(0, 0);
        //printf("  beat: %i pid=%i pid=%i", id_map[b_pkt->player_id], b_pkt->player_id, packet[0x21]);
        if (b_pkt && id_map[b_pkt->player_id]) {

            update_ui(b_pkt->player_id, b_pkt->bar_pos, b_pkt->bpm);

        }
        free(b_pkt);

    }
}

