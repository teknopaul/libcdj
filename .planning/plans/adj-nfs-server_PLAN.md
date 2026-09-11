# adj NFS Server — Wiring Plan

## Background & Context

The libcdj NFS server (`src/nfs/nfs_server.c`) and a complete working ops implementation
(`src/bin/nfs_server_test.c`) already exist. Pioneer XDJs query a portmapper on UDP 111
before opening connections on port 12523 (RemoteDB). The system `rpcbind` is already
running on the development machine, so we can register with it (no root needed).

From the live TCP capture session we know:
- Port 12523 RemoteDB handshake fires **before** port 1051 ProLink TCP
- The NFS/portmapper exchange should fire before either of those
- `adj_tcp_log_listen()` already opens TCP ports before any UDP, NFS should follow the
  same pattern

The NFS server needs two high UDP ports (no root):
- **mount port**: 7004  (same as `nfs_server_test` default)
- **nfs port**: 7005

ONC-RPC program numbers:
- Mount: prog `100005`, vers `1`  (`MOUNT_PROG` / `MOUNT_VERS` in `mount_client.h`)
- NFS:   prog `100003`, vers `2`  (`NFS_PROG`   / `NFS_VERS`   in `nfs_client.h`)

`portmap_set("localhost", prog, vers, port)` registers with the running rpcbind.

`adj_nfs.h` already exists at `adj/src/adj_nfs.h` with the declarations:
```c
int  adj_nfs_start(void);
void adj_nfs_stop(void);
```

---

## Key files

```
adj/src/adj_nfs.h              — already written (declarations only)
adj/src/adj_nfs.c              — Phase 1: implementation
adj/Makefile                   — Phase 1: add NFS object rules + link flags
adj/src/adj_vdj.c              — Phase 2: call adj_nfs_start / adj_nfs_stop
libcdj/src/nfs/nfs_server.c    — reference (poll API, nfs_server_init/destroy)
libcdj/src/nfs/portmap_client.c — reference (portmap_set/unset)
libcdj/src/bin/nfs_server_test.c — COPY all ops from here verbatim
```

---

## Phase 1 — Write adj_nfs.c and update Makefile

**Goal:** A self-contained `adj_nfs.c` that starts the NFS server in a background thread
and registers with the system portmapper. `adj_nfs_start()` / `adj_nfs_stop()` are the
only public functions.

### adj_nfs.c outline

```c
#include <pthread.h>
#include <stdlib.h>
#include <stdio.h>
#include "adj_nfs.h"

/* NFS source is in libcdj/src/nfs/ — included via -I flag added to Makefile */
#include "nfs_server.h"
#include "nfs_types.h"
#include "nfs_client.h"    /* NFS_PROG, NFS_VERS */
#include "mount_client.h"  /* MOUNT_PROG, MOUNT_VERS */
#include "portmap_client.h"
#include "fake_pdb.h"
#include "utf16.h"

#define ADJ_NFS_MOUNT_PORT  7004
#define ADJ_NFS_NFS_PORT    7005

/* Pioneer virtual filesystem inodes */
#define FSID        0x00000001
#define INO_ROOT    1
#define INO_PIONEER 2
#define INO_RB      3
#define INO_PDB     4

static uint8_t       *g_pdb_data  = NULL;
static uint32_t       g_pdb_len   = 0;
static nfs_server_t   g_srv;
static pthread_t      g_tid;
static volatile int   g_running   = 0;
```

**Ops:** Copy `op_list_exports`, `op_mount`, `op_getattr`, `op_lookup`, `op_readdir`,
`op_read`, `op_statfs` **verbatim** from
`libcdj/src/bin/nfs_server_test.c` lines 70–198.
These are already correct and tested; do not rewrite them.

**Poll thread:**
```c
static void* nfs_poll_thread(void *arg) {
    (void)arg;
    while (g_running)
        nfs_server_poll(&g_srv, 200);
    return NULL;
}
```

