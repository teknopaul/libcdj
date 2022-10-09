#include <stdio.h>
#include <pthread.h>
#include <signal.h>
#include <errno.h>
#include <unistd.h>
#include <sys/socket.h>
#include <stdlib.h>
#include <string.h>

#include "cdj.h"
#include "vdj.h"

/**
 * Monitor app that just scans the network for players and then exits
 */

static void handle_discovery_datagram(uint8_t* packet, uint16_t len);
static void* cdj_monitor_discoverys(void* arg);

static void signal_exit(int sig)
{
    exit(0);
}
static void usage()
{
    printf("options:\n");
    printf("    -i - network interface to use, required if pc has more than one\n");
    printf("    -w - how long scan for, default 3 seconds\n");
    printf("    -h - display this text\n");
    printf("cdj-scan exits 0 if at least 1 player is found, 1 for system errors, 2 for user errors and 3 if no players are found\n");
    exit(2);
}

// players we have already found
static uint8_t id_map[127];
static int found  = 0;

/**
 * CDJ scan for players
 */
int main(int argc, char* argv[])
{
    char* iface = NULL;
    unsigned int flags = 0;
    memset(id_map, 0, 127);
    int wait_time = 3;

    int c;
    while ( ( c = getopt(argc, argv, "i:w:h") ) != EOF) {
        switch (c) {
            case 'h':
                usage();
                break;
            case 'w':
                wait_time = atoi(optarg);
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

    // N.B. no handshake, CDJs and rekordbox do not know we are snooping the broadcast packets

    pthread_t thread_id;
    pthread_create(&thread_id, NULL, *cdj_monitor_discoverys, v);

    sleep(wait_time);

    if ( found ) return 0;
    return 3;
}

static void*
cdj_monitor_discoverys(void* arg)
{
    vdj_t* v = arg;
    int socket;
    ssize_t len;
    uint8_t packet[1500];

    socket = v->discovery_socket_fd;

    while (1) {
        len = recv(socket, packet, 1500, 0);
        if (len == -1) {
            printf("socket read error: %s\n", strerror(errno));
            sleep(1);
        } else {
            handle_discovery_datagram(packet, (uint16_t) len);
        }
    }

    return NULL;
}

static void
handle_discovery_datagram(uint8_t* packet, uint16_t len)
{
    cdj_discovery_packet_t* d_pkt;

    if ( cdj_packet_type(packet, len) == CDJ_KEEP_ALIVE ) {
        if ( (d_pkt = cdj_new_discovery_packet(packet, len)) ) {
            if ( id_map[d_pkt->player_id] ) {
                free(d_pkt);
                return;
            }
            printf("%s %i %d.%d.%d.%d\n",
                cdj_discovery_model(d_pkt),
                d_pkt->player_id,
                (unsigned char) (d_pkt->ip >> 24),
                (unsigned char) (d_pkt->ip >> 16),
                (unsigned char) (d_pkt->ip >> 8),
                (unsigned char) (d_pkt->ip >> 0)
                );
            id_map[d_pkt->player_id] = 1;
            found = 1;
            free(d_pkt);
        }
    }
}


