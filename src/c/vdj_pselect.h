

#ifndef _VDJ_PSELECT_H_INCLUDED_
#define _VDJ_PSELECT_H_INCLUDED_

#include "vdj.h"

int vdj_pselect_init(vdj_t* v, 
    vdj_discovery_ph          discovery_ph,
    vdj_discovery_unicast_ph  discovery_unicast_ph,
    vdj_beat_ph               beat_ph,
    vdj_beat_unicast_ph       beat_unicast_ph,
    vdj_update_ph             update_ph
    );

void vdj_pselect_stop(vdj_t* v);

#endif // _VDJ_PSELECT_H_INCLUDED_