**adj_nfs_start:**
```c
int adj_nfs_start(void) {
    fake_track_t track = {
        .title         = "adj virtual track",
        .filename      = "fake.mp3",
        .file_path     = "/C/fake.mp3",
        .duration_secs = 300,
        .bpm_x100      = 12800,
    };
    g_pdb_data = fake_pdb_generate(&track, &g_pdb_len);
    if (!g_pdb_data) return -1;

    nfs_ops_t ops = {
        .list_exports = op_list_exports,
        .mount        = op_mount,
        .getattr      = op_getattr,
        .lookup       = op_lookup,
        .readdir      = op_readdir,
        .read         = op_read,
        .statfs       = op_statfs,
    };

    if (nfs_server_init(&g_srv, ADJ_NFS_MOUNT_PORT, ADJ_NFS_NFS_PORT, &ops, NULL) < 0) {
        free(g_pdb_data); g_pdb_data = NULL; return -1;
    }

    if (portmap_set("localhost", MOUNT_PROG, MOUNT_VERS, ADJ_NFS_MOUNT_PORT) < 0)
        fprintf(stderr, "adj_nfs: portmap_set mount failed (rpcbind running?)\n");
    if (portmap_set("localhost", NFS_PROG, NFS_VERS, ADJ_NFS_NFS_PORT) < 0)
        fprintf(stderr, "adj_nfs: portmap_set nfs failed\n");

    g_running = 1;
    if (pthread_create(&g_tid, NULL, nfs_poll_thread, NULL) != 0) {
        nfs_server_destroy(&g_srv);
        free(g_pdb_data); g_pdb_data = NULL;
        g_running = 0;
        return -1;
    }
    fprintf(stderr, "adj_nfs: NFS server started (mount=%u nfs=%u)\n",
            ADJ_NFS_MOUNT_PORT, ADJ_NFS_NFS_PORT);
    return 0;
}
```

**adj_nfs_stop:**
```c
void adj_nfs_stop(void) {
    if (!g_running) return;
    g_running = 0;
    pthread_join(g_tid, NULL);
    portmap_unset("localhost", MOUNT_PROG, MOUNT_VERS);
    portmap_unset("localhost", NFS_PROG,   NFS_VERS);
    nfs_server_destroy(&g_srv);
    free(g_pdb_data);
    g_pdb_data = NULL;
}
```

### Makefile changes

Add NFS source path variable near the top (after existing INCS line):
```makefile
LIBCDJ_NFS = /home/teknopaul/bzr_workspace/libcdj/src/nfs
NFS_INCS   = -I$(LIBCDJ_NFS)
NFS_OBJS   = target/nfs/xdr.o target/nfs/rpc.o target/nfs/utf16.o \
             target/nfs/portmap_client.o target/nfs/mount_client.o \
             target/nfs/nfs_client.o target/nfs/nfs_server.o \
             target/nfs/mount_server.o target/nfs/fake_pdb.o
```

Add `target/nfs` directory rule:
```makefile
target/nfs:
	mkdir -p target/nfs
```

Add individual NFS object rules (each needs `-I$(LIBCDJ_NFS)`):
```makefile
target/nfs/xdr.o: $(LIBCDJ_NFS)/xdr.c | target/nfs
	$(CC) $(CFLAGS) $(NFS_INCS) -c $< -o $@

target/nfs/rpc.o: $(LIBCDJ_NFS)/rpc.c | target/nfs
	$(CC) $(CFLAGS) $(NFS_INCS) -c $< -o $@

target/nfs/utf16.o: $(LIBCDJ_NFS)/utf16.c | target/nfs
	$(CC) $(CFLAGS) $(NFS_INCS) -c $< -o $@

target/nfs/portmap_client.o: $(LIBCDJ_NFS)/portmap_client.c | target/nfs
	$(CC) $(CFLAGS) $(NFS_INCS) -c $< -o $@

target/nfs/mount_client.o: $(LIBCDJ_NFS)/mount_client.c | target/nfs
	$(CC) $(CFLAGS) $(NFS_INCS) -c $< -o $@

target/nfs/nfs_client.o: $(LIBCDJ_NFS)/nfs_client.c | target/nfs
	$(CC) $(CFLAGS) $(NFS_INCS) -c $< -o $@

target/nfs/nfs_server.o: $(LIBCDJ_NFS)/nfs_server.c | target/nfs
	$(CC) $(CFLAGS) $(NFS_INCS) -c $< -o $@

target/nfs/mount_server.o: $(LIBCDJ_NFS)/mount_server.c | target/nfs
	$(CC) $(CFLAGS) $(NFS_INCS) -c $< -o $@

target/nfs/fake_pdb.o: $(LIBCDJ_NFS)/fake_pdb.c | target/nfs
	$(CC) $(CFLAGS) $(NFS_INCS) -c $< -o $@
```

