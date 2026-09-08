# VDJ Connect/Disconnect (Reconnect) Plan

## Goal

Enable a GUI to Connect and Disconnect from the Pioneer ProLink network cleanly.
Currently `vdj_t` can only be initialized once — threads use static atomics that
prevent restart. This plan moves that state into `vdj_t`, adds high-level
`vdj_connect()`/`vdj_disconnect()` APIs to libcdj, and exposes
`adj_vdj_connect()`/`adj_vdj_disconnect()` to adj for GUI wiring.

## Background

- `vdj_pselect_init()` starts a single thread that multiplexes all CDJ sockets.
  adj always uses this path (not the individual thread APIs).
- Three files contain static atomics / static state that prevent clean restart:
  - `vdj_pselect.c`: `vdj_pselect_running`, `self_pipe[2]`, `keepalive_ticker`
  - `vdj.c`: `vdj_discovery_running`, `vdj_beat_running`, `vdj_update_running`,
    `vdj_status_running` (unused by adj/pselect path but still present)
  - `vdj_discovery.c`: `vdj_keepalive_running`
- `vdj_pselect_init()` signature mismatch: `.h` declares `expired_h` parameter
  but `.c` implementation omits it — adj call passes 7 args to a 6-param function.
- `pthread_t` handles are lost after creation, so clean `pthread_join()` is not
  possible today.

---

## Phase 1 — libcdj: make pselect restartable

**Files:** `src/c/vdj.h`, `src/c/vdj_pselect.c`, `src/c/vdj.c`, `src/c/vdj_discovery.c`

### 1a — Add per-instance thread state to `vdj_t`

In `vdj.h`, add to `vdj_t` struct:

```c
    // pselect thread state (moved from statics in vdj_pselect.c)
    unsigned _Atomic    pselect_running;
    pthread_t           pselect_thread;
    int                 self_pipe[2];       // [0]=read [1]=write
    int                 keepalive_ticker;

    // individual thread states (moved from statics in vdj.c / vdj_discovery.c)
    unsigned _Atomic    discovery_running;
    unsigned _Atomic    beat_running;
    unsigned _Atomic    update_running;
    unsigned _Atomic    status_running;
    unsigned _Atomic    keepalive_running;
```

Initialise all to 0 in `vdj_init_net()` (already `calloc` so zeroed, but add
`atomic_init` calls for clarity).

### 1b — Rewrite vdj_pselect.c to use vdj_t state

