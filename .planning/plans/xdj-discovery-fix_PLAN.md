# XDJ Discovery Protocol Fix Plan

Fix two UDP discovery bugs that cause "invalid firmware" on XDJ-1000 when a
libcdj virtual CDJ joins the ProLink network.  Identified by diffing
`references/adj/doc/LinkInfo.pcapng` (good boot) against
`references/adj/doc-internal/wrong-boot-invalid-firmware.pcapng` (bad boot).

**No git** — use `.bak` copies; ask the human for any git operations.  
**Hardware required** — physical XDJ-1000 needed to confirm each fix.

---

## Bug summary

| # | Type byte | Issue | Good boot | Bad boot |
|---|-----------|-------|-----------|----------|
| 1 | `0x02` CDJ_ID_USE_REQ | sub-byte wrong | `0x01` | `0x00` |
| 2 | `0x04` CDJ_ID_SET_REQ | no response sent | CDJ replies `0x05` | adj silent |

---

## Phase 1 — Read and understand the relevant code

**Goal:** Locate every line that will change before touching anything.

### 1a. Read `cdj_set_header` in `src/c/cdj.c` (line 846)

```c
// Confirm what cdj_set_header writes at offset 0x0a and 0x0b.
// cdj_create_id_use_req_packet (line 902) calls cdj_set_header(packet, CDJ_ID_USE_REQ).
// The sub-byte (0x0b) is left as 0x00 by calloc — needs to be 0x01.
```

### 1b. Confirm `cdj_create_id_set_req_packet` structure (line 951)

Packet layout for `CDJ_ID_SET_REQ` (0x04), length `0x26`:
- `0x24` = player_id
- `0x25` = reqid/counter

`CDJ_ID_SET_RESP` (0x05) has the identical layout — same length, same offsets —
only the type byte differs.  No new function is strictly needed: can reuse the
struct with type overwrite, but a proper `cdj_create_id_set_resp_packet()` is
cleaner.

### 1c. Read `vdj_handle_managed_discovery_datagram` (vdj_discovery.c line 310)

The `CDJ_ID_SET_REQ` case (line 363–378) is entirely commented out.  The TODO
says "can't filter self packets — packet does not have IP."

Resolution: filter by **player_id** instead of IP.  The packet carries the
sender's player_id at `d_pkt->data[0x24]`.  If `d_pkt->player_id != v->player_id`
it is not our own packet.  The sender's IP comes from the backline:
`v->backline->link_members[d_pkt->player_id]->ip_addr`.

By the time the XDJ sends its `0x04` packets (~10 s into the session) it is
already in the backline from prior keepalives, so the backline lookup is safe.

### 1d. Verify `vdj_sendto_update` signature (vdj.c line 700)

Used by the existing `CDJ_ID_USE_REQ` clash handler (vdj_discovery.c line 338)
to send a unicast reply.  Same function will be used for `CDJ_ID_SET_RESP`.

### Deliverable
No file changes.  Human review of relevant code sections before any edit.

```sh
geany-progress done 1 \
  -r src/c/cdj.c:846 \
  -r src/c/cdj.c:902 \
  -r src/c/cdj.c:951 \
  -r src/c/vdj_discovery.c:310 \
  -r src/c/vdj_discovery.c:363
```

---

## Phase 2 — Fix Bug 1: wrong sub-byte in CDJ_ID_USE_REQ (0x02)

**Backup first:**
```sh
cp src/c/cdj.c src/c/cdj.c.bak
```

### Change in `cdj_create_id_use_req_packet` (cdj.c ~line 907)

After `cdj_set_header(packet, CDJ_ID_USE_REQ);` add:

```c
packet[CDJ_PACKET_TYPE_OFFSET + 1] = 0x01;  /* sub-type: acknowledged variant */
```

This sets byte `0x0b` to `0x01`, matching the good-boot pcap value.

Also update `cdj_type_to_string` (cdj.c ~line 38) so the sub_type `0x01`
branch for type `0x02` returns a useful name (aids debugging):

```c
case 0x02 : {
    if (sub_type == 0) return "CDJ_ID_USE_REQ";
    if (sub_type == 1) return "CDJ_ID_USE_REQ_ACK";
}
```

### Build and test

```sh
make
# run vdj-nfs on the DJ LAN interface
# power-cycle XDJ-1000 and watch for "invalid firmware" message
```

Expected: message may disappear, or the deck may behave differently.  
Either way, **capture a new pcap** with Wireshark to confirm the `0x02` packet
now has sub-byte `0x01`.

If message persists → Bug 1 alone is not sufficient; proceed to Phase 3.  
If message disappears → Bug 2 can still be fixed for correctness (Phase 3).

**Revert if things go badly wrong:**
```sh
cp src/c/cdj.c.bak src/c/cdj.c
make
```
(Ask the human to do `git diff` or `git checkout` if a clean revert is needed.)

```sh
geany-progress done 2 \
  -r src/c/cdj.c:902 \
  -w "Test against physical XDJ-1000 before proceeding — capture pcap to confirm sub-byte"
```

