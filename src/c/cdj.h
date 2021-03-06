/**
 * Common library for Pioneer CDJ protocol code.
 */

#ifndef _LIBCDJ_H_INCLUDED_
#define _LIBCDJ_H_INCLUDED_

#include <inttypes.h>
#include <time.h>

// Constants //

#define CDJ_OK              0
#define CDJ_ERROR           1

#define CDJ_MAGIC_NUMBER        "Qspt1WmJOL"
#define CDJ_DEVICE_MODEL_LENGTH  0x14
#define CDJ_PACKET_TYPE_OFFSET   0x0a
/**
 * The sequence of 10 bytes which begins all UDP packets sent in the protocol. (Qspt1WmJOL)
 */
#define CDJ_MAGIC_HEADER_LENGTH   10


// CDJs broadcast keepalives to this port, and unicast for auto id assigment
#define CDJ_DISCOVERY_PORT      50000
// CDJs broadcast beat timing info to this port also used unicast for tempo master handoff DMs
#define CDJ_BEAT_PORT           50001
// CDJs listen to unicast messages on this port for status updates
#define CDJ_UPDATE_PORT         50002

// I have no proof but I suspect the first two bytes of a message are major and minor protocol version
#define CDJ_DISCOVERY_VERSION   0x0102
#define CDJ_BEAT_VERSION        0x0100
#define CDJ_UPDATE_VERSION      0x0103 // CDJs send 0x0100

/**
 * Keepalives should be broadcast with this frequency in millis to 50001
 */
#define CDJ_KEEPALIVE_INTERVAL    2000
/**
 * How long to wait for replies during discovery phase or id assignment
 */
#define CDJ_REPLY_WAIT           300
/**
 * Status should be sent unicast to all link members with this frequency
 */
#define CDJ_STATUS_INTERVAL       200

// The discovery channel, message types
#define CDJ_STAGE1_DISCOVERY     0x00 // mac discovery (on 50000) LinkMember? TODO rename to just discovery? what the betting this mac_use and insignificant if hardware macs are used
#define CDJ_ID_USE_REQ           0x02 // XDJ-1000 broadcasts this on discovery port
#define CDJ_ID_USE_RESP          0x03 // XDJ-1000 unicasts this back if we try to use its player_id
#define CDJ_ID_SET_REQ           0x04 // send to indicate we have set our player_id, XDJ-1000 will complain immediatly with CDJ_COLLISION
#define CDJ_ID_SET_RESP          0x05 // XDJ-1000 unicasts this back if thinks we are OK
#define CDJ_KEEP_ALIVE           0x06 // sent to clarify that we are still here
#define CDJ_COLLISION            0x08 // Player number clash
#define CDJ_DISCOVERY            0x0a // also initial discovery (on 50000) (potentialy compatibiltiy request)

// The status /control channel
#define CDJ_FADER_START_COMMAND  0x02
#define CDJ_CHANNELS_ON_AIR      0x03
#define CDJ_GOODBYE              0x04 // Disconnect?
#define CDJ_MEDIA_QRY            0x05
#define CDJ_MEDIA_RESP           0x06 // 
#define CDJ_STATUS               0x0a // MySystemStatus ?
#define CDJ_LOAD_TRACK_COMMAND   0x19
#define CDJ_LOAD_TRACK_ACK       0x1a
#define CDJ_MASTER_REQ           0x26 // TimeServerRequest?
#define CDJ_MASTER_RESP          0x27
#define CDJ_BEAT                 0x28
#define CDJ_MIXER_STATUS         0x29
#define CDJ_SYNC_CONTROL         0x2a

// supported models
#define CDJ_CDJ                 'C'
#define CDJ_XDJ                 'X'
#define CDJ_VDJ                 'V'

// CDJ status flags
#define CDJ_STAT_SLOT_NONE      0x00
#define CDJ_STAT_SLOT_CD        0x01
#define CDJ_STAT_SLOT_SD        0x02
#define CDJ_STAT_SLOT_USB       0x03
#define CDJ_STAT_SLOT_RB        0x04 // rekordbox

