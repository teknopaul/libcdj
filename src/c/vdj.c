
/*
 * VirtualCDJ is a faked CDJ device on the network which can receive info from other CDJs.
 * Not all info is broadcast, some is point to point UDP and TCP
 * This code contains common utils for VCDJs.
 *
 * open sockets for send and receive
 * - local_ip:50000    broadcast sendto & recv
 * - local_ip:50000    unicast recv
 * - broadcast:50001  recv
 * - local_ip:50002    
 *
 * Pioneer send from ip:any   to broadcast:50001
 *         send from ip:50000 to broadcast:50000 
 *
 * we seond from the same sockes we listen from
 * 
 * @autho teknopaul
 */

#include <pthread.h>
#include <stdatomic.h>
#include <errno.h>
#include <time.h>
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
#include "vdj_net.h"
#include "vdj_store.h"
#include "vdj_beat.h"
#include "vdj_master.h"
#include "vdj_discovery.h"

#define BROADCAST 1
#define UNICAST   0


static unsigned _Atomic vdj_discovery_running = ATOMIC_VAR_INIT(0);
static unsigned _Atomic vdj_broadcast_running = ATOMIC_VAR_INIT(0);
static unsigned _Atomic vdj_update_running = ATOMIC_VAR_INIT(0);    // read other statuses
static unsigned _Atomic vdj_status_running = ATOMIC_VAR_INIT(0);    // send status


static int vdj_open_discovery_socket(vdj_t* v);
static int vdj_open_discovery_unicast_socket(vdj_t* v);
static int vdj_open_update_socket(vdj_t* v);
static int vdj_open_broadcast_socket(vdj_t* v);
static int vdj_open_send_socket(vdj_t* v);
static int vdj_close_discovery_socket(vdj_t* v);
static int vdj_close_discovery_unicast_socket(vdj_t* v);
static int vdj_close_update_socket(vdj_t* v);
static int vdj_close_broadcast_socket(vdj_t* v);
static int vdj_close_send_socket(vdj_t* v);
static int vdj_get_network_details(char* iface, unsigned char** local_mac, char** ip_address, struct sockaddr_in** ip_addr, struct sockaddr_in** netmask, struct sockaddr_in** broadcast_addr);

static void* vdj_discovery_loop(void* arg);

static void vdj_handle_managed_update_datagram(vdj_t* v, vdj_update_handler update_handler, unsigned char* packet, uint16_t len);
static void vdj_handle_managed_broadcast_datagram(vdj_t* v, vdj_broadcast_handler broadcast_handler, unsigned char* packet, uint16_t len);

/**
 * Initialise Virtual CDJ, this tries to guess the NIC by looking for one that is a physical device, not wireless, and not the loopback interface.
 *
 * If you have multiple NICs it gives up and you must provide one.
 * 
 * @param flags - see headers
 * 
 * @return mallocd space or NULL;
 */
vdj_t*
vdj_init(uint32_t flags)
{
    return vdj_init_iface(NULL, flags);
}

/**
 * Initialise Virtual CDJ.
 * 
 * @param iface - interface to use or NULL if this PC has only one IP address it will be used
 * @param flags - see headers
 * 
 * @return mallocd space or NULL;
 */
vdj_t*
vdj_init_iface(char* iface, uint32_t flags)
{
    unsigned char* mac;
    char* ip_address;
    struct sockaddr_in* ip_addr;
    struct sockaddr_in* netmask;
    struct sockaddr_in* broadcast_addr;
    if (vdj_get_network_details(iface, &mac, &ip_address, &ip_addr, &netmask, &broadcast_addr) == CDJ_OK) {
        ip_addr->sin_family = AF_INET;
        return vdj_init_net(mac, ip_address, ip_addr, netmask, broadcast_addr, flags);
    }
    return NULL;
}

