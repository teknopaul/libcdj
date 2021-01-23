

/**
 * Common library for Pioneer CDJ protocol code (ProLink)
 *
 * N.B. this is reverse engineed from packet snooping.
 * 
 *
 * https://djl-analysis.deepsymmetry.org/djl-analysis/packets.html
 *
 * @author teknopaul
 */

#include <stdio.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "cdj.h"


unsigned char CDJ_MAGIC_HEADER[10] = {0x51, 0x73, 0x70, 0x74, 0x31, 0x57, 0x6d, 0x4a, 0x4f, 0x4c}; // reads Qspt1WmJOL

const char*
cdj_type_to_string(uint16_t port, unsigned char type, unsigned char sub_type)
{
    switch (port) {
        case CDJ_DISCOVERY_PORT : {
            switch (type) {
                case 0x00 : {
                    if (sub_type == 0) return "CDJ_STAGE1_DISCOVERY";
                }
                case 0x01 : {
                    if (sub_type == 1) return "CDJ_MIXER_ASSIGN_INTENT";
                }
                case 0x02 : {
                    if (sub_type == 0) return "CDJ_ID_USE_REQ";
                }
                case 0x03 : {
                    if (sub_type == 0) return "CDJ_ID_USE_RESP";
                    if (sub_type == 1) return "CDJ_MIXER_ASSIGN_CHANNEL";
                }
                case 0x04 : {
                    if (sub_type == 0) return "CDJ_ID_SET_REQ";
                }
                case 0x05 : {
                    if (sub_type == 0) return "CDJ_ID_SET_RESP";
                    if (sub_type == 1) return "CDJ_MIXER_ASSIGN_FINISHED";
                }
                case 0x06 : {
                    if (sub_type == 0) return "CDJ_DEVICE_KEEP_ALIVE";
                }
                case 0x08 : {
                    if (sub_type == 0) return "CDJ_COLLISION";
                }
                case 0x0a : {
                    if (sub_type == 0) return "CDJ_DISCOVERY";
                }
            }
        }
        case CDJ_BROADCAST_PORT :
        case CDJ_UPDATE_PORT :
        default : {
            switch (type) {
                case 0x02 : {
                    return "CDJ_FADER_START_COMMAND";
                }
                case 0x03 : {
                    return "CDJ_CHANNELS_ON_AIR";
                }
                case 0x04 : {
                    return "CDJ_GOODBYE";
                }
                case 0x05 : {
                    return "CDJ_MEDIA_QRY";
                }
                case 0x06 : {
                    return "CDJ_MEDIA_RESP";
                }
                case 0x0a : {
                    return "CDJ_STATUS";
                }
                case 0x19 : {
                    return "CDJ_LOAD_TRACK_COMMAND";
                }
                case 0x1a : {
                    return "CDJ_LOAD_TRACK_ACK";
                }
                case 0x26 : {
                    return "CDJ_MASTER_HANDOFF_REQ";
                }
                case 0x27 : {
                    return "CDJ_MASTER_HANDOFF_RESP";
                }
                case 0x28 : {
                    return "CDJ_BEAT";
                }
                case 0x29 : {
                    return "CDJ_MIXER_STATUS";
                }
                case 0x2a : {
                    return "CDJ_SYNC_CONTROL";
                }
                default : return "UNKNOWN";
            }
        }
    }
}

//SNIP_bpm_madness
char*
cdj_bpm_to_string(float bpm)
{
    char* bpm_s = calloc(1, 24);
    if (bpm_s) snprintf(bpm_s, 24, "%06.2f", bpm);
    return bpm_s;
}

char*
cdj_pitch_to_string(uint32_t pitch)
{
    char* pitch_s = calloc(1, 24);
    if (pitch_s) printf(pitch_s, "%+5.2f%%", cdj_pitch_to_percentage(pitch));
    return pitch_s;
}
//SNIP_bpm_madness

char*
cdj_flags_to_emoji(unsigned char flags)
{
    char* state = calloc(1, 24);
    if (state) {
        snprintf(state, 24, "%s %s%s%s ", 
            flags & CDJ_STAT_FLAG_PLAY   ? "▶️" : "⏸️",
            flags & CDJ_STAT_FLAG_SYNC   ? "🟡" : "⚪",
            flags & CDJ_STAT_FLAG_MASTER ? "🔴" : "⚪",
            flags & CDJ_STAT_FLAG_ONAIR  ? "🔇" : "🔊"
            );
    }
    return state;
}

char*
cdj_flags_to_chars(unsigned char flags)
{
    char* state = calloc(1, 5);
    if (state) {
        snprintf(state, 5, "%c%c%c%c", 
            flags & CDJ_STAT_FLAG_PLAY   ? '>' : '"',
            flags & CDJ_STAT_FLAG_SYNC   ? 's' : ' ',
            flags & CDJ_STAT_FLAG_MASTER ? 'm' : ' ',
            flags & CDJ_STAT_FLAG_ONAIR  ? '*' : ' '
            );
    }
    return state;
}

char*
cdj_flags_to_term(unsigned char flags)
{
    char* state = calloc(1, 44);
    if (state) {
        snprintf(state, 44, "%s%s%s%s", 
            flags & CDJ_STAT_FLAG_PLAY   ? "\033[5m>\033[0m"  : " ",
            flags & CDJ_STAT_FLAG_SYNC   ? "\033[93ms\033[0m" : " ",
            flags & CDJ_STAT_FLAG_MASTER ? "\033[31mM\033[0m" : " ",
            flags & CDJ_STAT_FLAG_ONAIR  ? "\033[5m*\033[0m"  : " "
            );
    }
    return state;
}