// cdj_status_master_state() flags  02 master but no beat grid
#define CDJ_MASTER_STATE_OFF    0x00
#define CDJ_MASTER_STATE_ON     0x01
#define CDJ_MASTER_STATE_KO     0x02

// bit masks for the nexus statues
#define CDJ_STAT_FLAG_BASE      0x04 | 0x80  // when sending the is the base state
#define CDJ_STAT_FLAG_UNKNOWN3  0x80
#define CDJ_STAT_FLAG_PLAY      0x40
#define CDJ_STAT_FLAG_MASTER    0x20
#define CDJ_STAT_FLAG_SYNC      0x10
#define CDJ_STAT_FLAG_ONAIR     0x08 // DJM thinks this player is audible
#define CDJ_STAT_FLAG_UNKNOWN2  0x04 // not known
#define CDJ_STAT_FLAG_BPM       0x02
#define CDJ_STAT_FLAG_UNKNOWN1  0x01

#define CDJ_PITCH_LOW            0x00000000
#define CDJ_PITCH_NORMAL         0x00100000
#define CDJ_PITCH_HIGH           0x00200000

/**
 * The smallest packet size from which CDJ_STATUS can be constructed. Anything less than this and we are missing
 * crucial information.
 */
#define CDJ_STATUS_MINIMUM_PACKET_SIZE 0xCC   // TODO per PDF old players send less

#define CDJ_MAX_DJM_CHANNELS           4      // max players supported by on air  packets

#define CDJ_CLOCK CLOCK_REALTIME  // @see https://linux.die.net/man/2/clock_gettime

// Data Structures

typedef struct {
    uint16_t   len;
    uint8_t*   data;
} cdj_str_t;

// Update (packets) types

typedef struct {
    uint8_t*       data;
    uint16_t       len;
    uint8_t        type;
} cdj_generic_packet_t;

typedef struct {
    uint8_t*       data;
    uint16_t       len;
    uint8_t        type;
    uint8_t        sub_type;
    uint8_t        player_id;
    uint8_t        mac[6];
    uint32_t       ip;
} cdj_discovery_packet_t;

typedef struct {
    uint8_t*        data;
    uint16_t        len;
    uint8_t         type;
    uint8_t         player_id;
    struct timespec timestamp;
    uint8_t         bar_pos;
    float           bpm;
} cdj_beat_packet_t;

typedef struct {
    uint8_t*       data;
    uint16_t       len;
    uint8_t        type;
    uint8_t        player_id;
    float          bpm;
} cdj_mixer_status_packet_t;

typedef struct {
    uint8_t*       data;
    uint16_t       len;
    uint8_t        type;
    uint8_t        player_id;
    float          bpm;
    uint8_t        flags;
} cdj_cdj_status_packet_t;

// Constructors

// port = 50000
cdj_discovery_packet_t*
cdj_new_discovery_packet(uint8_t* packet, uint16_t length);

// port = 50001
cdj_beat_packet_t*
cdj_new_beat_packet(uint8_t* packet, uint16_t length);

// port = 50002
cdj_mixer_status_packet_t*
cdj_new_mixer_status_packet(uint8_t* packet, uint16_t length);

// port = 50002
cdj_cdj_status_packet_t*
cdj_new_cdj_status_packet(uint8_t* packet, uint16_t length);


// Functions

// Protocol util functions

// frame type
const char* cdj_type_to_string(uint16_t port, uint8_t type, uint8_t sub_type);
// return malloced bpm to 3 digits, two decimal places, ProLink protocol has no more precision
char* cdj_bpm_to_string(float bpm);
// return malloced pitch value (as it si represented in ProLink protocol) as +0.00%
char* cdj_pitch_to_string(uint32_t pitch);
// return malloced emoji representation of CDJ state
char* cdj_flags_to_emoji(uint8_t flags);
// returns malloced char representation of CDJ state
char* cdj_flags_to_chars(uint8_t flags);
// returns malloced ANSI escape sequence representation of CDJ state
char* cdj_flags_to_term(uint8_t flags);