// This is public so you can start with a specific IP on an interface with two IPs
vdj_t*
vdj_init_net(unsigned char* mac, char* ip_address, struct sockaddr_in* ip_addr, struct sockaddr_in* netmask, struct sockaddr_in *broadcast_addr, uint32_t flags)
{
    char mac_s[18];
    vdj_t* v = (vdj_t*) calloc(1, sizeof(vdj_t));
    if (v != NULL) {
        // Setup the vdj_t struct
        v->model = CDJ_VDJ;

        memcpy(v->mac, mac, 6); // TODO leak
        v->ip_address = ip_address;
        v->ip_addr = ip_addr;
        v->netmask = netmask;
        v->broadcast_addr = broadcast_addr;

        cdj_ip_format(ip_address, v->ip); // TODO error handling

        // process flags
        char player_id = flags & 0x7;
        if (player_id) {
            v->player_id = player_id;
        } else {
            v->player_id = 5;
        }

        if (flags & VDJ_FLAG_DEV_XDJ) {
            v->model = CDJ_XDJ;
        }

        if (flags & VDJ_FLAG_DEV_CDJ) {
            v->model = CDJ_CDJ;
        }

        if (flags & VDJ_FLAG_AUTO_ID) {
            v->auto_id = 1;
            v->player_id = 1;
        }

        if (flags & VDJ_FLAG_PRINT_IP) {
            vdj_mac_addr_to_string(mac, mac_s);
            printf("vdj: %s/%s\n", ip_address, mac_s);
        }
        v->status_counter = 1;
        v->pitch = 0x00100000;

        v->backline = (vdj_backline_t*) calloc(1, sizeof(vdj_backline_t));

    }
    return v;
}

void
vdj_set_bpm(vdj_t* v, float bpm)
{
    v->bpm = bpm;
}

/**
 * Open the UDP file descriptors for CDJ communication.
 * Does not start listening yet.
 */
int
vdj_open_sockets(vdj_t* v)
{
    return (
        vdj_open_discovery_socket(v) == CDJ_OK &&
        vdj_open_discovery_unicast_socket(v) == CDJ_OK &&
        vdj_open_broadcast_socket(v) == CDJ_OK &&
        vdj_open_update_socket(v) == CDJ_OK &&
        vdj_open_send_socket(v) == CDJ_OK ) ? CDJ_OK : CDJ_ERROR;
}

int
vdj_open_broadcast_sockets(vdj_t* v)
{
    return (
        vdj_open_discovery_socket(v) == CDJ_OK &&
        vdj_open_broadcast_socket(v) == CDJ_OK ) ? CDJ_OK : CDJ_ERROR;
}

/**
 * Calls free() make sure threads that have this pointer have terminated first
 */
int
vdj_destroy(vdj_t* v)
{
    int i;
    int res = vdj_close_sockets(v);
    if (v->backline) {
        for (i = 0; i < VDJ_MAX_BACKLINE; i++) {
            if (v->backline->link_members[i]) {
                if (v->backline->link_members[i]->mip_addr) {
                    free(v->backline->link_members[i]->mip_addr);
                }
            }
            free(v->backline->link_members[i]);
        }
        free(v->backline);
    }
    if (v->ip_addr) free(v->ip_addr);
    if (v->netmask) free(v->netmask);
    if (v->broadcast_addr) free(v->broadcast_addr);

    free(v);
    return res;
}

/**
 * Closes all network sockets
 */
int
vdj_close_sockets(vdj_t* v)
{
    return vdj_close_discovery_socket(v) | 
        vdj_close_discovery_unicast_socket(v) | 
        vdj_close_update_socket(v) | 
        vdj_close_broadcast_socket(v) | 
        vdj_close_send_socket(v);
}


/**
 * manually trigger a keep alive message, N.B. backline needs to be updated for the link memmber count filed to be correct
 */
void
vdj_send_keepalive(vdj_t* v)
{
    uint16_t length;
    // TODO XDJ does not base link member count on keepalives
    unsigned char* packet = cdj_create_keepalive_packet(&length, v->model, v->ip, v->mac, v->player_id, 1 + vdj_link_member_count(v));
    if (packet) {
        vdj_sendto_discovery(v, packet, length);
        free(packet);
    }
}