uint16_t
cdj_header_len(uint16_t port, unsigned char type)
{
    if (port == CDJ_DISCOVERY_PORT) {
        return 12;
    }

    return 11;
}

/**
 * Convert string IP address to CDJ message format.
 */
int
cdj_ip_format(const char* ip_address, unsigned char* ip)
{
    int addr = inet_addr(ip_address);
    if (addr == INADDR_NONE) return CDJ_ERROR;
    ip[3] = (unsigned char) (addr >> 24);
    ip[2] = (unsigned char) (addr >> 16);
    ip[1] = (unsigned char) (addr >> 8);
    ip[0] = (unsigned char) (addr >> 0);
    return CDJ_OK;
}

// convert CDJ format to Linux standard
struct sockaddr_in*
cdj_ip_decode(uint32_t ip)
{
    struct sockaddr_in* ip_addr = (struct sockaddr_in*) calloc(1, sizeof(struct sockaddr_in*));
    if (ip_addr) {
        ip_addr->sin_family = AF_INET;
        ip_addr->sin_addr.s_addr = htonl(ip);
    }
    return ip_addr;
}

// convert to CDJ format
uint32_t
cdj_ip_encode(struct sockaddr_in* ip_addr)
{
    return ntohl(ip_addr->sin_addr.s_addr);
}


/**
 * validate that a received packet appears to be the correct protocol
 */
int
cdj_validate_header(unsigned char* data, uint16_t len) {

    if (len < CDJ_PACKET_TYPE_OFFSET || memcmp(CDJ_MAGIC_HEADER, data, CDJ_MAGIC_HEADER_LENGTH ) != 0 ) {
        return CDJ_ERROR;
    }

    return CDJ_OK;
}

/**
 * Takes a UDP packet and returns a pointer to newly malloced struct
 * that helps describe the packet, or NULL
 * @param UDP - data
 * @param packet_len - length of data
 * @param struct_len - size of real struct to create
 */
cdj_generic_packet_t*
cdj_new_generic_packet(unsigned char* packet, int packet_len, uint16_t struct_len)
{
    if (packet_len < CDJ_PACKET_TYPE_OFFSET + 2) return NULL;
    cdj_generic_packet_t* a_pkt = (cdj_generic_packet_t*) calloc(1, struct_len);
    if (a_pkt != NULL) {
        a_pkt->data = packet;
        a_pkt->len = packet_len;
        a_pkt->type = packet[CDJ_PACKET_TYPE_OFFSET];
    }
    return a_pkt;
}

cdj_discovery_packet_t*
cdj_new_discovery_packet(unsigned char* packet, uint16_t len)
{
    if (cdj_validate_header(packet, len) != CDJ_OK) {
        return NULL;
    }

    cdj_discovery_packet_t* d_pkt = (cdj_discovery_packet_t*) cdj_new_generic_packet(packet, len, sizeof(cdj_discovery_packet_t));
    if (d_pkt != NULL) {
        d_pkt->sub_type = cdj_discovery_sub_type(d_pkt);
        d_pkt->player_id = cdj_discovery_player_id(d_pkt);
        d_pkt->ip = cdj_discovery_ip(d_pkt);
        char* mac = cdj_discovery_mac(d_pkt);
        if (mac) {
            memcpy(d_pkt->mac, mac, 6);
            free(mac);
        }

    }

    return d_pkt;
}

cdj_beat_packet_t*
cdj_new_beat_packet(unsigned char* packet, uint16_t len)
{
    if (cdj_validate_header(packet, len) != CDJ_OK) {
        return NULL;
    }

    cdj_beat_packet_t* b_pkt = (cdj_beat_packet_t*) cdj_new_generic_packet(packet, len, sizeof(cdj_beat_packet_t));
    if (b_pkt != NULL) {
        b_pkt->player_id = cdj_beat_player_id(b_pkt);
        b_pkt->bpm = cdj_beat_calculated_bpm(b_pkt);
        b_pkt->bar_pos = cdj_beat_bar_pos(b_pkt);
    }

    return b_pkt;
}

cdj_cdj_status_packet_t*
cdj_new_cdj_status_packet(unsigned char* packet, uint16_t len) {

    if (cdj_validate_header(packet, len) != CDJ_OK) {
        return NULL;
    }

    cdj_cdj_status_packet_t* cs_pkt = (cdj_cdj_status_packet_t*) cdj_new_generic_packet(packet, len, sizeof(cdj_cdj_status_packet_t));
    if (cs_pkt != NULL) {
        cs_pkt->player_id = cdj_status_player_id(cs_pkt);
        cs_pkt->bpm = cdj_status_calculated_bpm(cs_pkt);
        cs_pkt->flags = cdj_status_flags(cs_pkt);
    }
    return cs_pkt;
}

cdj_mixer_status_packet_t*
cdj_new_mixer_status_packet(unsigned char* packet, uint16_t len) {

    if (cdj_validate_header(packet, len) != CDJ_OK) {
        return NULL;
    }

    cdj_mixer_status_packet_t* ms_pkt = (cdj_mixer_status_packet_t*) cdj_new_generic_packet(packet, len, sizeof(cdj_mixer_status_packet_t));

    return ms_pkt;
}

