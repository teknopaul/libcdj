
#ifndef _VDJ_DISCOVERY_H_INCLUDED_
#define _VDJ_DISCOVERY_H_INCLUDED_

int vdj_exec_discovery(vdj_t* v);

int vdj_init_keepalive_thread(vdj_t* v);
void vdj_stop_keepalive_thread();


int vdj_init_managed_discovery_thread(vdj_t* v, vdj_discovery_ph discovery_ph);
void vdj_stop_managed_discovery_thread(vdj_t* v);

// exposed for use by vdj_pselect
void vdj_expire_players(vdj_t* v);
void vdj_handle_managed_discovery_datagram(vdj_t* v, vdj_discovery_ph discovery_ph, uint8_t* packet, ssize_t len);
void vdj_handle_managed_discovery_unicast_datagram(vdj_t* v, vdj_discovery_unicast_ph discovery_unicast_ph, uint8_t* packet, ssize_t len);

#endif // _VDJ_DISCOVERY_H_INCLUDED_