// status loop (every 200ms)
int
vdj_send_status(vdj_t* v)
{
    uint16_t length;
    int i, rv = CDJ_OK;
    vdj_link_member_t* m;
    struct sockaddr_in* dest;

    if (v->backline) {
        unsigned char* packet = cdj_create_status_packet(&length, v->model, v->player_id, 
            v->bpm, v->bar_pos, v->active, v->master, v->backline ? v->backline->sync_counter : 1,
            v->status_counter++);
        if (packet == NULL) {
            return CDJ_ERROR;
        }

        for ( i = 0; i < VDJ_MAX_BACKLINE; i++) {
            if ( (m = v->backline->link_members[i]) && m->mip_addr) {
                dest = vdj_alloc_dest_addr(m, CDJ_UPDATE_PORT);
                rv |= vdj_sendto_update(v, dest, packet, length);
                free(dest);
                //printf("update sent to player_id=%02i err=%i len=%i\n", i, rv, length);
            }
        }

        //cdj_print_packet(packet, length, CDJ_UPDATE_PORT);
    }
    return rv;
}

static void*
vdj_status_loop(void* arg)
{
    vdj_t* v = arg;
    vdj_status_running = 1;
    while (vdj_status_running) {
        usleep(CDJ_STATUS_INTERVAL * 1000);
        vdj_send_status(v);
    }
    return NULL;
}

/**
 * sends out status to the network
 */
int
vdj_init_status_thread(vdj_t* v)
{
    pthread_t thread_id;
    int s = pthread_create(&thread_id, NULL, vdj_status_loop, v);
    if (s != 0) {
        return CDJ_ERROR;
    }
    return CDJ_OK;
}

void
vdj_stop_status_thread(vdj_t* v)
{
    vdj_status_running = 0;
}

void
vdj_stop_threads(vdj_t* v)
{
    vdj_discovery_running = 0;
    vdj_broadcast_running = 0;
    vdj_update_running = 0;
    vdj_status_running = 0;
    vdj_stop_keepalive_thread();
}

void
vdj_broadcast_beat(vdj_t* v, unsigned char bar_pos)
{
    uint16_t length;
    v->bar_pos = bar_pos;
    unsigned char* packet = cdj_create_beat_packet(&length, v->model, v->player_id, v->bpm, bar_pos);
    if (packet) {
        vdj_sendto_broadcast(v, packet, length);
        free(packet);
    }
    clock_gettime(CLOCK_MONOTONIC, &v->last_beat);
}

// networks methods

void
vdj_print_sockaddr(char* context, struct sockaddr_in* ip)
{
    char ip_s[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &ip->sin_addr.s_addr, ip_s, INET_ADDRSTRLEN);
    fprintf(stderr, "%s %s:%i\n", context, ip_s, ntohs(ip->sin_port));
}

struct sockaddr_in*
vdj_alloc_dest_addr(vdj_link_member_t* m, uint16_t port)
{
    struct sockaddr_in* dest = calloc(1, sizeof(struct sockaddr_in));
    dest->sin_family = AF_INET;
    dest->sin_addr.s_addr = m->mip_addr->sin_addr.s_addr;
    dest->sin_port = (in_port_t)htons(port);
    return dest;
}

static int
vdj_get_network_details(char* iface, unsigned char** out_mac, char** out_ip_address, struct sockaddr_in** out_ip_addr, struct sockaddr_in** out_netmask, struct sockaddr_in** out_broadcast_addr)
{
    unsigned char* mac;
    char* ip_address;
    char netmask_s[INET6_ADDRSTRLEN];
    char broadcast_s[INET6_ADDRSTRLEN];
    struct sockaddr_in* ip_addr;
    struct sockaddr_in* netmask;
    struct sockaddr_in* broadcast_addr;

    mac = (unsigned char*) calloc(1, 6 * sizeof(unsigned char));
    ip_addr = (struct sockaddr_in*) calloc(1, sizeof(struct sockaddr_in));
    netmask = (struct sockaddr_in*) calloc(1, sizeof(struct sockaddr_in));
    broadcast_addr = (struct sockaddr_in*) calloc(1, sizeof(struct sockaddr_in));
    ip_address = calloc(1, INET_ADDRSTRLEN);

    int err = CDJ_OK;
    if (iface == NULL) {
        // not been told which interface to use
        if (vdj_has_single_ip() == CDJ_OK) {
            // init with single interface
            iface = (char*) calloc(1, IFNAMSIZ + 1);
            err = vdj_get_single_ip(iface, ip_addr, netmask);
        } else {
            err = CDJ_ERROR; // TODO more explicit
        }
    } else {
        err = vdj_find_ip(iface, ip_addr, netmask);
    }

    if (err) {
        fprintf(stderr, "error: could not determine ip address (use -i)\n");
        vdj_print_iface();
        free(mac);
        free(ip_addr);
        free(netmask);
        free(broadcast_addr);
        free(ip_address);
        return CDJ_ERROR;
    }

    // create boradcast
    broadcast_addr->sin_addr.s_addr = netmask->sin_addr.s_addr ^ 0xffffffff;
    broadcast_addr->sin_addr.s_addr = broadcast_addr->sin_addr.s_addr | ip_addr->sin_addr.s_addr;

    // debug
    inet_ntop(AF_INET, &(ip_addr->sin_addr), ip_address, INET_ADDRSTRLEN);
    inet_ntop(AF_INET, &(netmask->sin_addr), netmask_s, INET_ADDRSTRLEN);
    inet_ntop(AF_INET, &(broadcast_addr->sin_addr), broadcast_s, INET_ADDRSTRLEN);

    vdj_get_mac_addr(iface, mac);

    // return
    *out_mac = mac;
    *out_ip_address = ip_address;
    *out_ip_addr = ip_addr;
    *out_netmask = netmask;
    *out_broadcast_addr = broadcast_addr;

    return CDJ_OK;
}