// Message intepretation functions

// Some of these methods return 0 rather than risk index out of bounds errors, but its the caller's responsibiltiy to make sure the
// data and message call is sane.  This is done so we can return unsigned char which is much more convenient tham -1 and casting.


//SNIP_bpm_madness


// these are deep symetries versions

// 0x100000 = 1048567
double
cdj_pitch_to_percentage_bpm(uint32_t pitch) {
    return (pitch - 1048567) / 10485.76;
}

// pitch to BPM multiplier
double
cdj_pitch_to_multiplier_bpm(uint32_t pitch) {
    return pitch / 104857600.0;  // pitch ÷ 0x100000 ÷ 100
}

float
cdj_calculated_bpm(uint16_t track_bpm, uint32_t pitch)
{
    return (float) (track_bpm * cdj_pitch_to_multiplier_bpm(pitch));
}


//pitch to multiplier
double
cdj_pitch_to_multiplier(uint32_t pitch)
{
    return (double) pitch / (double)0x100000;
}
double
cdj_pitch_to_percentage(uint32_t pitch) {
    return 100.0 * cdj_pitch_to_multiplier(pitch) - 100.0;
}
uint32_t
cdj_calculated_next(uint32_t next_beat, uint32_t pitch)
{
    return (uint32_t) ((double) next_beat * cdj_pitch_to_multiplier(pitch));
}

//SNIP_bpm_madness

static uint32_t
cdj_read_uint32(unsigned char* data, uint16_t pos)
{
    uint32_t i = 0;
    i |= data[pos++] << 24;
    i |= data[pos++] << 16;
    i |= data[pos++] << 8;
    i |= data[pos++] << 0;
    return i;
}

static uint16_t
cdj_read_uint16(unsigned char* data, uint16_t pos)
{
    unsigned int i = 0;
    i |= data[pos++] << 8;
    i |= data[pos++] << 0;
    return i;
}

static void
cdj_set_uint32(unsigned char* packet_pt, uint32_t i)
{
    *packet_pt++ = (unsigned char) (i  >> 24);
    *packet_pt++ = (unsigned char) (i  >> 16);
    *packet_pt++ = (unsigned char) (i  >> 8);
    *packet_pt++ = (unsigned char) (i  >> 0);
}

static void
cdj_set_uint16(unsigned char* packet_pt, uint16_t i)
{
    *packet_pt++ = (unsigned char) (i  >> 8);
    *packet_pt++ = (unsigned char) (i  >> 0);
}


// generic
unsigned char
cdj_packet_type(unsigned char* packet, uint16_t len)
{
    if (len < CDJ_PACKET_TYPE_OFFSET + 1) return 255;
    return packet[CDJ_PACKET_TYPE_OFFSET];
}


char*
cdj_model_name(unsigned char* packet, uint16_t len, uint16_t port)
{
    char* model = calloc(1, CDJ_DEVICE_MODEL_LENGTH + 1);
    if (len > 0x20) {
        snprintf(model, CDJ_DEVICE_MODEL_LENGTH, "%s", packet + cdj_header_len(port, cdj_packet_type(packet, len)));
    }
    return model;
}


// discovery

// discovery now has a sub type for mixer chanel assignments
unsigned char
cdj_discovery_sub_type(cdj_discovery_packet_t* d_pkt)
{
    if (d_pkt->len < CDJ_PACKET_TYPE_OFFSET + 2) return 255;
    return d_pkt->data[CDJ_PACKET_TYPE_OFFSET + 1];
}

unsigned char
cdj_discovery_player_id(cdj_discovery_packet_t* d_pkt)
{
    switch (d_pkt->type) {
        case CDJ_DISCOVERY:
        case CDJ_STAGE1_DISCOVERY: {
            // no player id aserted yet
            return 0;
        }
        case CDJ_ID_USE_REQ: {
            if (d_pkt->sub_type == 0) {
                if (d_pkt->len < 0x2f) return 0;
                return d_pkt->data[0x2e];
            }
            break;
        }
        case CDJ_ID_SET_REQ:
        case CDJ_ID_USE_RESP:
        case CDJ_KEEP_ALIVE: {
            if (d_pkt->sub_type == 0) {
                if (d_pkt->len < 0x25) return 0;
                return d_pkt->data[0x24];
            }
            break;
        }
    }
    if (d_pkt->len < 0x22) return 0;
    return d_pkt->data[0x21];
}
uint32_t
cdj_discovery_ip(cdj_discovery_packet_t* d_pkt)
{
    switch (cdj_packet_type(d_pkt->data, d_pkt->len)) {
        case CDJ_DISCOVERY:
        case CDJ_STAGE1_DISCOVERY: {
            break;
        }
        case CDJ_ID_USE_REQ: {
            if (d_pkt->sub_type == 0) {
                if (d_pkt->len < 0x27) return 0;
                return cdj_read_uint32(d_pkt->data, 0x24);
            }
            break;
        }
        case CDJ_ID_USE_RESP: {
            break;
        }
        case CDJ_COLLISION: {
            if (d_pkt->sub_type == 0) {
                if (d_pkt->len < 0x29) return 0;
                return cdj_read_uint32(d_pkt->data, 0x25);
            }
            break;
        }
        case CDJ_KEEP_ALIVE: {
            if (d_pkt->sub_type == 0) {
                if (d_pkt->len < 0x2f) return 0;
                return cdj_read_uint32(d_pkt->data, 0x2c);
            }
            break;
        }
    }
    return 0;
}

