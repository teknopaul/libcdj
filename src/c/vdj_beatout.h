
#include "cdj.h"
#include "vdj.h"


int vdj_init_beatout_thread(vdj_t* v);
// kill
void vdj_stop_beatout_thread(vdj_t* v);

// unpause
void vdj_start_beatout_thread(vdj_t* v);
// thread stays alive but we become v->inactive
void vdj_pause_beatout_thread(vdj_t* v);