/**
 * Open a socket with which to send and/or receive packets.
 * Send from v->ip_address:port
 * 
 * @param port - local port to bind to
 * @param broadcast - BROADCAST|UNICAST  if BROADCAST set SO_BROADCAST socket option and bind to .255 addr
 * @param socket_fd_out if successfule this is set to the file descriptor
 */
static int
vdj_open_socket(vdj_t* v, int port, int broadcast, int *socket_fd_out)
{
    struct sockaddr_in src_addr;
    int socket_fd, value;

    //  create the socket
    socket_fd = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (-1 == socket_fd) {
        fprintf(stderr, "error: create socket");
        return CDJ_ERROR;
    }

    // set socket options
    if (broadcast) {
        value = 1;
        if ( setsockopt(socket_fd, SOL_SOCKET, SO_BROADCAST, &value, sizeof(int)) ) {
            fprintf(stderr, "error: set broadcast socket opt");
            return CDJ_ERROR;
        }
        if ( setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, &value, sizeof(int)) ) {
            fprintf(stderr, "error: set broadcast socket reuse opt");
            return CDJ_ERROR;
        }
        // create source address
        memcpy(&src_addr, v->broadcast_addr, sizeof(struct sockaddr_in));
        src_addr.sin_family = AF_INET;
        src_addr.sin_port = htons(port);
    } else {
        // create source address
        memcpy(&src_addr, v->ip_addr, sizeof(struct sockaddr_in));
        src_addr.sin_family = AF_INET;
        src_addr.sin_port = htons(port);
    }

    // bind socket to local port
    if ( bind(socket_fd, (struct sockaddr *) &src_addr, sizeof(src_addr)) < 0) {
        fprintf(stderr, "error: bind %s:%d\n", v->ip_address, port);
        close(socket_fd);
        return CDJ_ERROR;
    } else {
        *socket_fd_out = socket_fd;
        return CDJ_OK;
    }

}
static int
vdj_open_socket_out(vdj_t* v, int port, int *socket_fd_out)
{
    struct sockaddr_in src_addr;
    int socket_fd = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (-1 == socket_fd) {
        fprintf(stderr, "error: create out socket");
        return CDJ_ERROR;
    }

    memcpy(&src_addr, v->ip_addr, sizeof(struct sockaddr_in));
    src_addr.sin_family = AF_INET;
    src_addr.sin_port = htons(0); // any free port

    // bind socket to local port
    if ( bind(socket_fd, (struct sockaddr *) &src_addr, sizeof(src_addr)) < 0) {
        fprintf(stderr, "error: bind %s:%d\n", v->ip_address, port);
        close(socket_fd);
        return CDJ_ERROR;
    }

    *socket_fd_out = socket_fd;
    return CDJ_OK;
}
/**
 * Open socket with which to send keep alive packets.
 * Send from v->ip_address:50000 to v->broadcast:50000
 */