char*
cdj_discovery_mac(cdj_discovery_packet_t* d_pkt)
{
    char* mac = (char*) calloc(1, 6);
    if (mac == NULL) return NULL;

    switch (cdj_packet_type(d_pkt->data, d_pkt->len)) {
        case CDJ_KEEP_ALIVE:
        case CDJ_STAGE1_DISCOVERY: {
            if (d_pkt->sub_type == 0) {
                if (d_pkt->len < 0x2c) return 0;
                mac[0] = d_pkt->data[0x26];
                mac[1] = d_pkt->data[0x27];
                mac[2] = d_pkt->data[0x28];
                mac[3] = d_pkt->data[0x29];
                mac[4] = d_pkt->data[0x2a];
                mac[5] = d_pkt->data[0x2b];
            }
            break;
        }
        case CDJ_ID_USE_REQ: {
            if (d_pkt->sub_type == 0) {
                if (d_pkt->len < 0x2e) return 0;
                mac[0] = d_pkt->data[0x28];
                mac[1] = d_pkt->data[0x29];
                mac[2] = d_pkt->data[0x2a];
                mac[3] = d_pkt->data[0x2b];
                mac[4] = d_pkt->data[0x2c];
                mac[5] = d_pkt->data[0x2d];
            }
            break;
        }
    }
    return mac;
}

// cdj status, need to be careful here because some different playeer send different mounts of information

unsigned char
cdj_status_player_id(cdj_cdj_status_packet_t* cs_pkt)
{
    if (cs_pkt->len < 0x20) return 0;
    return cs_pkt->data[0x21];
}

// should be 1 or zero, 1 means busy, not necessarily playing audio, can be loading a track
unsigned char
cdj_status_active(cdj_cdj_status_packet_t* cs_pkt)
{
    if (cs_pkt->len < 0x26) return 0;
    return cs_pkt->data[0x27];
}
/**
 * player_id this CDJ is playing from. If zero no track loaded
 */
unsigned char
cdj_status_playing_from(cdj_cdj_status_packet_t* cs_pkt)
{
    if (cs_pkt->len < 0x27) return 0;
    return cs_pkt->data[0x28];
}
/**
 * 00=no track, 01=CD, 02=SD, 03=USB, 04=rekordbox
 */
unsigned char
cdj_status_playing_from_slot(cdj_cdj_status_packet_t* cs_pkt)
{
    if (cs_pkt->len < 0x28) return 0;
    return cs_pkt->data[0x29];
}
/**
 * 00=no track, 01=have beat grid, 02=no beat grid, 05=loaded from CD
 */
unsigned char
cdj_status_track_type(cdj_cdj_status_packet_t* cs_pkt)
{
    if (cs_pkt->len < 0x29) return 0;
    return cs_pkt->data[0x2a];
}
/**
 * recordbox track id (labeled `recordbox` in pdf), or CD track number, number used to request track meta data
 */
uint32_t
cdj_status_track_id(cdj_cdj_status_packet_t* cs_pkt)
{
    if (cs_pkt->len < 0x2e) return 0;
    return cdj_read_uint32(cs_pkt->data, 0x2c);
}
/**
 * Load a uint alto I think the devices are actuall limited to 4000 tracks per playlist.
 * Potentialy can be used to auto-play a whole playlist?
 */
uint32_t
cdj_status_track_number(cdj_cdj_status_packet_t* cs_pkt)
{
    if (cs_pkt->len < 0x32) return 0;
    return cdj_read_uint32(cs_pkt->data, 0x30);
}
/**
 * 00 no CD, 1e audio CD loaded, 11 MP3/data disk
 */
uint32_t
cdj_status_cd_data_type(cdj_cdj_status_packet_t* cs_pkt)
{
    if (cs_pkt->len < 0x39) return 0;
    return cdj_read_uint32(cs_pkt->data, 0x37);
}
/*
Value   Meaning
00      No track is loaded
02      A track is in the process of loading
03      Player is playing normally
04      Player is playing a loop
05      Player is paused anywhere other than the cue point
06      Player is paused at the cue point
07      Cue Play is in progress (playback while the cue button is held down)
08      Cue scratch is in progress
09      Player is searching forwards or backwards
0e      Audio CD has spun down due to lack of use
11      Player reached the end of the track and stopped
 */
unsigned char
cdj_status_play_mode(cdj_cdj_status_packet_t* cs_pkt)
{
    if (cs_pkt->len < 0x7a) return 0;
    return cs_pkt->data[0x7b];
}

uint32_t
cdj_status_sync_counter(cdj_cdj_status_packet_t* cs_pkt)
{
    if (cs_pkt->len < 0x84) return 0;
    return cdj_read_uint32(cs_pkt->data, 0x84);
}
/**
 * F1 - return a bit mask, of play state, e.g. flag & with CDJ_PLAY_STATE_*
 */
unsigned char
cdj_status_flags(cdj_cdj_status_packet_t* cs_pkt)
{
    if (cs_pkt->len < 0x88) return 0;
    return cs_pkt->data[0x89];
}
/**
 * P2 - play state indicator different per CDJ type
 */
uint32_t
cdj_status_sync_state(cdj_cdj_status_packet_t* cs_pkt)
{
    if (cs_pkt->len < 0x8d) return 0;
    return cdj_read_uint32(cs_pkt->data, 0x8b);
}
/**
 * @returns  CDJ_MASTER_STATE_* 00 not master, 01 tempo master, 02 master but no beat grid
 */
