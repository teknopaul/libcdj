/*
 * vdj-nfs: Virtual CDJ + NFS server.
 *
 * Joins ProLink as a mock XDJ and simultaneously serves an NFSv2 export
 * at /C/ containing a fake export.pdb, so real CDJs can browse the track.
 *
 * Usage: vdj-nfs [-i iface] [-p player_id] [-x] [-c] [-b bpm]
 *                [--mount-port PORT] [--nfs-port PORT]
 *
 * Port defaults: mount=7004, nfs=7005 (use 48276/2049 as root for real XDJs)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <pthread.h>
#include <arpa/inet.h>

#include "cdj.h"
#include "vdj.h"
#include "vdj_net.h"
#include "vdj_beatout.h"
#include "vdj_discovery.h"
#include "vdj_nfs.h"

static volatile int g_running = 1;

static void on_signal(int sig) { (void)sig; g_running = 0; }

static void *nfs_thread(void *arg) {
    vdj_t *v = (vdj_t *)arg;
    while (g_running)
        vdj_nfs_poll(v, 100);
    return NULL;
}

static void vdj_nfs_usage(void) {
    printf("options:\n");
    printf("    -i IFACE         network interface\n");
    printf("    -p PLAYER_ID     player id (1-6)\n");
    printf("    -a               auto-assign player number\n");
    printf("    -x               mimic XDJ-1000\n");
    printf("    -c               mimic CDJ-1000\n");
    printf("    -b BPM           broadcast beat at BPM\n");
    printf("    --mount-port N   mountd UDP port (default 7004; use 48276 as root)\n");
    printf("    --nfs-port N     NFS UDP port (default 7005; use 2049 as root)\n");
    printf("    -h               display this text\n");
    exit(0);
}

static void on_discovery(vdj_t *v, cdj_discovery_packet_t *d_pkt) {
    if (d_pkt->player_id && v->backline->link_members[d_pkt->player_id]) {
        char ip_s[INET_ADDRSTRLEN];
        struct sockaddr_in *ip_addr = v->backline->link_members[d_pkt->player_id]->ip_addr;
        if (ip_addr) {
            inet_ntop(AF_INET, &ip_addr->sin_addr.s_addr, ip_s, INET_ADDRSTRLEN);
            printf("link member: %02i [%s] %s\n",
                   d_pkt->player_id, cdj_discovery_model(d_pkt), ip_s);
        }
    }
}

int main(int argc, char *argv[]) {
    unsigned int flags      = 0;
    uint8_t      player_id  = 0;
    char        *iface      = NULL;
    float        bpm        = 0.0f;
    uint16_t     mount_port = 7004;
    uint16_t     nfs_port   = 7005;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0) {
            vdj_nfs_usage();
        } else if (strcmp(argv[i], "-i") == 0 && i+1 < argc) {
            iface = argv[++i];
        } else if (strcmp(argv[i], "-p") == 0 && i+1 < argc) {
            player_id = (uint8_t)atoi(argv[++i]);
            if (player_id < 0xf) flags |= player_id;
        } else if (strcmp(argv[i], "-a") == 0) {
            flags |= VDJ_FLAG_AUTO_ID;
        } else if (strcmp(argv[i], "-x") == 0) {
            flags |= VDJ_FLAG_DEV_XDJ;
        } else if (strcmp(argv[i], "-c") == 0) {
            flags |= VDJ_FLAG_DEV_CDJ;
        } else if (strcmp(argv[i], "-b") == 0 && i+1 < argc) {
            bpm = strtof(argv[++i], NULL);
        } else if (strcmp(argv[i], "--mount-port") == 0 && i+1 < argc) {
            mount_port = (uint16_t)atoi(argv[++i]);
        } else if (strcmp(argv[i], "--nfs-port") == 0 && i+1 < argc) {
            nfs_port = (uint16_t)atoi(argv[++i]);
        }
    }

    if (player_id == 0) flags |= VDJ_FLAG_AUTO_ID;

    vdj_t *v = vdj_init_iface(iface, flags);
    if (!v) { fprintf(stderr, "error: creating virtual cdj\n"); return 1; }
    if (bpm > 0.0f) v->bpm = bpm;

    if (vdj_open_sockets(v) != CDJ_OK) {
        fprintf(stderr, "error: failed to open sockets\n");
        vdj_destroy(v); return 1;
    }

    if (vdj_exec_discovery(v) != CDJ_OK) {
        fprintf(stderr, "error: cdj initialization\n");
        vdj_destroy(v); return 1;
    }

    if (vdj_nfs_init(v, mount_port, nfs_port) < 0) {
        fprintf(stderr, "error: nfs init failed\n");
        vdj_destroy(v); return 1;
    }

    sleep(1);

    if (vdj_init_managed_discovery_thread(v, on_discovery) != CDJ_OK) {
        fprintf(stderr, "error: init discovery thread\n");
        vdj_nfs_destroy(v); vdj_destroy(v); return 1;
    }

    sleep(1);
    if (vdj_init_managed_update_thread(v, NULL) != CDJ_OK) {
        fprintf(stderr, "error: init update thread\n");
        vdj_nfs_destroy(v); vdj_destroy(v); return 1;
    }

    if (vdj_init_status_thread(v) != CDJ_OK) {
        fprintf(stderr, "error: init status thread\n");
        vdj_nfs_destroy(v); vdj_destroy(v); return 1;
    }

    if (bpm > 0.0f) {
        if (vdj_init_beatout_thread(v) != CDJ_OK) {
            fprintf(stderr, "error: init beatout thread\n");
        } else {
            vdj_start_beatout_thread(v);
        }
    }

    pthread_t nfs_tid;
    if (pthread_create(&nfs_tid, NULL, nfs_thread, v) != 0) {
        perror("pthread_create");
        vdj_nfs_destroy(v); vdj_destroy(v); return 1;
    }

    signal(SIGINT,  on_signal);
    signal(SIGTERM, on_signal);

    printf("vdj-nfs running. Ctrl-C to stop.\n");
    while (g_running) sleep(1);

    pthread_join(nfs_tid, NULL);
    vdj_nfs_destroy(v);
    vdj_destroy(v);
    return 0;
}