static int
vdj_open_discovery_socket(vdj_t* v)
{
    return vdj_open_socket(v, CDJ_DISCOVERY_PORT, BROADCAST, &v->discovery_socket_fd);
}

static int
vdj_close_discovery_socket(vdj_t* v)
{
    if (v->discovery_socket_fd && close(v->discovery_socket_fd) ) {
        fprintf(stderr, "error: discovery socket udp socket close '%s'\n", strerror(errno));
        return CDJ_ERROR;
    }
    return CDJ_OK;
}

/**
 * Open socket with which to recv unicast id use messages
 * recv from v->ip_address:50000
 */
static int
vdj_open_discovery_unicast_socket(vdj_t* v)
{
    return vdj_open_socket(v, CDJ_DISCOVERY_PORT, UNICAST, &v->discovery_unicast_socket_fd);
}

static int
vdj_close_discovery_unicast_socket(vdj_t* v)
{
    if (v->discovery_unicast_socket_fd && close(v->discovery_unicast_socket_fd) ) {
        fprintf(stderr, "error: discovery unicast socket udp socket close '%s'\n", strerror(errno));
        return CDJ_ERROR;
    }
    return CDJ_OK;
}
/**
 * Open socket with which to receive data from other CDJs (v->ip_address:50002)
 */
static int
vdj_open_update_socket(vdj_t* v)
{
    return vdj_open_socket(v, CDJ_UPDATE_PORT, UNICAST, &v->update_socket_fd);
}

static int
vdj_close_update_socket(vdj_t* v)
{
    if (v->update_socket_fd && close(v->update_socket_fd) ) {
        fprintf(stderr, "error: update udp socket close '%s'\n", strerror(errno));
        return CDJ_ERROR;
    }
    return CDJ_OK;
}

/**
 * Open socket with which to receive timing packets.
 * Send from v->ip_address:50001 to v->broadcast:50001
 */
static int
vdj_open_broadcast_socket(vdj_t* v)
{
    return vdj_open_socket(v, CDJ_BROADCAST_PORT, BROADCAST, &v->broadcast_socket_fd);
}

static int
vdj_close_broadcast_socket(vdj_t* v)
{
    if (v->broadcast_socket_fd && close(v->broadcast_socket_fd) ) {
        fprintf(stderr, "error: broadcast udp socket close '%s'\n", strerror(errno));
        return CDJ_ERROR;
    }
    return CDJ_OK;
}

/**
 * Open socket with which to send udp
 */
static int
vdj_open_send_socket(vdj_t* v)
{
    return vdj_open_socket_out(v, 0, &v->send_socket_fd);
}

static int
vdj_close_send_socket(vdj_t* v)
{
    if (v->send_socket_fd && close(v->send_socket_fd) ) {
        fprintf(stderr, "error: send udp socket close '%s'\n", strerror(errno));
        return CDJ_ERROR;
    }
    return CDJ_OK;
}
// output

/**
 * send a packet from discovery_socket_fd to broadcast:50000
 */
int
vdj_sendto_discovery(vdj_t* v, unsigned char* packet, uint16_t packet_length)
{
    int flags = 0;
    struct sockaddr_in dest;

    if (v->discovery_socket_fd == 0) {
        fprintf(stderr, "error: socket not open\n");
        return CDJ_ERROR;
    }

    memcpy(&dest, v->broadcast_addr, sizeof(struct sockaddr_in));
    dest.sin_port = (in_port_t)htons(CDJ_DISCOVERY_PORT);

    int res = sendto(v->discovery_socket_fd, packet, packet_length, flags, (struct sockaddr*) &dest, sizeof(struct sockaddr_in));
    if (res == -1) {
        fprintf(stderr, "error: broadcast:50000 %s\n", strerror(errno));
        return CDJ_ERROR;
    }
    return res == -1 ? CDJ_ERROR : CDJ_OK;
}

/**
 * send a packet from broadcast_socket_fd to broadcast:50001
 * used to send out beat timing
 */