unsigned char
cdj_status_master_state(cdj_cdj_status_packet_t* cs_pkt)
{
    if (cs_pkt->len < 0x9d) return 0;
    return cs_pkt->data[0x9e];
}
/**
 * player id of a new master being negotiated
 */
unsigned char
cdj_status_new_master(cdj_cdj_status_packet_t* cs_pkt)
{
    if (cs_pkt->len < 0x9e) return 0;
    return cs_pkt->data[0x9f];
}

/**
 * returns 1 indexed position of the beat in the bar
 */
unsigned char
cdj_status_bar_pos(cdj_cdj_status_packet_t* cs_pkt)
{
    if (cs_pkt->len < 0xa5) return 0;
    return cs_pkt->data[0xa6];
}


uint16_t
cdj_status_bpm(cdj_cdj_status_packet_t* cs_pkt)
{
    if (cs_pkt->len < 0x92) return 0;
    unsigned int bpm = cdj_read_uint16(cs_pkt->data, 0x92);
    if (0xffff == bpm) return 0;
    return bpm;

}
uint32_t
cdj_status_pitch(cdj_cdj_status_packet_t* cs_pkt)
{
    if (cs_pkt->len < 0x8e) return 0;
    return cdj_read_uint32(cs_pkt->data, 0x8c);
}
float
cdj_status_calculated_bpm(cdj_cdj_status_packet_t* cs_pkt) {
    return cdj_calculated_bpm(cdj_status_bpm(cs_pkt), cdj_status_pitch(cs_pkt));
}
uint32_t
cdj_status_pitch_per_fader(cdj_cdj_status_packet_t* cs_pkt)
{
    if (cs_pkt->len < 0xc2) return 0;
    return cdj_read_uint32(cs_pkt->data, 0xc0);
}
uint32_t
cdj_status_counter(cdj_cdj_status_packet_t* cs_pkt)
{
    if (cs_pkt->len < 0xcb) return 0;
    return cdj_read_uint32(cs_pkt->data, 0xc8);
}


// beat
// this returns the BPM x 100
uint16_t
cdj_beat_bpm(cdj_beat_packet_t* b_pkt)
{
    if (b_pkt->len < 0x5a) return 0;
    return cdj_read_uint16(b_pkt->data, 0x5a);
}
// N.B. this gives you a 4 byte int, but careful, its not a typical C int.  See docs for details of hot to interpret this number
uint32_t
cdj_beat_pitch(cdj_beat_packet_t* b_pkt)
{
    if (b_pkt->len < 0x56) return 0;
    return cdj_read_uint32(b_pkt->data, 0x54);
}
// this method does the math to get actual BPM.
float
cdj_beat_calculated_bpm(cdj_beat_packet_t* b_pkt) 
{
    return cdj_calculated_bpm(cdj_beat_bpm(b_pkt), cdj_beat_pitch(b_pkt));
}
double
cdj_beat_calculated_pitch(cdj_beat_packet_t* b_pkt)
{
    if (b_pkt->len < 0x56) return 0;
    uint32_t pitch = cdj_beat_pitch(b_pkt);
    int32_t decimal = 
        (((unsigned char) pitch >> 16) * 10000) +  // I guess some bright spark at Pioneer
        (((unsigned char) pitch >> 8) *  100) +    // figured you can only fit 1 to 100 in a byte, doh!
        (((unsigned char) pitch >> 0) *  1);       // Id pay good money to see the comments in the code :)
    return (double)(decimal - 100000) / 100000.0;
}
//
uint32_t
cdj_beat_next(cdj_beat_packet_t* b_pkt)
{
    return cdj_read_uint32(b_pkt->data, 0x24);
}

// this is bpm taken from the miliseconds diff between the next beat and the 8th beat (the longest time diff provided in a beat packet)
// I'm not quite brainy enough to work out why this does not give us better resolution bpm, using bpm as a float is recommended
// N.B. this will only work if we are at the start of a constant bpm track.
double
cdj_beat_measured_bpm(cdj_beat_packet_t* b_pkt) 
{
    unsigned int b1, b2;
    b1 = cdj_read_uint32(b_pkt->data, 0x24);
    b2 = cdj_read_uint32(b_pkt->data, 0x38);
    double pitch = cdj_beat_pitch(b_pkt) / 100000.0;
    return pitch * (1.0 / ((double)(b2 - b1) / (6000000.0 * 7.0)));
}
unsigned char
cdj_beat_player_id(cdj_beat_packet_t* b_pkt)
{
    if (b_pkt->len < 0x20) return 0;
    return b_pkt->data[0x21];
}
unsigned char
cdj_beat_bar_pos(cdj_beat_packet_t* b_pkt)
{
    if (b_pkt->len < 0x5b) return 0;
    return b_pkt->data[0x5c];
}


// TODO effective cdj_status_bpm

// Message creation functions, based on CDJ_Protocol_Analysis.pdf
// hence lots of magic numbers we dont really know what mean.

/**
 * Set the 11 byte header
 */
static void
cdj_set_header(unsigned char* packet_pt, unsigned char type)
{
    memcpy(packet_pt, CDJ_MAGIC_NUMBER, 10);
    packet_pt[10] = type;
}

