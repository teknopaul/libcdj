


#include "cdj.h"
#include "vdj.h"

/* 

To switch master the following is needed

- If there is no master I just become it
- Find out who is the current master
- send a cdj_create_master_request_packet() to port 50001 of the current master
- expect a response on 50001
- when current master's status packet Mh says your own player_id you can assert this in your own status packets

TODO manage v->backline->sync_counter this increments with every handoff

*/

void
vdj_request_master(vdj_t* v)
{
    int length;
    vdj_link_member_t* m;
    struct sockaddr_in* dest;

    unsigned char master_id = v->backline->master_id;

    /* TODO only do ths when we have backline info coorect
    if(! master_id) {
        v->backline->master_id = v->player_id;
        v->backline->master_bpm = v->bpm;
        v->master = 1;
        return
    }
    */

    if (master_id) {
        unsigned char* packet = cdj_create_master_request_packet(&length, v->model, v->player_id);

        m = vdj_get_link_member(v, master_id);
        if (m) {
           dest = vdj_alloc_dest_addr(m, CDJ_UPDATE_PORT);
           vdj_sendto_update(v, dest, packet, length);
           free(dest);
        }
    }

    // lets see if we can ignore the response on 50001
    // if other players tell us we are the master managed_update can handle it.

}


void
vdj_update_new_master(vdj_t* v, unsigned char new_master_id)
{
        int i;
    vdj_link_member_t* m;
    if (new_master_id) {
        if (v->player_id == new_master_id) { // thats me!
            v->master = 1;
        }
        for (i = 0; i < VDJ_MAX_BACKLINE; i++) {
            if ( (m = v->backline->link_members[i]) ) {
                m->master_state = m->player_id = new_master_id;
            }
        }
    }
}