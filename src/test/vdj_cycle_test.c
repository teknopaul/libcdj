/*
 * Connect / disconnect cycles with every worker thread running (the order vdj-ui tears down in).
 * Built with AddressSanitizer by `make test`; before the threads were joined this printed
 * "Bad file descriptor" for every socket and ended in a heap-use-after-free in the status thread.
 *
 * usage: vdj_cycle_test [interface] [cycles]     (default: lo, 3)
 */
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include "../c/vdj.h"
#include "../c/vdj_discovery.h"

static void discovery_ph(vdj_t* v, cdj_discovery_packet_t* d) {}
static void update_ph(vdj_t* v, cdj_cdj_status_packet_t* c) {}
static void beat_ph(vdj_t* v, cdj_beat_packet_t* b) {}

int main(int argc, char** argv)
{
    const char* ifname = argc > 1 ? argv[1] : "lo";
    int cycles = argc > 2 ? atoi(argv[2]) : 3;

    for (int i = 0; i < cycles; i++) {
        struct timespec a, b;
        clock_gettime(CLOCK_MONOTONIC, &a);

        vdj_t* v = vdj_init_iface((char*) ifname, VDJ_FLAG_AUTO_ID | VDJ_FLAG_DEV_XDJ);
        if (!v) { fprintf(stderr, "[error] vdj_init_iface(%s) failed\n", ifname); return 2; }
        if (vdj_open_sockets(v) != VDJ_OK) { fprintf(stderr, "[error] open sockets failed in cycle %d\n", i); return 3; }
        if (vdj_init_managed_discovery_thread(v, discovery_ph) != CDJ_OK ||
            vdj_init_managed_update_thread(v, update_ph) != CDJ_OK ||
            vdj_init_managed_beat_thread(v, beat_ph) != CDJ_OK ||
            vdj_init_managed_beat_unicast_thread(v, NULL) != CDJ_OK ||
            vdj_init_status_thread(v) != CDJ_OK) {
            fprintf(stderr, "[error] a thread did not start in cycle %d\n", i);
            return 4;
        }
        usleep(300000);

        vdj_stop_managed_discovery_thread(v);
        vdj_stop_managed_beat_unicast_thread(v);
        vdj_stop_threads(v);
        vdj_close_sockets(v);
        if (vdj_destroy(v) != 0) { fprintf(stderr, "[error] vdj_destroy reported an error in cycle %d\n", i); return 5; }

        clock_gettime(CLOCK_MONOTONIC, &b);
        long ms = (b.tv_sec - a.tv_sec) * 1000 + (b.tv_nsec - a.tv_nsec) / 1000000;
        if (ms > 3000) { fprintf(stderr, "[error] cycle %d took %ld ms\n", i, ms); return 6; }
    }
    printf("[pass] vdj_cycle_test (%d cycles)\n", cycles);
    return 0;
}