static void
cdj_set_model_name(unsigned char* packet_pt, unsigned char model)
{
    memcpy(packet_pt, "CDJ-1000\0\0\0\0\0\0\0\0\0\0\0\0", CDJ_DEVICE_MODEL_LENGTH);
    packet_pt[0] = model;
}

// CDJ_Protocol_Analysis.pdf Figure 6
unsigned char*
cdj_create_initial_discovery_packet(uint16_t* length, unsigned char model)
{
    *length = 0x25;
    unsigned char* packet = (unsigned char*) calloc(1, *length);
    if (packet) {
        cdj_set_header(packet, CDJ_DISCOVERY);
        cdj_set_model_name(packet + cdj_header_len(50000, CDJ_DISCOVERY), model);
        packet[0x20] = 0x01;
        packet[0x21] = 0x02;
        cdj_set_uint16(packet + 0x22, *length);
        packet[0x24] = 0x01; // TODO potentially should change for other devices
    }
    return packet;
}

// CDJ_Protocol_Analysis.pdf Figure 7
unsigned char*
cdj_create_stage1_discovery_packet(uint16_t* length, unsigned char model, unsigned char *mac, unsigned char n)
{
    *length = 0x2c;
    unsigned char* packet = (unsigned char*) calloc(1, *length);
    if (packet) {
        cdj_set_header(packet, CDJ_STAGE1_DISCOVERY);
        cdj_set_model_name(packet + cdj_header_len(50000, CDJ_DISCOVERY), model);
        packet[0x20] = 0x01;
        packet[0x21] = 0x02;
        cdj_set_uint16(packet + 0x22, *length);
        packet[0x24] = n;
        packet[0x25] = 0x01; // CDJ (mixer is 0x02)
        memcpy(packet + 0x26, mac, 6);
    }
    return packet;
}

unsigned char 
cdj_inc_stage1_discovery_packet(unsigned char* packet)
{
    return ++packet[0x24];
}


unsigned char*
cdj_create_id_use_req_packet(uint16_t* length, unsigned char model, unsigned char* ip, unsigned char* mac, unsigned char player_id, unsigned char n)
{
    *length = 0x32;
    unsigned char* packet = (unsigned char*) calloc(1, *length);
    if (packet) {
        cdj_set_header(packet, CDJ_ID_USE_REQ);
        cdj_set_model_name(packet + cdj_header_len(50000, CDJ_ID_USE_REQ), model);
        packet[0x20] = 0x01;
        packet[0x21] = 0x02;
        cdj_set_uint16(packet + 0x22, *length);
        memcpy(packet + 0x24, ip, 4);
        memcpy(packet + 0x28, mac, 6);
        packet[0x2e] = player_id;
        packet[0x2f] = n;
        packet[0x30] = 0x01; // CDJ (mixer is 0x02) (maybe)
        packet[0x31] = 0x01; // unknown
    }
    return packet;
}

unsigned char
cdj_inc_id_use_req_packet(unsigned char* packet)
{
    return ++packet[0x2f];
}

void
cdj_mod_id_use_req_packet_player_id(unsigned char* packet, unsigned char player_id)
{
    packet[0x2e] = player_id;
}


unsigned char*
cdj_create_id_use_resp_packet(uint16_t* length, unsigned char model, unsigned char player_id, unsigned char* ip)
{
    *length = 0x29;
    unsigned char* packet = (unsigned char*) calloc(1, *length);
    if (packet) {
        cdj_set_header(packet, CDJ_ID_USE_RESP);
        cdj_set_model_name(packet + cdj_header_len(50000, CDJ_DISCOVERY), model);
        packet[0x20] = 0x01;
        packet[0x21] = 0x02;
        cdj_set_uint16(packet + 0x22, *length);
        packet[0x24] = player_id;
        memcpy(packet + 0x25, ip, 4);
    }
    return packet;
}


unsigned char*
cdj_create_id_set_req_packet(uint16_t* length, unsigned char model, unsigned char player_id, unsigned char n)
{
    *length = 0x26;
    unsigned char* packet = (unsigned char*) calloc(1, *length);
    if (packet) {
        cdj_set_header(packet, CDJ_ID_SET_REQ);
        cdj_set_model_name(packet + cdj_header_len(50000, CDJ_DISCOVERY), model);
        packet[0x20] = 0x01;
        packet[0x21] = 0x02;
        cdj_set_uint16(packet + 0x22, *length);
        packet[0x24] = player_id;
        packet[0x25] = n;
    }
    return packet;
}

unsigned char
cdj_inc_id_set_req_packet(unsigned char* packet)
{
    return ++packet[0x25];
}

unsigned char*
cdj_create_keepalive_packet(uint16_t* length, unsigned char model, unsigned char* ip, unsigned char* mac, unsigned char player_id, unsigned char member_count)
{
    *length = 0x36;
    unsigned char* packet = (unsigned char*) calloc(1, *length);
    if (packet) {
        cdj_set_header(packet, CDJ_KEEP_ALIVE);
        cdj_set_model_name(packet + cdj_header_len(50000, CDJ_DISCOVERY), model);
        packet[0x20] = 0x01;
        packet[0x21] = 0x02;
        cdj_set_uint16(packet + 0x22, *length);
        packet[0x24] = player_id;
        packet[0x25] = 0x02;
        memcpy(packet + 0x26, mac, 6);
        memcpy(packet + 0x2c, ip, 4);
        packet[0x30] = member_count;
        packet[0x31] = 0x00;
        packet[0x32] = 0x00;
        packet[0x33] = 0x00;
        packet[0x34] = 0x01; // CDJ (mixer is 0x02)
        packet[0x35] = 0x00;
    }
    return packet;
}