int
vdj_sendto_broadcast(vdj_t* v, unsigned char* packet, uint16_t packet_length)
{
    int flags = 0;
    struct sockaddr_in dest;

    if (v->broadcast_socket_fd == 0) {
        fprintf(stderr, "error: socket not open\n");
        return CDJ_ERROR;
    }

    memcpy(&dest, v->broadcast_addr, sizeof(struct sockaddr_in));
    dest.sin_port = (in_port_t)htons(CDJ_BROADCAST_PORT);

    int res = sendto(v->broadcast_socket_fd, packet, packet_length, flags, (struct sockaddr*) &dest, sizeof(struct sockaddr_in));
    if (res == -1) {
        fprintf(stderr, "error: broadcast:50001 'error: '%s'\n", strerror(errno));
        return CDJ_ERROR;
    }
    return res == -1 ? CDJ_ERROR : CDJ_OK;
}

/**
 * send a packet from send_socket_fd to remote:50002 (or anywhere else)
 */
int
vdj_sendto_update(vdj_t* v, struct sockaddr_in* dest, unsigned char* packet, uint16_t packet_length)
{
    int flags = 0;
    if (v->send_socket_fd == 0) {
        fprintf(stderr, "error: socket not open\n");
        return CDJ_ERROR;
    }

    int res = sendto(v->send_socket_fd, packet, packet_length, flags, (struct sockaddr*) dest, sizeof(struct sockaddr_in));
    if (res == -1) {
        char ip_s[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &dest->sin_addr.s_addr, ip_s, INET_ADDRSTRLEN);
        fprintf(stderr, "error: unicast to [%i] %s:%i '%s'\n", dest->sin_family, ip_s, ntohs(dest->sin_port), strerror(errno));
        return CDJ_ERROR;
    }
    return res == -1 ? CDJ_ERROR : CDJ_OK;
}


// client managed discovery

static void*
vdj_discovery_loop(void* arg)
{
    vdj_thread_info* tinfo = arg;
    vdj_discovery_handler discovery_handler = tinfo->handler;

    ssize_t len;
    unsigned char packet[1500];

    while (vdj_discovery_running) {
        len = recv(tinfo->v->discovery_socket_fd, packet, 1500, 0);
        if (len == -1) {
            fprintf(stderr, "error: socket read '%s'", strerror(errno));
            return NULL;
        } else {
            if ( ! cdj_validate_header(packet, len)) discovery_handler(tinfo->v, packet, len);
        }
    }

    return NULL;
}

int
vdj_init_discovery_thread(vdj_t* v, vdj_discovery_handler discovery_handler)
{
    if (vdj_discovery_running) return CDJ_ERROR;
    vdj_discovery_running = 1;

    pthread_t thread_id;
    vdj_thread_info* tinfo = (vdj_thread_info*) calloc(1, sizeof(vdj_thread_info));
    tinfo->v = v;
    tinfo->handler = discovery_handler;
    return pthread_create(&thread_id, NULL, &vdj_discovery_loop, tinfo);
}
void
vdj_stop_discovery_thread(vdj_t* v)
{
    vdj_discovery_running = 0;
}



static void*
broadcast_loop(void* arg)
{
    vdj_thread_info* tinfo = arg;
    vdj_broadcast_handler broadcast_handler = tinfo->handler;

    ssize_t len;
    unsigned char packet[1500];

    vdj_broadcast_running = 1;
    while (vdj_broadcast_running) {
        len = recv(tinfo->v->broadcast_socket_fd, packet, 1500, 0);
        if (len == -1) {
            fprintf(stderr, "error: socket read '%s'", strerror(errno));
            return NULL;
        } else {
            if ( ! cdj_validate_header(packet, len) ) broadcast_handler(tinfo->v, packet, len);
        }
    }
    return NULL;
}

int
vdj_init_broadcast_thread(vdj_t* v, vdj_broadcast_handler broadcast_handler)
{
    pthread_t thread_id;
    vdj_thread_info* tinfo = (vdj_thread_info*) calloc(1, sizeof(vdj_thread_info));
    tinfo->v = v;
    tinfo->handler = broadcast_handler;
    return pthread_create(&thread_id, NULL, &broadcast_loop, tinfo);
}

void
vdj_stop_broadcast_thread(vdj_t* v)
{
    vdj_broadcast_running = 0;
}