---

## Phase 3 — Fix Bug 2: respond to CDJ_ID_SET_REQ (0x04)

**Backup first:**
```sh
cp src/c/cdj.c     src/c/cdj.c.bak2
cp src/c/cdj.h     src/c/cdj.h.bak
cp src/c/vdj_discovery.c src/c/vdj_discovery.c.bak
```

### Step 3a — Add `cdj_create_id_set_resp_packet` to cdj.c

Immediately after `cdj_inc_id_set_req_packet` (~line 970), add:

```c
uint8_t*
cdj_create_id_set_resp_packet(uint16_t* length, unsigned char model,
                               uint8_t player_id, uint8_t reqid)
{
    *length = 0x26;
    uint8_t* packet = (uint8_t*) calloc(1, *length);
    if (packet) {
        cdj_set_header(packet, CDJ_ID_SET_RESP);
        cdj_set_model_name(packet + cdj_header_len(50000, CDJ_DISCOVERY), model);
        cdj_set_uint16(packet + 0x20, CDJ_DISCOVERY_VERSION);
        cdj_set_uint16(packet + 0x22, *length);
        packet[0x24] = player_id;
        packet[0x25] = reqid;
    }
    return packet;
}
```

### Step 3b — Declare it in cdj.h

After the `cdj_create_id_set_req_packet` declaration (~line 316), add:

```c
uint8_t* cdj_create_id_set_resp_packet(uint16_t* length, unsigned char model,
                                        uint8_t player_id, uint8_t reqid);
```

### Step 3c — Handle `CDJ_ID_SET_REQ` in vdj_discovery.c

Replace the entirely-commented `CDJ_ID_SET_REQ` case (lines 363–378) with:

```c
case CDJ_ID_SET_REQ: {
    if ( (d_pkt = cdj_new_discovery_packet(packet, len)) ) {
        /* Respond only to other players, not our own broadcast */
        if (d_pkt->player_id != v->player_id && d_pkt->player_id > 0) {
            vdj_link_member_t* sender =
                v->backline->link_members[d_pkt->player_id];
            if (sender && sender->ip_addr) {
                uint8_t* resp;
                uint16_t rlen;
                resp = cdj_create_id_set_resp_packet(&rlen, v->model,
                           v->player_id, d_pkt->data[0x25]);
                if (resp) {
                    struct sockaddr_in dest = *sender->ip_addr;
                    dest.sin_port = htons(CDJ_DISCOVERY_PORT);
                    vdj_sendto_update(v, &dest, resp, rlen);
                    free(resp);
                }
            }
        }
        if (discovery_ph) discovery_ph(v, d_pkt);
        free(d_pkt);
    }
    break;
}
```

Note: `vdj_sendto_update` sends on the send socket to any address; the dest
port is set to `CDJ_DISCOVERY_PORT` (50000) matching how `CDJ_ID_USE_RESP` is
sent in the existing handler above.

### Build and test

```sh
make
# run vdj-nfs, power-cycle XDJ-1000
# capture pcap: should now see adj sending type 0x05 in reply to XDJ's 0x04
```

Verify in Wireshark: after adj joins and XDJ broadcasts `CDJ_ID_SET_REQ` (type
`0x04`), adj should reply unicast with `CDJ_ID_SET_RESP` (type `0x05`) within
a keepalive cycle.

**Revert if needed:**
```sh
cp src/c/cdj.c.bak2          src/c/cdj.c
cp src/c/cdj.h.bak            src/c/cdj.h
cp src/c/vdj_discovery.c.bak  src/c/vdj_discovery.c
make
```

```sh
geany-progress done 3 \
  -r src/c/cdj.c:951 \
  -r src/c/cdj.h:316 \
  -r src/c/vdj_discovery.c:363 \
  -w "Capture pcap to confirm 0x05 reply appears after XDJ 0x04 broadcast" \
  -w "If XDJ is not yet in backline when its first 0x04 arrives, the reply will be silently skipped — check timing in pcap"
```

---

## Phase 4 — Evaluate result and clean up

After both fixes are applied and tested:

- If "invalid firmware" is gone: remove `.bak` files, ask human to commit.
- If message persists: the next candidate is the missing NFS EXPORT+MNT call
  on the XDJ's own filesystem (separate plan needed).
- Capture a final pcap and compare against both reference captures to confirm
  the discovery sequence now matches the good-boot reference.

```sh
# Remove backup files once satisfied
rm -f src/c/cdj.c.bak src/c/cdj.c.bak2 src/c/cdj.h.bak src/c/vdj_discovery.c.bak
```

Ask the human to run:
```
git add src/c/cdj.c src/c/cdj.h src/c/vdj_discovery.c
git commit -m "fix XDJ discovery: correct CDJ_ID_USE_REQ sub-byte and respond to CDJ_ID_SET_REQ"
```

```sh
geany-progress done 4 \
  -w "If invalid firmware persists after both fixes, NFS EXPORT/MNT on XDJ is the next suspect"
```