unsigned char*
cdj_create_id_collision_packet(uint16_t* length, unsigned char model, unsigned char player_id, unsigned char* ip)
{
    *length = 0x29;
    unsigned char* packet = (unsigned char*) calloc(1, *length);
    if (packet) {
        cdj_set_header(packet, CDJ_COLLISION);
        cdj_set_model_name(packet + cdj_header_len(50000, CDJ_DISCOVERY), model);
        packet[0x20] = 0x01;
        packet[0x21] = 0x02;
        cdj_set_uint16(packet + 0x22, *length);
        packet[0x24] = player_id;
        memcpy(packet + 0x25, ip, 4);
    }
    return packet;
}


static uint32_t
cdj_beat_millis(float bpm)
{
    return (uint32_t) (60000.0 / bpm);
}

/**
 * Figure 9, beat packets.
 * this code presumes constant bpm and/or that CDJs can handle the fact that between beat messages BPM may change.
 * A lot of redundant info for house music, I dont see when the client armed with beat time and BPM cant do this calculation themselves, or why the need it?
 *
 * N.B. Pioneer send the tracks base BPM and then the pitch adjust, instead of sending the actual BPM (TODO presumably tempo for master tempo mde)
 * But the BPM is sent every message so one wonders why?  Maybe to enable key matching?
 *
 * @param length - returns buffer length
 * @param model - currently sane values are just  'C' or 'X'
 * @param player_id - player number / device number / D
 * @param bpm - in sane format , e.g. 120.00
 * @param bar_index - 0 - 3, where in the bar we are, protocol presumes 4/4 timing
 */ 
unsigned char*
cdj_create_beat_packet(uint16_t* length, unsigned char model, unsigned char player_id, float bpm, unsigned char bar_index)
{
    *length = 0x60;
    int beat_offset;
    uint32_t beat_pos;
    uint32_t bar_pos;
    uint32_t millis_in_a_beat = cdj_beat_millis(bpm);

    unsigned char* packet = (unsigned char*) calloc(1, *length);
    if (packet) {
        cdj_set_header(packet, CDJ_BEAT);
        cdj_set_model_name(packet + cdj_header_len(50001, CDJ_BEAT), model);
        packet[0x1f] = 0x01;       // wtf
        packet[0x20] = 0x00;
        packet[0x21] = player_id;  // device number or player number?
        cdj_set_uint16(packet + 0x22, *length);

        beat_offset = 0x24;
        
        // nextBeat
        beat_pos = millis_in_a_beat;
        cdj_set_uint32(packet + beat_offset, beat_pos); beat_offset += 4;
        // 2ndBeat
        beat_pos += millis_in_a_beat;
        cdj_set_uint32(packet + beat_offset, beat_pos); beat_offset += 4;
        // nextBar
        bar_pos = (4 - bar_index) * millis_in_a_beat;
        cdj_set_uint32(packet + beat_offset, bar_pos); beat_offset += 4;
        // 4thBeat
        beat_pos += 2 * millis_in_a_beat;
        cdj_set_uint32(packet + beat_offset, beat_pos); beat_offset += 4;
        // 2ndBar
        bar_pos = (8 - bar_index) * millis_in_a_beat;
        cdj_set_uint32(packet + beat_offset, bar_pos); beat_offset += 4;
        //8thBeat
        beat_pos += 4 * millis_in_a_beat;
        cdj_set_uint32(packet + beat_offset, beat_pos); beat_offset += 4;

        memset(packet + 0x3c, 0xff, 24);

        // TODO pitch, midi does not pitch adjust
        cdj_set_uint32(packet + 0x54, 0x00100000);

        // bpm (protocol hardcodes to 2 decimal places, but seems to reserve space)
        uint16_t bpm_i = (uint16_t) (bpm * 100);
        cdj_set_uint32(packet + 0x58, bpm_i);

        // b, bar index
        packet[0x5c] = bar_index + 1;

        packet[0x5f] = player_id;
    }
    return packet;
}


