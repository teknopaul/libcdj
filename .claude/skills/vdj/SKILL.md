---
name: vdj
description: how to work with the VDJ library
---

<objective>
Work with libcdj/libvdj — a pure C implementation of the Pioneer Pro DJ Link protocol. The libraries let you join a CDJ network as a virtual player, observe real CDJs, and emit beats, status, and discovery packets.
</objective>

<process>

## Libraries and headers

- `libcdj.so` — packet parsing/creation (`src/c/cdj.h`)
- `libvdj.so` — virtual CDJ lifecycle and threads (`src/c/vdj.h`)
- Build: `make` produces both `.so` files and binaries under `target/`
- Headers live in `src/c/` — use `-Isrc/c` when compiling

## Core types

```c
vdj_t               // the local virtual CDJ (one per process)
vdj_backline_t      // snapshot of all known network members
vdj_link_member_t   // one remote CDJ/XDJ on the network

cdj_discovery_packet_t   // port 50000 — keepalives, ID assignment
cdj_beat_packet_t        // port 50001 — beat timing, BPM
cdj_cdj_status_packet_t  // port 50002 — full CDJ status (flags, bpm, pitch…)
cdj_mixer_status_packet_t
```

## Initialization pattern

```c
vdj_t *v = vdj_init_iface("eth0", VDJ_FLAG_DEV_XDJ | VDJ_FLAG_AUTO_ID);
vdj_open_sockets(v);
vdj_exec_discovery(v);   // blocking handshake — claims a player ID
```

`vdj_init(flags)` auto-detects the interface. Use `vdj_init_iface(iface, flags)` when the machine has multiple NICs (e.g. `enx001060ce107c` for the CDJ link-local network).

## Init flags

| Flag                          | Meaning                         |
|-------------------------------|---------------------------------|
| `VDJ_FLAG_DEV_XDJ`            | Announce as XDJ-1000            |
| `VDJ_FLAG_DEV_CDJ`            | Announce as CDJ-1000            |
| `VDJ_FLAG_AUTO_ID`            | Auto-assign player number (1–4) |
| `VDJ_FLAG_PRINT_IP`           | Print resolved IP to stdout     |
| `0x01`–`0x06` OR'd into flags | Force a specific player_id      |

## Managed thread model

Start threads after `vdj_exec_discovery`. Each thread calls back into your handler:

```c
// receives cdj_discovery_packet_t*, maintains v->backline
vdj_init_managed_discovery_thread(v, my_discovery_ph);

// receives cdj_cdj_status_packet_t* from each CDJ
vdj_init_managed_update_thread(v, my_update_ph);

// sends status unicast to all link members every 200 ms
vdj_init_status_thread(v);

// sends beat broadcasts (needs v->bpm set)
vdj_init_beatout_thread(v);
vdj_start_beatout_thread(v);
```

Handler signatures:
```c
typedef void (*vdj_discovery_ph)(vdj_t *v, cdj_discovery_packet_t *d_pkt);
typedef void (*vdj_update_ph)   (vdj_t *v, cdj_cdj_status_packet_t *cs_pkt);
typedef void (*vdj_beat_ph)     (vdj_t *v, cdj_beat_packet_t *b_pkt);
typedef void (*vdj_expired_h)   (vdj_t *v, vdj_link_member_t *m);
```

## Single-thread alternative: vdj_pselect

For event-loop programs (no extra threads), use `vdj_pselect_init` to multiplex all CDJ sockets into one `pselect()` call. Pass NULL for handlers you don't need.

```c
#include "vdj_pselect.h"
vdj_pselect_init(v, discovery_ph, NULL, beat_ph, NULL, update_ph, expired_h);
// blocks until vdj_pselect_stop(v)
```

## Key vdj_t fields

```c
v->bpm           // float — virtual player's current BPM
v->master        // uint8_t — set 1 to claim beat master
v->active        // uint8_t — set 1 to appear as "playing"
v->bar_index     // 0–3 beat position in bar
v->backline      // vdj_backline_t* — all known remote players
v->player_id     // assigned after exec_discovery
v->client        // void* — hook for extension state (e.g. NFS ctx)
```

## Reading remote CDJ state

```c
vdj_link_member_t *m = vdj_get_link_member(v, player_id);
// m->bpm, m->play_state, m->master_state, m->ip_addr, m->gone
```

From a status packet:
```c
cdj_status_flags(cs_pkt)       // CDJ_STAT_FLAG_PLAY | _MASTER | _SYNC | _ONAIR …
cdj_status_calculated_bpm(cs_pkt)
cdj_status_playing_from_slot(cs_pkt)  // CDJ_STAT_SLOT_USB, _SD, _RB …
```

## Sending packets

```c
vdj_send_keepalive(v);             // broadcast on 50000
vdj_send_status(v);                // unicast to all members on 50002
vdj_broadcast_beat(v, bpm, bar_pos); // broadcast on 50001
vdj_set_playing(v, 1);             // mark active
```

## Ports (Pioneer Pro DJ Link)

| Port  | Use                                              |
|-------|--------------------------------------------------|
| 50000 | Discovery broadcasts (keepalives, ID assignment) |
| 50001 | Beat timing broadcasts                           |
| 50002 | Status unicasts (player → player)                |

## Built binaries

| Binary             | Purpose                                                |
|--------------------|--------------------------------------------------------|
| `target/vdj`       | Minimal virtual CDJ — joins ProLink, does nothing else |
| `target/vdj-1`     | Single-event test variant                              |
| `target/vdj-debug` | Verbose protocol debugging                             |
| `target/vdj-mon`   | Monitor/display network state                          |
| `target/cdj-mon`   | Monitor CDJ packets (read-only, no join)               |
| `target/cdj-scan`  | Scan for CDJs on network                               |

## Network context

The CDJ link-local network uses `169.254.x.x` auto-IP. On this machine the CDJ interface is `enx001060ce107c` with IP `169.254.6.231`. Always pass `-i enx001060ce107c` when testing.

## Teardown

```c
vdj_stop_threads(v);  // or stop individual threads
vdj_destroy(v);
```

</process>
