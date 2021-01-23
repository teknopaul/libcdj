

// higher level callback API



typedef struct  {
    vdj_t* v;
    // raw packet handlers
    vdj_update_handler          update_pkt;
    vdj_broadcast_handler       broadcast_pkt;
    vdj_discovery_handler       ;
} vdj_ext_client;


// formatted structs from cdj.h

typedef void (*vdj_status_handler)(vdj_t* v, cdj_cdj_status_packet_t* cs_pkt);
typedef void (*vdj_beat_handler)(vdj_t* v, cdj_beat_packet_t* b_pkt);
typedef void (*vdj_discovery_handler)(vdj_t* v, cdj_discovery_packet_t* d_pkt);


typedef void (*vdj_new_member_handler)(vdj_t* v, vdj_link_member_t* bm);
typedef void (*vdj_member_gone_handler)(vdj_t* v, unsigned char player_id);
typedef void (*vdj_status_change_handler)(vdj_t* v, unsigned char* status);
typedef void (*vdj_io_error_handler)(vdj_t* v, unsigned char* packet, int packet_length);
