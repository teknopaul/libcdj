
#ifndef _VDJ_H_INCLUDED_
#define _VDJ_H_INCLUDED_

#include <time.h>
#include <inttypes.h>

#include "cdj.h"

#define VDJ_OK          0
#define VDJ_ERROR       1

#define VDJ_MAX_BACKLINE         32   // max devices on the link we can handle, also highes player_id
#define VDJ_DEVICE_TYPE          CDJ_DEV_TYPE_CDJ  // 1


// Initialization flags
// First 3 bits are player_id 0 - 15 is player ID  (when zero user player _id 5)

#define VDJ_FLAG_DEV_DJM          0x08  // pretend to be an DJM device type 2
#define VDJ_FLAG_DEV_XDJ          0x10  // pretend to be an XDJ
#define VDJ_FLAG_DEV_CDJ          0x20  // pretend to be an XDJ
#define VDJ_FLAG_AUTO_ID          0x40  // automatically assign an id
#define VDJ_FLAG_PRINT_IP         0x80  // print resolved ip address to stdout


// data structures

// Remote (real) CDJ
typedef struct {
    unsigned char       player_id;     // id of the device
    struct sockaddr_in* mip_addr;      // ip address of the device for dm
    float               bpm;           // calculated bpm, based on bpm reported in a beat message (2 decimal places)
    time_t              last_keepalive;// last time we heard from this player, rekordbox disconnects after 7 seconds
    unsigned char       active;        // device thinks its active
    unsigned char       master_state;  // sync mater state
    unsigned char       play_state;    // all the flags sent on a status packet
    unsigned int        known:1;       // this device knows us, we are getting stuff on 50002
    unsigned int        onair:1;       // DJMs can send out this info
    unsigned int        gone:1;        // CDJ has gone from the network, no keep alive in 7 seconds
} vdj_link_member_t;

// State of the whole Network, as far as we know
// N.B. requires managed threads to maintain this data based on status unicast message
typedef struct {
    unsigned char       player_count;
    vdj_link_member_t*  link_members[VDJ_MAX_BACKLINE + 1];  // link member in the slot for its player_id, in theory there are 255 slots in the protocol, 32 decks should be enough for Jeff Mills
    unsigned char       master_id;         // this VCDJ's opinion as to who is the master (there is negotiation across all the connected players) 0 = no master
    uint32_t            sync_counter;      // used for becoming master its a counter/sequence of all the ever handoffs
    unsigned char       master_new;        // new master being negotiated
    float               master_bpm;        // bpm of the player thas claims to be beat sync master
} vdj_backline_t;

// Local VCDJ
typedef struct {
    char*               ip_address;      // string format
    struct sockaddr_in* ip_addr;         // e.g. "192.168.1.5"
    struct sockaddr_in* netmask;         // e.g. "255.255.255.0"
    struct sockaddr_in* broadcast_addr;  // e.g. "192.168.1.255"

    unsigned char       ip[4];        // Stored in the format CDJs expect it, 4 bytes reading forward 192 168 2 1
    unsigned char       mac[6];       // Stored in the format CDJs expect it, 6 bytes reading forward.

    int                 discovery_socket_fd;         // 50000
    int                 discovery_unicast_socket_fd; // 50000
    int                 broadcast_socket_fd;         // 50001
    int                 update_socket_fd;            // 50002
    int                 send_socket_fd;              // any port

    unsigned char       model;          // CDJ_VDJ CDJ_XDJ or CDJ_CDJ
    unsigned char       player_id;      // player id 1 to 4 for CDJs rekordbox is 17, we use 5 by default
    unsigned char       device_type;    // device type
    unsigned char       active;         // device thinks its active, we chose this to also mean playing but there are other states
    unsigned char       bar_pos;        // 0 index position in the bar
    unsigned int        auto_id:1;      // automatically assign id
    unsigned int        have_id:1;      // automatically assign id

    // state
    uint32_t            status_counter; // how many status packets have been sent
    unsigned char       master;         // I think i am master
    unsigned char       master_req;     // this player wants to be master
    float               bpm;            // my virtual device's bpm
    vdj_backline_t*     backline;       // info we have about other CDJs on the DJ Pro Link

} vdj_t;

typedef struct  {
    vdj_t* v;
    void* handler;
} vdj_thread_info;



// handlers

// raw handlers

typedef void (*vdj_update_handler)(vdj_t* v, unsigned char* packet, uint16_t packet_length);
typedef void (*vdj_broadcast_handler)(vdj_t* v, unsigned char* packet, uint16_t packet_length);
typedef void (*vdj_discovery_handler)(vdj_t* v, unsigned char* packet, uint16_t packet_length);


// allocs
vdj_t* vdj_init(uint32_t flags);
vdj_t* vdj_init_iface(char* interface, uint32_t flags);
vdj_t* vdj_init_net(unsigned char* mac, char* ip_address, struct sockaddr_in* ip_addr, struct sockaddr_in* netmask, struct sockaddr_in *broadcast_addr, uint32_t flags);
int vdj_destroy(vdj_t* v);

void vdj_load_player_id(vdj_t* v);
void vdj_save_player_id(vdj_t* v);

int vdj_open_sockets(vdj_t* v);
int vdj_open_broadcast_sockets(vdj_t* v);
int vdj_exec_discovery(vdj_t* v);
int vdj_close_sockets(vdj_t* v);
void vdj_print_sockaddr(char* context, struct sockaddr_in* ip);

// send a single keepalive
void vdj_send_keepalive(vdj_t* v);

// sendto on different sockets and ports
int vdj_sendto_discovery(vdj_t* v, unsigned char* packet, uint16_t packet_length);
int vdj_sendto_broadcast(vdj_t* v, unsigned char* packet, uint16_t packet_length);
int vdj_sendto_update(vdj_t* v, struct sockaddr_in* dest, unsigned char* packet, uint16_t packet_length);

// internal threads

// reads discovery broadcasts
int vdj_init_discovery_thread(vdj_t* v, vdj_discovery_handler discovery_handler);
void vdj_stop_discovery_thread(vdj_t* v);

// reads beat broadcasts
int vdj_init_broadcast_thread(vdj_t* v, vdj_broadcast_handler broadcast_handler);
void vdj_stop_broadcast_thread(vdj_t* v);

// read direct messages
int vdj_init_update_thread(vdj_t* v, vdj_update_handler update_handler);
void vdj_stop_update_thread(vdj_t* v);

int vdj_init_status_thread(vdj_t* v);
void vdj_stop_status_thread(vdj_t* v);

int vdj_init_managed_update_thread(vdj_t* v, vdj_update_handler update_handler);
void vdj_stop_managed_update_thread(vdj_t* v);

void vdj_stop_threads(vdj_t* v);


void vdj_set_bpm(vdj_t* v, float bpm);
void vdj_broadcast_beat(vdj_t* v, unsigned char bar_pos);

// backline management

vdj_link_member_t* vdj_get_link_member(vdj_t* v, unsigned char player_id);
vdj_link_member_t* vdj_new_link_member(vdj_t* v, cdj_discovery_packet_t* d_pkt);
struct sockaddr_in* vdj_alloc_dest_addr(vdj_link_member_t* m, uint16_t port);
unsigned char vdj_link_member_count(vdj_t* v);

#endif /* _VDJ_H_INCLUDED_ */
