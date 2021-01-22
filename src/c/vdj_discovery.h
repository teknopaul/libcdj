
#ifndef _VDJ_DISCOVERY_H_INCLUDED_
#define _VDJ_DISCOVERY_H_INCLUDED_


int vdj_exec_discovery(vdj_t* v);

int vdj_init_keepalive_thread(vdj_t* v, vdj_discovery_handler discovery_handler);
void vdj_stop_keepalive_thread();

#endif // _VDJ_DISCOVERY_H_INCLUDED_