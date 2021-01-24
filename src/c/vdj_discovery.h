
#ifndef _VDJ_DISCOVERY_H_INCLUDED_
#define _VDJ_DISCOVERY_H_INCLUDED_

int vdj_exec_discovery(vdj_t* v);

int vdj_init_keepalive_thread(vdj_t* v);
void vdj_stop_keepalive_thread();


int vdj_init_managed_discovery_thread(vdj_t* v, vdj_discovery_handler discovery_handler);
void vdj_stop_managed_discovery_thread(vdj_t* v);

#endif // _VDJ_DISCOVERY_H_INCLUDED_