
#ifndef _VDJ_H_INCLUDED_
#define _VDJ_H_INCLUDED_

#include "cdj.h"

#define VDJ_OK          0
#define VDJ_ERROR       1

#define VDJ_MAX_BACKLINE         32   // max devices on the link we can handle, also highest player_id
#define VDJ_DEVICE_TYPE          CDJ_DEV_TYPE_CDJ  // 1
#define VDJ_MAX_PLAYERS          4    // max players onthe backline, protocol seems to imply 4 is max

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
    struct timespec     last_beat;     // nanosecond time of last beat
    time_t              last_keepalive;// last time we heard from this player, (resolution in secs) rekordbox disconnects after 7 seconds
    float               bpm;           // calculated bpm, based on bpm reported in a beat message (2 decimal places)
    int32_t             pitch;         // slider amount (tempo not necessarily pitch)
    struct sockaddr_in* ip_addr;       // ip address of the device
    struct sockaddr_in* update_addr;   // ip address and port of the device for dm on CDJ_UPDATE_PORT
    uint8_t             bar_pos;       // 
    uint8_t             active;        // device thinks its active
    uint8_t             master_state;  // sync master state
    uint8_t             play_state;    // all the flags sent on a status packet
    uint8_t             player_id;     // id of the device
    unsigned int        known:1;       // this device knows us, we are getting stuff on 50002
    unsigned int        onair:1;       // DJMs can send out this info
    unsigned int        gone:1;        // CDJ has gone from the network, no keep alive in 7 seconds
} vdj_link_member_t;

// State of the whole Network, as far as we know
// N.B. requires managed threads to maintain this data based on status unicast message
typedef struct {
    vdj_link_member_t*  link_members[VDJ_MAX_BACKLINE + 1];  // link member in the slot for its player_id, in theory there are 255 slots in the protocol, 32 decks should be enough for Jeff Mills
                                                             // this array always excludes self
    uint32_t            sync_counter;      // used for becoming master its a counter/sequence of all the ever handoffs
    float               master_bpm;        // bpm of the player thas claims to be beat sync master
    uint8_t             master_id;         // this VDJ's opinion as to who is the master (there is negotiation across all the connected players) 0 = no master
    uint8_t             master_new;        // new master being negotiated
} vdj_backline_t;

// Local VCDJ
typedef struct {
    char*               ip_address;      // string format
    struct sockaddr_in* ip_addr;         // e.g. "192.168.1.5"
    struct sockaddr_in* netmask;         // e.g. "255.255.255.0"
    struct sockaddr_in* broadcast_addr;  // e.g. "192.168.1.255"

    unsigned char       ip[4];        // Stored in the format CDJs expect it, 4 bytes reading forward 192 168 2 1
    uint8_t             mac[6];       // Stored in the format CDJs expect it, 6 bytes reading forward.

    int                 discovery_socket_fd;         // 50000
    int                 discovery_unicast_socket_fd; // 50000
    int                 beat_socket_fd;              // 50001
    int                 beat_unicast_socket_fd;      // 50001
    int                 update_socket_fd;            // 50002
    int                 send_socket_fd;              // any port

    uint8_t             model;          // CDJ_VDJ CDJ_XDJ or CDJ_CDJ
    uint8_t             player_id;      // player id 1 to 4 for CDJs rekordbox is 17, we use 5 by default
    uint8_t             device_type;    // device type
    uint8_t             reqid;          // last sent request id, aka n, used to identify individual discovery responses
    int                 refs;           // todo referrence counting

    // state
    vdj_backline_t*     backline;       // info we have about other CDJs on the DJ Pro Link
    int32_t             pitch;          // (tempo not necessarily pitch)
    uint8_t             master_state;   // my own state flags
    uint32_t            status_counter; // how many status packets have been sent
    uint8_t             master;         // 0x01 I think i am master
    int8_t              master_req;     // some other player wants to be master or -1
    float               bpm;            // my virtual device's bpm
    struct timespec     last_beat;      // nanosecond time of my last beat (may use MONOTONIC clock)
    uint8_t             active;         // we chose this to mean playing, but there are other states for CDJs
    uint8_t             bar_index;      // 0 - 3 index position in the bar
    void*               client;         // if anyone wants to hook to our callbacks (e.g. adj_seq_info_t* adj)
    unsigned int        auto_id:1;      // automatically assign id
    unsigned int        have_id:1;      // got an id assigned
    unsigned int        follow_master:1; // vdj should track master (in adj)
} vdj_t;

typedef struct  {
    vdj_t* v;
    void* handler;
} vdj_thread_info;



// handlers

// raw packet handlers