Add `target/adj_nfs.o` rule:
```makefile
target/adj_nfs.o: src/adj_nfs.c src/adj_nfs.h
	$(CC) $(CFLAGS) $(NFS_INCS) -c src/adj_nfs.c -o $@
```

Add `target/adj_nfs.o` and `$(NFS_OBJS)` to the `OBJS` variable and the `target/adj` link line.

```sh
geany-progress done 1 \
  -r "adj/src/adj_nfs.c" \
  -r "adj/Makefile" \
  -w "The fake_pdb_generate track metadata is a placeholder — update title/bpm once we have real track data from the XDJ TCP stream" \
  -w "portmap_set failures are logged but non-fatal — NFS server still runs, XDJ just won't find it via portmapper"
```

---

## Phase 2 — Wire into adj_vdj.c

**Goal:** `adj_nfs_start()` is called before `vdj_open_sockets()` (so the NFS server
is up before any UDP announcements). `adj_nfs_stop()` is called on shutdown.

### adj_vdj.c changes

Add include at top (near `adj_tcp_log.h`):
```c
#include "adj_nfs.h"
```

Add extern near the top (near `extern const char* tcp_log_path`):
```c
extern int nfs_enabled;  /* set from CLI -N flag or similar */
```

In the init sequence (the function that calls `vdj_open_sockets`), before the TCP
listen call:
```c
if (nfs_enabled) {
    adj_nfs_start();
}

if (tcp_log_path) {
    adj_tcp_log_listen(tcp_log_path);
}
```

The current init sequence is (from the codebase):
```
adj_tcp_log_listen()      ← TCP ports bound
vdj_open_sockets()        ← UDP sockets open
vdj_exec_discovery()      ← UDP player-ID negotiation
adj_tcp_log_set_player_id()
vdj_pselect_init()        ← event loop
```

Insert `adj_nfs_start()` before `adj_tcp_log_listen()` so NFS is registered with
portmapper before TCP ports open and before any UDP is sent.

### adj.c / adj_cli.c — add -N flag

Add a `-N` / `--nfs` flag that sets `nfs_enabled = 1`.
If the flag is complex to add, a simple global in `adj.c`:
```c
int nfs_enabled = 0;
```

Set it from the existing CLI parsing wherever `-T` (tcp log) is parsed.
Alternatively keep it always-on if `-T` is given, since they're used together for
reverse-engineering sessions.

### Shutdown

In the vdj cleanup path (wherever `adj_tcp_log_stop()` would be called on exit),
add:
```c
if (nfs_enabled) adj_nfs_stop();
```

### Verify

```sh
# Start adj with NFS and TCP logging
./target/adj -v -i eth0 -T /tmp/tcp.log -N

# In another terminal, check portmapper sees our services
rpcinfo -p localhost | grep -E "100003|100005"
# Expected:
#   100005    1   udp   7004  mountd
#   100003    2   udp   7005  nfs

# Verify NFS is reachable from localhost
/home/teknopaul/bzr_workspace/libcdj/target/xdj-explore 127.0.0.1 \
    --mount-port 7004 --nfs-port 7005
```

```sh
geany-progress done 2 \
  -r "adj/src/adj_vdj.c" \
  -r "adj/src/adj.c" \
  -w "adj_nfs_stop must be called before vdj_destroy — NFS poll thread must exit before the event loop closes" \
  -w "Test rpcinfo BEFORE connecting XDJs — once the XDJ probes portmapper and gets no response it may cache the failure"
```

---

## Appendix: Startup order (after both phases complete)

```
adj_nfs_start()              ← NFS server bound on UDP 7004/7005, registered in rpcbind
adj_tcp_log_listen()         ← TCP 12523 + 1051 + 50003 bound
vdj_open_sockets()           ← UDP 50000/50001/50002 open
vdj_exec_discovery()         ← player-ID negotiation (we get player 2)
adj_tcp_log_set_player_id(2) ← SESSION_INIT patched with player 2
vdj_pselect_init()           ← event loop; per-player: connect TCP 12523 → 1051 → 50003
```

This mirrors the Pioneer boot sequence: NFS available → TCP ready → UDP announced.