static void*
vdj_managed_broadcast_loop(void* arg)
{
    vdj_thread_info* tinfo = arg;
    vdj_broadcast_handler broadcast_handler = tinfo->handler;
    vdj_t* v = tinfo->v;

    ssize_t len;
    unsigned char packet[1500];

    vdj_broadcast_running = 1;
    while (vdj_broadcast_running) {
        len = recv(tinfo->v->broadcast_socket_fd, packet, 1500, 0);
        if (len == -1) {
            fprintf(stderr, "error: socket read '%s'", strerror(errno));
            return NULL;
        } else {
            if ( ! cdj_validate_header(packet, len) ) {
                vdj_handle_managed_broadcast_datagram(v, broadcast_handler, packet, len);
            }
        }
    }
    return NULL;
}

int
vdj_init_managegd_broadcast_thread(vdj_t* v, vdj_broadcast_handler broadcast_handler)
{
    pthread_t thread_id;
    vdj_thread_info* tinfo = (vdj_thread_info*) calloc(1, sizeof(vdj_thread_info));
    tinfo->v = v;
    tinfo->handler = broadcast_handler;
    return pthread_create(&thread_id, NULL, &vdj_managed_broadcast_loop, tinfo);
}
void
vdj_stop_managegd_broadcast_thread(vdj_t* v)
{
    vdj_broadcast_running = 0;
}


static void
vdj_handle_managed_broadcast_datagram(vdj_t* v, vdj_broadcast_handler broadcast_handler, unsigned char* packet, uint16_t len)
{
    unsigned char type = cdj_packet_type(packet, len);
    cdj_beat_packet_t* b_pkt;
    vdj_link_member_t* m;

    switch (type) {

        case CDJ_BEAT : {

            b_pkt = cdj_new_beat_packet(packet, len);
            if (!b_pkt) {
                return;
            }

            if ( (m = vdj_get_link_member(v, b_pkt->player_id)) ) {
                m->bpm = b_pkt->bpm;
                clock_gettime(CLOCK_MONOTONIC, &m->last_beat);
            }
            // optionally chain the handler so that client code can also react to client updates
            if (broadcast_handler) broadcast_handler(v, packet, len);
            free(b_pkt);
            break;
        }

    }
}


static void*
vdj_update_loop(void* arg)
{
    vdj_thread_info* tinfo = arg;
    vdj_update_handler update_handler = tinfo->handler;

    ssize_t len;
    unsigned char packet[1500];

    vdj_update_running = 1;
    while (vdj_update_running) {
        len = recv(tinfo->v->update_socket_fd, packet, 1500, 0);
        if (len == -1) {
            fprintf(stderr, "error: socket read '%s'", strerror(errno));
            return NULL;
        } else {
            if ( ! cdj_validate_header(packet, len) ) update_handler(tinfo->v, packet, len);
        }
    }

    return NULL;
}

int
vdj_init_update_thread(vdj_t* v, vdj_update_handler update_handler)
{
    pthread_t thread_id;
    vdj_thread_info* tinfo = (vdj_thread_info*) calloc(1, sizeof(vdj_thread_info));
    tinfo->v = v;
    tinfo->handler = update_handler;
    return pthread_create(&thread_id, NULL, &vdj_update_loop, tinfo);
}

void
vdj_stop_update_thread(vdj_t* v)
{
    vdj_update_running = 0;
}

// managed update thread, this handles other CDJ status messages
// and updates backline state and vdj_t internal satte

static void*
vdj_managed_update_loop(void* arg)
{
    vdj_thread_info* tinfo = arg;
    vdj_t* v = tinfo->v;
    vdj_update_handler update_handler = tinfo->handler;

    ssize_t len;
    unsigned char packet[1500];

    vdj_update_running = 1;
    while (vdj_update_running) {
        len = recv(v->update_socket_fd, packet, 1500, 0);
        if (len == -1) {
            fprintf(stderr, "socket read error: %s", strerror(errno));
            return NULL;
        } else {
            if ( ! cdj_validate_header(packet, len) ) vdj_handle_managed_update_datagram(v, update_handler, packet, len);
        }
    }
    return NULL;
}

