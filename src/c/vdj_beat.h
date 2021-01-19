
#include "cdj.h"
#include "vdj.h"


int vdj_init_beat_thread(vdj_t* v);
// kill
void vdj_stop_beat_thread(vdj_t* v);

// unpause
void vdj_start_beat_thread(vdj_t* v);
// thread stays alive but we become v->inactive
void vdj_pause_beat_thread(vdj_t* v);
