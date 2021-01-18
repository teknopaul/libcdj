/**
 * Common library for Pioneer CDJ protocol code.
 */

#ifndef _LIBCDJ_H_INCLUDED_
#define _LIBCDJ_H_INCLUDED_

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

// Constants //

#define CDJ_OK              0
#define CDJ_ERROR           1

#define CDJ_MAGIC_NUMBER        "Qspt1WmJOL"
#define CDJ_DEVICE_NAME_LENGTH   0x14
#define CDJ_DEVICE_NUMBER_OFFSET 0x24
#define CDJ_PACKET_LENGTH_OFFSET 0x23
#define CDJ_PACKET_TYPE_OFFSET   0x0a
/**
 * The sequence of 10 bytes which begins all UDP packets sent in the protocol. (Qspt1WmJOL)
 */
#define CDJ_MAGIC_HEADER_LENGTH   10


// CDJs broadcast keepalives to this port
#define CDJ_DISCOVERY_PORT    50000
// CDJs broadcast beat timing info to this port also used unicast for tempo master handoff DMs
#define CDJ_BROADCAST_PORT    50001
// CDJs listen to unicast messages on this port for status updates
#define CDJ_UPDATE_PORT       50002


/**
 * Keepalives should be broadcast with this frequency in millis to 50001
 */
#define CDJ_KEEPALIVE_INTERVAL    1500
/**
 * Status should be sent unicast to all link members with this frequency
 */
#define CDJ_STATUS_INTERVAL       200

// The discovery channel, message types
#define CDJ_PLAYER_NUM_DISCOVERY 0x00 // player_id and mac discovery (on 50000) LinkMember?
#define CDJ_STAGE2_DISCOVERY     0x02 // XDJ-1000 sends this on discovery as well as IP discovery
#define CDJ_FINAL_DISCOVERY      0x04 // final stage discovery (on 50000)
#define CDJ_KEEP_ALIVE           0x06 // 
#define CDJ_DISCOVERY            0x0a // also initial discovery (on 50000)

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
#define CDJ_CDJ             'C'
#define CDJ_XDJ             'X'
#define CDJ_VDJ             'V'

// CDJ status flags
#define CDJ_STAT_SLOT_NONE      0x00
#define CDJ_STAT_SLOT_CD        0x01
#define CDJ_STAT_SLOT_SD        0x02
#define CDJ_STAT_SLOT_USB       0x03
#define CDJ_STAT_SLOT_RB        0x04 // rekordbox

// CDJ devices types (observered knot known)
#define CDJ_DEV_TYPE_DJM       2
#define CDJ_DEV_TYPE_CDJ       1
#define CDJ_DEV_TYPE_XDJ       2

// cdj_status_master_state() flags  02 master but no beat grid
#define CDJ_MASTER_STATE_ON     0x00
#define CDJ_MASTER_STATE_OFF    0x01
#define CDJ_MASTER_STATE_KO     0x02

// bit masks for the nexus statues
#define CDJ_PLAY_STATE_BASE      0x04 | 0x80
#define CDJ_PLAY_STATE_PLAY      0x40
#define CDJ_PLAY_STATE_MASTER    0x20
#define CDJ_PLAY_STATE_SYNC      0x10
#define CDJ_PLAY_STATE_ONAIR     0x08  // DJM thinks this player is audible

#define CDJ_PITCH_LOW            0x00000000
#define CDJ_PITCH_NORMAL         0x00100000
#define CDJ_PITCH_HIGH           0x00200000

/**
 * The smallest packet size from which CDJ_STATUS can be constructed. Anything less than this and we are missing
 * crucial information.
 */
#define CDJ_STATUS_MINIMUM_PACKET_SIZE 0xCC   // TODO per PDF old players send less

#define CDJ_MAX_DJM_CHANNELS           4      // max players supported by on air  packets

// Data Structures

typedef struct {
    unsigned char* data;
    unsigned int   len;
} cdj_str_t;

// Update (packets) types

typedef struct {
    unsigned char* data;
    unsigned int   len;
    unsigned char  type;
} cdj_generic_packet_t;

typedef struct {
    unsigned char* data;
    unsigned int   len;
    unsigned char  type;
    unsigned char  player_id;
    unsigned char  device_type;
    unsigned char  mac[6];
    unsigned int   ip;
} cdj_discovery_packet_t;

typedef struct {
    unsigned char* data;
    unsigned int   len;
    unsigned char  type;
    unsigned char  player_id;
    unsigned char  device_type;
    unsigned char  bar_pos;
    float          bpm;
} cdj_beat_packet_t;

typedef struct {
    unsigned char* data;
    unsigned int   len;
    unsigned char  type;
    unsigned char  player_id;
    float          bpm;  // real BPM calcualted from base & pitch
} cdj_mixer_status_packet_t;

typedef struct {
    unsigned char* data;
    unsigned int   len;
    unsigned char  type;
    unsigned char  player_id;
    float          bpm;  // real BPM calcualted from base & pitch
} cdj_cdj_status_packet_t;

// Constructors


// port = 50000
cdj_discovery_packet_t*
cdj_new_discovery_packet(unsigned char* packet, int length, int port);

cdj_beat_packet_t*
cdj_new_beat_packet(unsigned char* packet, int length, int port);