// pitch to percentage adjustment e.g. +8.22% or -0.25%
double cdj_pitch_to_percentage(uint32_t pitch);
// pitch to multiplier e.g. 1.0822 or 0.9775
double cdj_pitch_to_multiplier(uint32_t pitch);
float cdj_calculated_bpm(uint16_t track_bpm, uint32_t pitch);
uint32_t cdj_beat_millis(float bpm);
uint16_t cdj_bpm_to_int(float bpm);

int cdj_ip_format(const char* ip_address, unsigned char* ip);
struct sockaddr_in* cdj_ip_decode(uint32_t ip);
uint32_t cdj_ip_encode(struct sockaddr_in* ip_addr);

uint16_t cdj_header_len(uint16_t port, uint8_t type);

// Packet inspection functions

// generic
uint8_t cdj_packet_type(uint8_t* data, uint16_t len);

// returns 0 (CDJ_OK) on success
int cdj_validate_header(uint8_t* data, uint16_t len);

// allocs and returns cdj model .e.g "XDJ-1000" N.B. cdj_*_model dont alloc
char* cdj_model_name(uint8_t* packet, uint16_t len, uint16_t port);

// read data from discovery packets

// returns a pointer to data in the packet
char* cdj_discovery_model(cdj_discovery_packet_t* d_pkt);
uint8_t cdj_discovery_sub_type(cdj_discovery_packet_t* d_pkt);
uint8_t cdj_discovery_player_id(cdj_discovery_packet_t* d_pkt);
unsigned int cdj_discovery_ip(cdj_discovery_packet_t* d_pkt);
// returns a pointer to data in the packet, only read 6 bytes data also available at d_pkt->mac
uint8_t* cdj_discovery_mac(cdj_discovery_packet_t* d_pkt);
// returns true if packet is id in use response for this player's most recent request
int cdj_discovery_is_id_in_use(cdj_discovery_packet_t* d_pkt, uint8_t player_id, uint8_t reqid);

// read data from status packets

// returns a pointer to data in the packet
char* cdj_status_model(cdj_cdj_status_packet_t* cs_pkt);
uint8_t cdj_status_player_id(cdj_cdj_status_packet_t* cs_pkt);
uint8_t cdj_status_active(cdj_cdj_status_packet_t* cs_pkt);
uint8_t cdj_status_playing_from(cdj_cdj_status_packet_t* cs_pkt);
uint8_t cdj_status_playing_from_slot(cdj_cdj_status_packet_t* cs_pkt);
uint8_t cdj_status_track_type(cdj_cdj_status_packet_t* cs_pkt);
uint32_t cdj_status_track_id(cdj_cdj_status_packet_t* cs_pkt);
uint32_t cdj_status_track_number(cdj_cdj_status_packet_t* cs_pkt);
uint32_t cdj_status_cd_data_type(cdj_cdj_status_packet_t* cs_pkt);
// return P1 values e.g. 03 playing normally, 04 in a loop
uint8_t cdj_status_play_mode(cdj_cdj_status_packet_t* cs_pkt);
uint32_t cdj_status_sync_counter(cdj_cdj_status_packet_t* cs_pkt);
// a bit mask, of play state, e.g. flag & with CDJ_STAT_FLAG_* or pass to cdj_flag_to_emoji()
uint8_t cdj_status_flags(cdj_cdj_status_packet_t* cs_pkt);
uint32_t cdj_status_sync_state(cdj_cdj_status_packet_t* cs_pkt);
uint8_t cdj_status_master_state(cdj_cdj_status_packet_t* cs_pkt);
int8_t cdj_status_new_master(cdj_cdj_status_packet_t* cs_pkt);
uint16_t cdj_status_bpm(cdj_cdj_status_packet_t* cs_pkt);
uint32_t cdj_status_pitch(cdj_cdj_status_packet_t* cs_pkt);
float cdj_status_calculated_bpm(cdj_cdj_status_packet_t* cs_pkt);
uint32_t cdj_status_counter(cdj_cdj_status_packet_t* cs_pkt);


