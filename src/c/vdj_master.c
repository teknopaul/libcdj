
#include <stdlib.h>
#include <stdio.h>

#include "cdj.h"
#include "vdj.h"

/* 

To switch to master the following is needed...

- need to be sending beats (you can be master without beats but is useless)
- If there is no master I just become it
- Find out who is the current master (will be in v->backline from status messages)
- send a cdj_create_master_request_packet() to port 50001 of the current master
- expect a response on 50001
- when current master's status packet Mh says your own player_id you can assert this in your own status packets
  - I'll bet you can do that after the DM response too.
  
- manage v->backline->sync_counter this increments with every handoff

To give up being master the following is needed...

- receive a master request
  - update v->master_req and send that in status flags
  - reply with a dm to unicast:50001
- when we see the new master claiming to be master
  - stop saying we are
  - v->master_req becomes -1 again

*/

void
vdj_request_master(vdj_t* v)
{
    uint16_t length;
    vdj_link_member_t* m;
    uint8_t* pkt;
    struct sockaddr_in* dest;

    uint8_t master_id = v->backline->master_id;


    // no existing master, only happens on reboot of _all_ players
    // TODO race should verify we have received at least one status packet once
    if ( ! master_id ) {
        master_id = v->player_id;
        v->backline->master_id = v->player_id;
        v->backline->master_bpm = v->bpm;
        v->master = 1;
        v->master_state |= CDJ_STAT_FLAG_MASTER;
        return;
    }

    if (master_id != v->player_id) {
        if ( (pkt = cdj_create_master_request_packet(&length, v->model, v->player_id)) ) {
            if ( (m = vdj_get_link_member(v, master_id)) ) {
                if ( (dest = vdj_alloc_dest_addr(m, CDJ_BEAT_PORT)) ) {
                    vdj_sendto_update(v, dest, pkt, length);
                    free(dest);
                    fprintf(stderr, "\nsent update to %i \n\n", master_id);
                    cdj_fprint_packet(stderr, pkt, length, 50001);
                }
            }
            free(pkt);
        } 
    }
}


void
vdj_update_new_master(vdj_t* v, int8_t new_master_id)
{
    vdj_link_member_t* m;
    int i;
    
    if (new_master_id > 0) {
        if (v->player_id == new_master_id) { // thats me!
            //fprintf(stderr, "master handoff confirmed\n");
            v->master = 1;
            v->master_req = -1;
            v->backline->sync_counter++;
        }
        else {
            //fprintf(stderr, "i'm not master\n");
            v->master = 0;
        }
        for (i = 0; i < VDJ_MAX_BACKLINE; i++) {
            if ( (m = v->backline->link_members[i]) ) {
                m->master_state = m->player_id == new_master_id ? CDJ_MASTER_STATE_ON : CDJ_MASTER_STATE_OFF;
            }
        }
    }
}