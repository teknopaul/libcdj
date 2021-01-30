
#ifndef _LIBCDJ_SIMPLE_H_INCLUDED_
#define _LIBCDJ_SIMPLE_H_INCLUDED_

// TODO higher level callback API

typedef struct vdj_ext_client_s        vdj_ext_client;

// formatted structs from cdj.h

typedef void (*vdj_status_cb)(vdj_ext_client* v, cdj_cdj_status_packet_t* cs_pkt);
typedef void (*vdj_beat_cb)(vdj_ext_client* v, cdj_beat_packet_t* b_pkt);

// formatted structs from vdj.h

typedef void (*vdj_new_member_cb)(vdj_ext_client* v, vdj_link_member_t* m);
typedef void (*vdj_member_gone_cb)(vdj_ext_client* v, uint8_t player_id);
typedef void (*vdj_io_error_cb)(vdj_ext_client* v);

struct vdj_ext_client_s {
    vdj_t* v;
    vdj_new_member_cb      new_member;
    vdj_member_gone_cb     member_gone;
    vdj_status_cb          status;
    vdj_beat_cb            beat;
    vdj_io_error_cb        error;
};


int vdj_init_simple(uint32_t flags, vdj_ext_client* client);

#endif // _LIBCDJ_SIMPLE_H_INCLUDED_