typedef void (*vdj_discovery_handler)(vdj_t* v, uint8_t* packet, uint16_t packet_length);
typedef void (*vdj_discovery_unicast_handler)(vdj_t* v, cdj_discovery_packet_t* d_pkt);
typedef void (*vdj_update_handler)(vdj_t* v, uint8_t* packet, uint16_t packet_length);
typedef void (*vdj_beat_handler)(vdj_t* v, uint8_t* packet, uint16_t packet_length);

// managed packet handlers

typedef void (*vdj_discovery_ph)(vdj_t* v, cdj_discovery_packet_t* d_pkt);
typedef void (*vdj_discovery_unicast_ph)(vdj_t* v, cdj_discovery_packet_t* d_pkt);
typedef void (*vdj_update_ph)(vdj_t* v, cdj_cdj_status_packet_t* cs_pkt);
typedef void (*vdj_beat_ph)(vdj_t* v, cdj_beat_packet_t* b_pkt);
typedef void (*vdj_beat_unicast_ph)(vdj_t* v, cdj_beat_packet_t* b_pkt);

// allocs
vdj_t* vdj_init(uint32_t flags);
vdj_t* vdj_init_iface(char* interface, uint32_t flags);
vdj_t* vdj_init_net(uint8_t* mac, char* ip_address, struct sockaddr_in* ip_addr, struct sockaddr_in* netmask, struct sockaddr_in *broadcast_addr, uint32_t flags);
int vdj_destroy(vdj_t* v);

void vdj_load_player_id(vdj_t* v);
void vdj_save_player_id(vdj_t* v);

int vdj_open_sockets(vdj_t* v);
int vdj_open_broadcast_sockets(vdj_t* v);
int vdj_exec_discovery(vdj_t* v);
int vdj_close_sockets(vdj_t* v);
void vdj_print_sockaddr(char* context, struct sockaddr_in* ip);

// broadcast a single keepalive
void vdj_send_keepalive(vdj_t* v);
// unicast our status to all members
int vdj_send_status(vdj_t* v);
// broadcast a beat, 
// bpm does not have to be correct but its rendered on the CDJ so if bpm is not what is reported
// the DJ will not know
void vdj_broadcast_beat(vdj_t* v, float bpm, uint8_t bar_pos);
// set not active if last beat was more than a second ago
void vdj_expire_play_state(vdj_t* v);
void vdj_set_playing(vdj_t* v, int playing);

// sendto on different sockets and ports
int vdj_sendto_discovery(vdj_t* v, uint8_t* packet, uint16_t packet_length);
int vdj_sendto_beat(vdj_t* v, uint8_t* packet, uint16_t packet_length);
int vdj_sendto_update(vdj_t* v, struct sockaddr_in* dest, uint8_t* packet, uint16_t packet_length);

// internal threads

// reads discovery broadcasts
int vdj_init_discovery_thread(vdj_t* v, vdj_discovery_handler discovery_handler);
void vdj_stop_discovery_thread(vdj_t* v);

// reads beat broadcasts
int vdj_init_beat_thread(vdj_t* v, vdj_beat_handler beat_handler);
void vdj_stop_beat_thread(vdj_t* v);

// read direct messages
int vdj_init_update_thread(vdj_t* v, vdj_update_handler update_handler);
void vdj_stop_update_thread(vdj_t* v);

int vdj_init_status_thread(vdj_t* v);
void vdj_stop_status_thread(vdj_t* v);

int vdj_init_managed_update_thread(vdj_t* v, vdj_update_ph update_ph);
void vdj_stop_managed_update_thread(vdj_t* v);

int vdj_init_managed_beat_thread(vdj_t* v, vdj_beat_ph beat_ph);
void vdj_stop_managed_beat_thread(vdj_t* v);

void vdj_stop_threads(vdj_t* v);

// exposed for vdj_pselect
void vdj_handle_managed_update_datagram(vdj_t* v, vdj_update_ph update_ph, uint8_t* packet, uint16_t len);
void vdj_handle_managed_beat_datagram(vdj_t* v, vdj_beat_ph beat_ph, uint8_t* packet, uint16_t len);
void vdj_handle_managed_beat_unicast_datagram(vdj_t* v, vdj_beat_unicast_ph beat_unicast_ph, unsigned char* packet, uint16_t len);
void vdj_set_bpm(vdj_t* v, float bpm);

// backline management

// get link member or NULL, does not return self
vdj_link_member_t* vdj_get_link_member(vdj_t* v, uint8_t player_id);
// add a new member, checks not self
vdj_link_member_t* vdj_new_link_member(vdj_t* v, cdj_discovery_packet_t* d_pkt);

void vdj_update_link_member(vdj_link_member_t* m, uint32_t ip);
uint8_t vdj_link_member_count(vdj_t* v);
int64_t vdj_time_diff(vdj_t* v, vdj_link_member_t* m);
struct sockaddr_in* vdj_alloc_dest_addr(vdj_link_member_t* m, uint16_t port);

#endif /* _VDJ_H_INCLUDED_ */