- Replace every reference to static `vdj_pselect_running` → `v->pselect_running`
- Replace static `self_pipe[2]` → `v->self_pipe`
- Replace static `keepalive_ticker` → `v->keepalive_ticker`
- In `vdj_pselect_init()`: store `thread_id` into `v->pselect_thread` (don't discard it)
- Fix signature: add `vdj_expired_h expired_h` to implementation to match `.h` and
  populate `handlers->expired_h`

`vdj_pselect_stop()` stays the same conceptually — writes SIGQUIT byte to
`v->self_pipe[1]`.

Add new function:

```c
void vdj_pselect_join(vdj_t* v);  // pthread_join(v->pselect_thread, NULL)
```

Declare in `vdj_pselect.h`.

### 1c — Rewrite vdj.c / vdj_discovery.c to use vdj_t state

Replace each static atomic with the corresponding field on `vdj_t*`:
- `vdj_discovery_running` → `v->discovery_running`
- `vdj_beat_running`      → `v->beat_running`
- `vdj_update_running`    → `v->update_running`
- `vdj_status_running`    → `v->status_running`
- `vdj_keepalive_running` → `v->keepalive_running`

All thread loop functions already receive `vdj_t*` via `tinfo->v`, so the loop
condition changes from `while (vdj_beat_running)` to `while (v->beat_running)`.

Store `pthread_t` for status thread and keepalive thread in `vdj_t` as well
(`pthread_t status_thread`, `pthread_t keepalive_thread`) for future join.

### Completion marker

```sh
geany-progress done 1 \
  -r src/c/vdj.h \
  -r src/c/vdj_pselect.c \
  -r src/c/vdj.c \
  -r src/c/vdj_discovery.c \
  -w "All static atomics gone — test that vdj_simple and cdj_mon still build"
```

---

## Phase 2 — libcdj: high-level connect / disconnect API

**Files:** `src/c/vdj.h`, `src/c/vdj.c`

### 2a — Add connect/disconnect functions

In `vdj.h` declare:

```c
/* Connect: open sockets, run discovery, start pselect event thread.
 * Equivalent to the three-step init sequence callers currently do manually.
 * Returns CDJ_OK on success.                                                 */
int vdj_connect(vdj_t* v,
    vdj_discovery_ph          discovery_ph,
    vdj_discovery_unicast_ph  discovery_unicast_ph,
    vdj_beat_ph               beat_ph,
    vdj_beat_unicast_ph       beat_unicast_ph,
    vdj_update_ph             update_ph,
    vdj_expired_h             expired_h);

/* Disconnect: stop pselect thread, wait for it to exit, clear backline.
 * After this call sockets are closed. Call vdj_connect() to reconnect,
 * or vdj_destroy() to free the vdj_t.                                        */
void vdj_disconnect(vdj_t* v);

/* Reset backline — frees all link_members and zeroes the backline struct.
 * Called internally by vdj_disconnect(); exposed for testing.                */
void vdj_clear_backline(vdj_t* v);
```

### 2b — Implement in vdj.c

`vdj_connect()` body:
```c
if (vdj_open_sockets(v) != CDJ_OK) return CDJ_ERROR;
if (vdj_exec_discovery(v) != CDJ_OK) { vdj_close_sockets(v); return CDJ_ERROR; }
return vdj_pselect_init(v, discovery_ph, discovery_unicast_ph,
                        beat_ph, beat_unicast_ph, update_ph, expired_h);
```

`vdj_disconnect()` body:
```c
vdj_pselect_stop(v);
vdj_pselect_join(v);   // waits for clean exit + socket close
vdj_clear_backline(v);
v->have_id = 0;
```

`vdj_clear_backline()`: iterate backline array, free each `link_member`, set
pointer to NULL, zero `master_id`, `master_bpm`, `sync_counter`.

### 2c — Update vdj_destroy() to be safe post-disconnect

`vdj_destroy()` must not double-close sockets if called after `vdj_disconnect()`
(sockets already closed by pselect exit). Guard with `if (v->pselect_running)` or
check fd > 0 before close (already partly done with `if (v->discovery_socket_fd)`).
After disconnect sockets should be zeroed.

Zero each `socket_fd` field on close in the `vdj_close_*` statics so double-close
is a no-op.

### Completion marker

```sh
geany-progress done 2 \
  -r src/c/vdj.h \
  -r src/c/vdj.c \
  -w "vdj_pselect_join blocks — ensure disconnect is not called from pselect thread"
```

---

## Phase 3 — adj: reconnect wrappers

**Files:** `adj/src/adj_vdj.h`, `adj/src/adj_vdj.c`, `adj/src/adj.h`

### 3a — New ADJ_ITEM constant for VDJ connection state

In `adj.h`:

```c
#define ADJ_ITEM_VDJ_STATE  0x0C   // "connected" | "disconnected" | "error"
```

### 3b — New adj_vdj API

In `adj_vdj.h` add:

```c
/**
 * Connect to the ProLink network.
 * Creates (or re-creates) the vdj_t and starts the pselect thread.
 * adj->vdj is set to the new vdj_t on success, NULL on failure.
 * Fires data_change_handler(ADJ_ITEM_VDJ_STATE, "connected") on success.
 * Safe to call when already connected — disconnects first.
 */
int adj_vdj_connect(adj_seq_info_t* adj, char* iface, uint32_t flags,
                    float bpm, uint32_t vdj_offset);

/**
 * Disconnect from the ProLink network.
 * Stops pselect thread, waits for clean exit, clears backline.
 * adj->vdj is NOT freed — the vdj_t can be reconnected.
 * Fires data_change_handler(ADJ_ITEM_VDJ_STATE, "disconnected").
 */
void adj_vdj_disconnect(adj_seq_info_t* adj);
```

### 3c — Implement in adj_vdj.c

`adj_vdj_connect()`:
1. If `adj->vdj` is NULL, call `vdj_init_iface(iface, flags)` and store in `adj->vdj`.
2. If `adj->vdj` is not NULL and pselect is running, call `adj_vdj_disconnect()` first.
3. Reset local state: `memset(high_slots, 0, ...)`, `adj_diff_reset()`,
   `difflock_default = vdj_offset`, `adj_estimate_bpm_init()`, `adj_lock_on = 0`.
4. If `bpm > 1.0`, set `adj->vdj->bpm = bpm`.
5. Render backline (TUI path, guarded by `if (tui)` — GUI path is via callbacks).
6. Call `vdj_connect(adj->vdj, adj_discovery_ph, NULL, adj_beat_ph, NULL, adj_update_ph, adj_expired_h)`.
7. On success: `adj->vdj->client = adj`; fire `data_change_handler(ADJ_ITEM_VDJ_STATE, "connected")`.
8. On failure: fire `data_change_handler(ADJ_ITEM_VDJ_STATE, "error")`; return ADJ_ERR.

`adj_vdj_disconnect()`:
1. Guard: if `adj->vdj == NULL` return.
2. Call `vdj_disconnect(adj->vdj)`.
3. Fire `data_change_handler(ADJ_ITEM_VDJ_STATE, "disconnected")`.

### 3d — Update adj_vdj_init() to delegate to adj_vdj_connect()

The existing `adj_vdj_init()` is the old entry point (called from `adj.c main()`).
Refactor it to:
1. Call `vdj_init_iface()` → assign to `adj->vdj`
2. Return `adj->vdj` (existing callers unaffected).

Or keep `adj_vdj_init()` as-is for backward compat and have it call
`adj_vdj_connect()` internally. The key constraint is that `adj.c` assigns the
returned `vdj_t*` to `adj->vdj` — refactoring `adj_vdj_init` to handle that
internally is cleaner.

Preferred approach: `adj_vdj_init()` calls `vdj_init_iface()`, assigns to
`adj->vdj`, then calls the shared connect sequence from `adj_vdj_connect()`.
Extract the connection logic into a `static int adj_vdj_do_connect(...)` helper
shared by both.

### Completion marker

```sh
geany-progress done 3 \
  -r /home/teknopaul/bzr_workspace/adj/src/adj_vdj.h \
  -r /home/teknopaul/bzr_workspace/adj/src/adj_vdj.c \
  -r /home/teknopaul/bzr_workspace/adj/src/adj.h \
  -w "adj_vdj_init() still used by adj.c main — keep its signature unchanged"
```

---

## Phase 4 — GUI wiring (adj)

**Files:** `adj/src/adj.c`, plus the GUI source (not yet in repo)

### 4a — Document GUI → adj call contract

The GUI Connect button should call:
```c
adj_vdj_connect(adj, iface, vdj_flags, bpm, vdj_offset);
```
The GUI Disconnect button should call:
```c
adj_vdj_disconnect(adj);
```
Both calls are safe to make from any thread (they post state via atomics / pselect
signals; only `vdj_pselect_join()` blocks, which is called internally on the adj
background thread or caller's thread — GUI should call these off the UI thread).

### 4b — GUI state update via data_change_handler

The existing `adj_ui_t.data_change_handler` is the right hook.

The GUI registers a handler that receives `(adj_ui_t*, adj_seq_info_t*, int item, char* data)`.

On `ADJ_ITEM_VDJ_STATE` it should update the Connect/Disconnect button label and
enable/disable appropriately:
- `"connected"` → button shows "Disconnect" (enabled)
- `"disconnected"` → button shows "Connect" (enabled)
- `"error"` → button shows "Connect" (enabled, display error message)

### 4c — adj.c main() — vdj init path (no change needed)

The `-v` flag path in `adj.c main()` calls `adj_vdj_init()`.
After Phase 3, this continues to work unchanged.

If a future GUI replaces the CLI entry point, it should call `adj_vdj_connect()` /
`adj_vdj_disconnect()` directly instead of relying on the `-v` flag.

### Completion marker

```sh
geany-progress done 4 \
  -w "GUI calls to adj_vdj_connect/disconnect must be off the UI event loop thread" \
  -w "vdj_destroy() must be called on app exit after adj_vdj_disconnect()"
```

---

## Key invariants

1. **One pselect thread per vdj_t at a time.** Guard `vdj_pselect_init()` with
   `if (v->pselect_running) return CDJ_ERROR;`.

2. **vdj_pselect_join() must not be called from the pselect thread itself** (deadlock).
   adj's connect/disconnect calls come from the GUI thread — safe.

3. **vdj_t lifetime**: create once with `vdj_init_iface()`, connect/disconnect many
   times, destroy once with `vdj_destroy()` on shutdown.
   `adj->vdj` is therefore NOT NULL between connect calls even while disconnected.

4. **adj static state** (`difflock_player`, `master`, `bpm` etc. in adj_vdj.c) must
   be reset on each `adj_vdj_connect()` call (already done in current `adj_vdj_init()`).

5. **Socket fd zeroing**: after each `close()` in the `vdj_close_*` statics, zero the
   `vdj_t` fd field so `vdj_destroy()` called post-disconnect does not double-close.