int
vdj_init_managed_update_thread(vdj_t* v, vdj_update_handler update_handler)
{
    if (vdj_update_running) return CDJ_ERROR;

    if (v->backline == NULL) {
        fprintf(stderr, "error: init a managed discovery thread first\n");
        return CDJ_ERROR;
    }

    pthread_t thread_id;
    vdj_thread_info* tinfo = (vdj_thread_info*) calloc(1, sizeof(vdj_thread_info));
    tinfo->v = v;
    tinfo->handler = update_handler;
    int s = pthread_create(&thread_id, NULL, vdj_managed_update_loop, tinfo);
    if (s != 0) {
        return CDJ_ERROR;
    }
    return CDJ_OK;
}

static void
vdj_handle_managed_update_datagram(vdj_t* v, vdj_update_handler update_handler, unsigned char* packet, uint16_t len)
{
    unsigned char type = cdj_packet_type(packet, len);
    cdj_cdj_status_packet_t* cs_pkt;
    vdj_link_member_t* m;

    switch (type) {

        case CDJ_STATUS : {

            cs_pkt = cdj_new_cdj_status_packet(packet, len);
            if (!cs_pkt) {
                return;
            }

            if ( (m = vdj_get_link_member(v, cs_pkt->player_id)) ) {
                // update link master 
                vdj_update_new_master(v, cdj_status_new_master(cs_pkt));
                m->bpm = cs_pkt->bpm;
                m->last_keepalive = time(NULL);
                m->known = 1;
                m->active = cdj_status_active(cs_pkt);
                m->master_state = cdj_status_master_state(cs_pkt);
                m->pitch = cdj_status_pitch(cs_pkt);
                if (m->master_state == CDJ_MASTER_STATE_ON) {
                    v->backline->master_id = cs_pkt->player_id;
                }
            }
            unsigned int sync_counter = cdj_status_sync_counter(cs_pkt);
            if ( sync_counter > v->backline->sync_counter ) {
                v->backline->sync_counter = sync_counter;
            }

            // optionally chain the handler so that client code can also react to client updates
            if (update_handler) update_handler(v, packet, len);
            free(cs_pkt);
            break;
        }

    }
}

void
vdj_stop_managed_update_thread(vdj_t* v)
{
    vdj_update_running = 0;
}


// backline management TODO new file

vdj_link_member_t*
vdj_get_link_member(vdj_t* v, unsigned char player_id)
{
    if (v->backline) {
        return v->backline->link_members[player_id];
    }
    return NULL;
}

vdj_link_member_t*
vdj_new_link_member(vdj_t* v, cdj_discovery_packet_t* d_pkt)
{
    // dont add self
    if (v->player_id == d_pkt->player_id) return NULL;

    vdj_link_member_t* m = (vdj_link_member_t*) calloc(1, sizeof(vdj_link_member_t));
    if (m) {
        m->player_id = d_pkt->player_id;
        m->mip_addr = cdj_ip_decode(d_pkt->ip);
        v->backline->link_members[d_pkt->player_id] = m;
        m->active = 1;
    }

    return m;
}

// return count excluding ourselves since we may be snooping
unsigned char
vdj_link_member_count(vdj_t* v)
{
    int i;
    unsigned char c = 0;
    if (v->backline) {
        for (i = 1; i <= VDJ_MAX_BACKLINE; i++) {
            if (v->backline->link_members[i] && v->backline->link_members[i]->active) {
                c++;
            }
        }
    }
    return c;
}


//SNIP_time_diff
/**
 * return time diff between our last beat and theirs as recorded on this machine.
 * only works at all if we have beats coming from both and managed threads.
 * max value is +250ms and min value is -250ms, which is ~one a beat at 240bpm
 * and 1/2 of a beat at 120, any further out and DJ is probably mixing on the half beats.
 * @return  a positive value if we are ahead, negative if we are behind.
 */
int64_t
vdj_time_diff(vdj_t* v, vdj_link_member_t* m)
{
    struct timespec me = v->last_beat;
    struct timespec you = m->last_beat;

    // +ve if you > me
    int64_t diff =  (you.tv_sec * 1000 + you.tv_nsec / 1000000) - (me.tv_sec * 1000 + me.tv_nsec / 1000000);
    return (diff >= -250 && diff <= 250) ? diff : 0;
}
//SNIP_time_diff