unsigned char*
cdj_create_status_packet(uint16_t* length, unsigned char model, unsigned char player_id,
    float bpm, unsigned char bar_index, unsigned char active, unsigned char master, uint32_t sync_counter,
    uint32_t n)
{
    *length = 0xd4;
    unsigned char* packet = (unsigned char*) calloc(1, *length);
    if (packet) {
        cdj_set_header(packet, CDJ_STATUS);
        cdj_set_model_name(packet + cdj_header_len(CDJ_UPDATE_PORT, CDJ_STATUS), model);
        packet[0x1f] = 0x01;
        packet[0x20] = 0x03;
        packet[0x21] = player_id;
        cdj_set_uint16(packet + 0x22, *length);
        packet[0x24] = player_id;
        packet[0x25] = 0x00;
        packet[0x26] = 0x00;       // unknown

        packet[0x27] = active;     // A  active
        packet[0x28] = player_id;  // Dr trackloaded from myself
        packet[0x29] = 0x03;       // Sr track loaded from USB
        packet[0x2a] = 0x01;       // Tr track supports beat grid (we thus have to broadcast beat frames)


        packet[0x3a] = 0xa0;

        packet[0x37] = 0x05;
        packet[0x47] = 0x08;

        // track id
        cdj_set_uint32(packet + 0x2c, 0x01); // static 1 for the moment since we don yet support media queries
        cdj_set_uint32(packet + 0x30, 0x01); // track number


        packet[0x68] = 0x01; // wtf
        packet[0x73] = 0x04; // SD nomedia
        packet[0x75] = 0x01; // link available (we dont have link yet but without this flag XDJ will not sync)
        packet[0x78] = 0x01; // wtf
        // play mode
        packet[0x7b] = active ? 0x04 : 0x05;       // playing in a loop | paused

        // firmware version  31 2e 30 35  "1.05" XDJ does not seem to care
        packet[0x7c] = 0x31;  // 1
        packet[0x7d] = 0x2e;  // .
        packet[0x7e] = 0x30;  // 0
        packet[0x7f] = 0x35;  // 5

        cdj_set_uint32(packet + 0x84, sync_counter);

        packet[0x89] = CDJ_STAT_FLAG_BASE; // TODO nexus flags sync & onair
        if (active) {
            packet[0x89] |= CDJ_STAT_FLAG_PLAY;
        }
        if (master) {
            packet[0x89] |= CDJ_STAT_FLAG_MASTER;
        }
        packet[0x8a] = 0xff;

        // pitch
        cdj_set_uint32(packet + 0x8c, CDJ_PITCH_NORMAL);
        cdj_set_uint32(packet + 0x98, CDJ_PITCH_NORMAL);
        cdj_set_uint32(packet + 0xc0, CDJ_PITCH_NORMAL);
        cdj_set_uint32(packet + 0xc4, CDJ_PITCH_NORMAL);

        // bpm
        // Mv 
        cdj_set_uint16(packet + 0x90, 0x8000); // 8000 is track loaded, you can sync from me, Mv for master handoffs  backline->master_new
        // bpm x 100  as an int
        cdj_set_uint16(packet + 0x92, (int) (bpm * 100.0));

        packet[0x9e] = master ? 0x01 : 0x00;  // Mm Now I am the master
        packet[0x9f] = 0xff;  // Mh master handoff, new_master goes here if we get sent a master_req

        cdj_set_uint32(packet + 0xc4, CDJ_PITCH_NORMAL);
        // magic / unknown
        cdj_set_uint32(packet + 0x94, 0x7fffffff);

        packet[0xa6] = 1 + bar_index;

        cdj_set_uint32(packet + 0xc8, n);
        packet[0xcc] = 0x0f;  // I am nexus

    }
    return packet;
}


unsigned char*
cdj_create_master_request_packet(uint16_t* length, unsigned char model, unsigned char player_id)
{
    *length = 0x28;
    unsigned char* packet = (unsigned char*) calloc(1, *length);
    if (packet) {
        cdj_set_header(packet, CDJ_MASTER_REQ);
        cdj_set_model_name(packet + cdj_header_len(50001, 0), model);
        packet[0x1f] = 0x01;
        packet[0x21] = player_id;
        packet[0x23] = 0x04;
        packet[0x27] = player_id;
    }
    return packet;
}

/**
 * Accept a take over request, DM to requester
 */
unsigned char*
cdj_create_master_response_packet(uint16_t* length, unsigned char model, unsigned char player_id)
{
    *length = 0x2c;
    unsigned char* packet = (unsigned char*) calloc(1, *length);
    if (packet) {
        cdj_set_header(packet, CDJ_MASTER_REQ);
        cdj_set_model_name(packet + cdj_header_len(50001, 0), model);
        packet[0x1f] = 0x01;
        packet[0x21] = player_id;
        packet[0x23] = 0x08;
        packet[0x27] = player_id;
        packet[0x2b] = 0x01;
    }
    return packet;
}

unsigned char*
cdj_create_sync_control_packet(uint16_t* length, unsigned char model, unsigned char player_id, int on_off)
{
    *length = 0x2c;
    unsigned char* packet = (unsigned char*) calloc(1, *length);
    if (packet) {
        cdj_set_header(packet, CDJ_SYNC_CONTROL);
        cdj_set_model_name(packet + cdj_header_len(50001, 0), model);
        packet[0x1f] = 0x01;
        packet[0x21] = player_id;
        packet[0x23] = 0x08;
        packet[0x27] = player_id;
        packet[0x2b] = on_off ? 0x10 : 0x20;
    }
    return packet;
}

// DEBUG

void
cdj_print_packet(unsigned char* packet, uint16_t length, uint16_t port)
{
    int i;
    char* str = (char*) calloc(1, (length * 4) + 5 + length / 32);
    if (str == NULL) return;

    unsigned char type = packet[CDJ_PACKET_TYPE_OFFSET];
    uint16_t hdr_len = cdj_header_len(port, type);

    char* s = str;

    for (i = 0; i < length; i++) {
        //if (i == hdr_len ) *s++ = '\n';
        if (i > 0 && (i % 16 == 0)) *s++ = '\n';
        if (i < 10 || (i >= hdr_len && i < hdr_len + CDJ_DEVICE_MODEL_LENGTH) ) {
            s += snprintf(s, 4, "%c_ ", packet[i] ? packet[i] : '_');
        }
        else s += snprintf(s, 4, "%02x ", packet[i]);
    }

    printf("packet::%03x %i\n'%s' type=%02x:%s\n%s\n", length, length, packet + hdr_len, packet[10], cdj_type_to_string(port, packet[10], packet[11]), str);
    free(str);
}