cdj_mixer_status_packet_t*
cdj_new_mixer_status_packet(unsigned char* packet, int length, int port);

cdj_cdj_status_packet_t*
cdj_new_cdj_status_packet(unsigned char* packet, int length, int port);



void 
cdj_print_packet(unsigned char* packet, int length, int port);

// Global variables

int64_t cdj_tempo_master;
int64_t cdj_tempo;

// Functions

// Protocol util funciotns

// frame type
const char* cdj_type_to_string(int port, unsigned char type);
double cdj_pitch_to_percentage(int pitch);
double cdj_pitch_to_multiplier(int pitch);
float cdj_calculated_bpm(unsigned int track_bpm, unsigned int pitch);

int cdj_ip_format(const char* ip_address, unsigned char* ip);
struct sockaddr_in* cdj_ip_decode(unsigned int ip);
unsigned int cdj_ip_encode(struct sockaddr_in* ip_addr);

int cdj_header_len(int port, unsigned char type);

// Packet inspection functions
char* cdj_model_name(unsigned char* packet, int len, int port);

unsigned char cdj_discovery_player_id(cdj_discovery_packet_t* a_pkt);
unsigned char cdj_discovery_device_type(cdj_discovery_packet_t* a_pkt);
unsigned int cdj_discovery_ip(cdj_discovery_packet_t* a_pkt);
// returns malloced data
char* cdj_discovery_mac(cdj_discovery_packet_t* a_pkt);


// status packets
unsigned char cdj_status_player_id(cdj_cdj_status_packet_t* cs_pkt);
unsigned char cdj_status_active(cdj_cdj_status_packet_t* cs_pkt);
unsigned char cdj_status_playing_from(cdj_cdj_status_packet_t* cs_pkt);
unsigned char cdj_status_playing_from_slot(cdj_cdj_status_packet_t* cs_pkt);
unsigned char cdj_status_track_type(cdj_cdj_status_packet_t* cs_pkt);
unsigned int cdj_status_track_id(cdj_cdj_status_packet_t* cs_pkt);
unsigned int cdj_status_track_number(cdj_cdj_status_packet_t* cs_pkt);
unsigned int cdj_status_cd_data_type(cdj_cdj_status_packet_t* cs_pkt);
unsigned int cdj_status_play_mode(cdj_cdj_status_packet_t* cs_pkt);
unsigned int cdj_status_sync_counter(cdj_cdj_status_packet_t* cs_pkt);
unsigned char cdj_status_sync_flags(cdj_cdj_status_packet_t* cs_pkt);
unsigned int cdj_status_sync_state(cdj_cdj_status_packet_t* cs_pkt);
unsigned char cdj_status_master_state(cdj_cdj_status_packet_t* cs_pkt);
unsigned char cdj_status_new_master(cdj_cdj_status_packet_t* cs_pkt);

unsigned int cdj_status_bpm(cdj_cdj_status_packet_t* cs_pkt);
unsigned int cdj_status_pitch(cdj_cdj_status_packet_t* cs_pkt);
float cdj_status_calculated_bpm(cdj_cdj_status_packet_t* cs_pkt);
unsigned char cdj_packet_type(unsigned char* data, int len);
int cdj_validate_header(unsigned char* data, int len, int port, int* packet_type);




// beat packets
unsigned int cdj_beat_bpm(cdj_beat_packet_t* cs_pkt);
unsigned int cdj_beat_pitch(cdj_beat_packet_t* cs_pkt);
float cdj_beat_calculated_bpm(cdj_beat_packet_t* b_pkt);
unsigned char cdj_beat_player_id(cdj_beat_packet_t* cs_pkt);
unsigned char cdj_beat_device_type(cdj_beat_packet_t* b_pkt);
unsigned char cdj_beat_bar_pos(cdj_beat_packet_t* b_pkt);
// handshake
unsigned char* cdj_create_initial_discovery_packet(int* length, unsigned char model, char device_type);
unsigned char* cdj_create_stage1_discovery_packet(int* length, unsigned char model, char device_type, unsigned char *mac, unsigned char n);
unsigned char* cdj_create_stage2_discovery_packet(int* length, unsigned char model, char device_type, unsigned char* ip, unsigned char* mac, unsigned char player_id, unsigned char n);
unsigned char* cdj_create_final_discovery_packet(int* length, unsigned char model, char device_type, unsigned char player_id, unsigned char n);
unsigned char* cdj_create_keepalive_packet(int* length, unsigned char model, char device_type, unsigned char* ip, unsigned char* mac, unsigned char player_id);

void           cdj_inc_stage1_discovery_packet(unsigned char* packet);
void           cdj_inc_stage2_discovery_packet(unsigned char* packet);
void           cdj_inc_final_discovery_packet(unsigned char* packet);


unsigned char* cdj_create_beat_packet(int* length, unsigned char model, char device_type, unsigned char player_id, float bpm, unsigned char bar_pos);

unsigned char* cdj_create_status_packet(int* length, unsigned char model, unsigned char player_id, float bpm, unsigned char bar_index, char master, unsigned int n);

unsigned char* cdj_create_master_request_packet(int* length, unsigned char model, unsigned char player_id);
unsigned char* cdj_create_master_response_packet(int* length, unsigned char model, unsigned char player_id);

#endif /* _LIBCDJ_H_INCLUDED_ */
