#include <unistd.h>
#include <pthread.h>
#include <stdatomic.h>

#include "cdj.h"
#include "vdj.h"

/**
 * Code to send out Beats according to the VDJ's current bpm.
 * Use for testing untill integrated with midi or a different time source.
 * THis code just ticks at a constant BPM..
 */

static unsigned _Atomic vdj_beat_running = ATOMIC_VAR_INIT(0);
static unsigned _Atomic vdj_beat_paused = ATOMIC_VAR_INIT(1);

static long int 
vdj_one_beat_nanos(float bpm)
{
    return (long int) (60000000000.0 / bpm);
}

static struct timespec
vdj_one_beat_time(float bpm)
{
    long int nanos = vdj_one_beat_nanos(bpm);
    struct timespec ts = {0};
    ts.tv_sec = nanos / 1000000000L;
    ts.tv_nsec = nanos % 1000000000L;
    return ts;
}

static void*
vdj_beat_loop(void* arg)
{
    vdj_t* v = arg;

    struct timespec sl;
    vdj_beat_running = 1;
    int was_paused = 0;
    while (vdj_beat_running) {

        if (vdj_beat_paused) {
            was_paused = 1;
            usleep(50000); // todo wake up immediatly
        }
        if (was_paused) {
            was_paused = 0;
            v->bar_pos = 0;
        }

        vdj_broadcast_beat(v, v->bar_pos++);
        if (v->bar_pos == 4) v->bar_pos = 0;

        sl = vdj_one_beat_time(v->bpm);
        nanosleep(&sl, NULL);

    }
    return NULL;
}

int
vdj_init_beat_thread(vdj_t* v)
{
    pthread_t thread_id;
    int s = pthread_create(&thread_id, NULL, vdj_beat_loop, v);
    if (s != 0) {
        return CDJ_ERROR;
    }
    return CDJ_OK;
}


// kill
void
vdj_stop_beat_thread(vdj_t* v)
{
    v->active = 0;
    vdj_beat_running = 0;
}


void
vdj_start_beat_thread(vdj_t* v)
{
    v->active = 1;
    vdj_beat_paused = 0;
}

void
vdj_pause_beat_thread(vdj_t* v)
{
    v->active = 0;
    vdj_beat_paused = 1;
}