// read data from beat packets, N.B. the protocol support tracks that dont have constant BPM.
/**
 * retuns a pointer to the model name in the packet, data is free() by managed threads
 */
char* cdj_beat_model(cdj_beat_packet_t* cs_pkt);
/**
 * BPM of the track at its current position, excluding pitch adjustment.
 */
uint16_t cdj_beat_bpm(cdj_beat_packet_t* cs_pkt);
/**
 * Pitch value of the current slider position, (or virtual pos issynced)
 */
uint32_t cdj_beat_pitch(cdj_beat_packet_t* cs_pkt);
/**
 * BPM taking into consideration the pitch.
 */
float cdj_beat_calculated_bpm(cdj_beat_packet_t* b_pkt);
/**
 * BPM of the next 7 beats (exc. pitch) calculated from millisecond offsets.
 */
double cdj_beat_measured_bpm(cdj_beat_packet_t* b_pkt);
/**
 * milliseconds to the next beat (excluding pitch) 
 */
uint32_t cdj_beat_next(cdj_beat_packet_t* b_pkt);
/**
 * player_id extracted form the beat packet
 */
uint8_t cdj_beat_player_id(cdj_beat_packet_t* b_pkt);
/**
 * Position of the beat in the bar, 1,2 3 or 4, because DJs only play house.
 */
uint8_t cdj_beat_bar_pos(cdj_beat_packet_t* b_pkt);
/**
 * new master in a CDJ_MASTER_REQ or CDJ_MASTER_RESP message
 */
uint8_t cdj_beat_master(cdj_beat_packet_t* b_pkt);
/**
 * Seems to return a 1 or 0 in a CDJ_MASTER_RESP
 */
uint32_t cdj_beat_master_ok(cdj_beat_packet_t* b_pkt);

// handshake
uint8_t* cdj_create_initial_discovery_packet(uint16_t* length, unsigned char model);
uint8_t* cdj_create_stage1_discovery_packet(uint16_t* length, unsigned char model, uint8_t* mac, uint8_t reqid);
uint8_t* cdj_create_id_use_req_packet(uint16_t* length, unsigned char model, uint8_t* ip, uint8_t* mac, uint8_t player_id, uint8_t reqid);
uint8_t* cdj_create_id_use_resp_packet(uint16_t* length, unsigned char model, uint8_t player_id, uint8_t* ip);
uint8_t* cdj_create_id_set_req_packet(uint16_t* length, unsigned char model, uint8_t player_id, uint8_t reqid);

uint8_t* cdj_create_keepalive_packet(uint16_t* length, unsigned char model, uint8_t* ip, uint8_t* mac, uint8_t player_id, uint8_t member_count);
uint8_t* cdj_create_id_collision_packet(uint16_t* length, unsigned char model, uint8_t player_id, uint8_t* ip);

uint8_t  cdj_inc_stage1_discovery_packet(uint8_t* packet);
uint8_t  cdj_inc_id_use_req_packet(uint8_t* packet);
void     cdj_mod_id_use_req_packet_player_id(uint8_t* packet, uint8_t player_id);
uint8_t  cdj_inc_id_set_req_packet(uint8_t* packet);

uint8_t* cdj_create_beat_packet(uint16_t* length, unsigned char model, uint8_t player_id, float bpm, uint8_t bar_index);

uint8_t* cdj_create_status_packet(uint16_t* length, unsigned char model, uint8_t player_id,
    float bpm, uint8_t bar_index, uint8_t active, uint8_t master, int8_t new_master, uint32_t sync_counter,
    uint32_t n);

uint8_t* cdj_create_master_request_packet(uint16_t* length, unsigned char model, uint8_t player_id);
uint8_t* cdj_create_master_response_packet(uint16_t* length, unsigned char model, uint8_t player_id);




void cdj_print_packet(uint8_t* packet, uint16_t length, uint16_t port);
void cdj_fprint_packet(FILE* f, uint8_t* packet, uint16_t length, uint16_t port);

#endif /* _LIBCDJ_H_INCLUDED_ */
