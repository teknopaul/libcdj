# ProLink TCP — Reverse Engineering Plan

## Background & Context

Modern Pioneer XDJs open TCP connections as part of the ProLink protocol. This is
undocumented — the [Deep Symmetry analysis](https://djl-analysis.deepsymmetry.org/djl-analysis/missing.html)
flags it as missing. When `adj` connects to physical XDJs, the decks show "connected to a
deck with older firmware", likely because the TCP handshake is absent.

All capture tooling lives in the **adj project** (accessible from this workspace as
`references/adj/`). adj already handles the full ProLink membership ceremony (keepalives,
player-ID negotiation, discovery) that an XDJ expects before opening TCP connections.
A standalone tool would need to replicate all of that, so adj is the right home.

Protocol parsing, once the wire format is confirmed, belongs in **libcdj**.

**Available hardware for live testing:**
- XDJ-700 (older/cheaper model, may have reduced feature set or older firmware behaviour)
- XDJ-1000 (more capable, likely closer to CDJ-2000nexus behaviour)

Both models should be captured independently during Phase 2. Fields that are constant
across all three data points (CDJ-2000nexus pcap + XDJ-700 live + XDJ-1000 live) are
almost certainly protocol constants. Fields that differ between models are likely
firmware version numbers, capability flags, or model identifiers — exactly the kind
of metadata needed to interpret message structure.

---

## What we know from the PCAP (`references/adj/doc/LinkInfo-tcp.pcapng`)

> **Note:** This capture was taken from a **CDJ-2000nexus + DJM-2000nexus** network —
> different hardware from the XDJ-700 and XDJ-1000 available for live testing. Differences
> in the TCP messages between the pcap and live captures are expected and informative.

Two TCP connections were captured:

**Port 12523 — RemoteDB server (already known)**
- Initiated by 169.254.192.112 (software) → 169.254.244.181 (XDJ)
- Handshake: client sends `\x00\x00\x00\x0fRemoteDBServer\x00` (length-prefixed name)
- Server responds `04 1b`
- This is the existing ProLink database lookup channel — already partly understood

**Port 1051 — New TCP ProLink protocol (the target)**
- Both sides send identical 5-byte handshake: `11 00 00 00 01`
- Messages share the ProLink UDP magic: `11 87 23 49 ae`
- Framing: a stream of variable-length messages, each starting with the 5-byte magic
- Tag system found within messages:
  - `0x11` = 32-bit integer field (`11 xx xx xx xx` — tag + 4-byte big-endian uint)
  - `0x26` = UTF-16-**BE** string field (`26 NN NN NN NN` — 4-byte char count + char-count×2 bytes of UTF-16BE)
  - `0x01` = single-byte field (structure not yet confirmed)
- Message types seen (bytes 10–13 of each message):
  - `0x1040 010f` — request/handshake message
  - `0x1041 010f` — large track-metadata message (title, artist, album, key, comment, BPM string)
  - `0x1042 010f` — end of burst / acknowledgement
  - `0x1020 020f` — short acknowledgement sent by client
  - `0x1030 000f` — appears before track info burst (request?)
- Track metadata confirmed present: track title, artist, key (e.g. `"F#, 2b, +9"`), BPM display string, comment, genre
- **String encoding is UTF-16-BE** — differs from rekordbox `.pdb` files which use UTF-16-LE

## Hypotheses (unconfirmed, require live testing)
- The `0x1041` burst is the XDJ pushing track info when a track loads
- The "older firmware" message may simply require completing the TCP handshake
- TCP is used for: long track names/metadata, liveness detection, possibly beat-grid data
- Same structs shared between UDP and TCP (same magic, same tag encoding)

## Key unknowns requiring live capture
- Full message header layout (player_id field location, sequence number, length field)
- Whether the client must send anything to receive track info, or XDJ pushes unsolicited
- What triggers each message type
- Whether port 1051 is initiated by the XDJ or the software
- What the "older firmware" message actually checks in the TCP handshake

---

## Target file structure

```
# Analysis artefacts (adj project — edit via references/adj/)
references/adj/doc/tcp-protocol-notes.md    — decoded pcap findings + live capture notes
references/adj/tools/decode-tcp-pcap.sh     — tshark decode script

# adj TCP capture module (adj project — edit via references/adj/)
references/adj/src/adj_tcp_log.c            — Phase 2: TCP listener/connect thread
references/adj/src/adj_tcp_log.h
# (also modifies adj_vdj.c, adj_cli.c, adj.c, Makefile in that project)

# Protocol stub in libcdj (populated in Phase 3 once wire format confirmed)
src/c/cdj_tcp.h                             — Phase 3: message type constants, partial structs
src/c/cdj_tcp.c                             — Phase 3: handshake helper only
```

---

## Phase 1 — PCAP Static Analysis

**Goal:** Fully decode `LinkInfo-tcp.pcapng` and write protocol notes covering everything
inferable without live hardware. Produce a repeatable decode script.

### 1.1 — Decode script

Write `references/adj/tools/decode-tcp-pcap.sh`:
```sh
#!/bin/bash
# Decodes LinkInfo-tcp.pcapng, prints each TCP payload in hex + annotated tag decode
# Usage: ./decode-tcp-pcap.sh [path-to-pcap]
PCAP=${1:-doc/LinkInfo-tcp.pcapng}
tshark -r "$PCAP" -Y "tcp.len > 0" \
  -T fields -e frame.number -e ip.src -e tcp.srcport -e ip.dst -e tcp.dstport \
             -e tcp.len -e data.data \
  2>/dev/null | python3 tools/decode-tcp-frames.py
```

Also write `references/adj/tools/decode-tcp-frames.py` which:
- Reads tshark TSV from stdin
- For each row: prints frame#, direction (src:port → dst:port), length
- Hex-dumps the payload with ASCII sidebar (16 bytes per line)
- For port-1051 frames: best-effort tag decode (`0x11` = uint32, `0x26` = UTF-16-BE string)
- For port-12523 frames: show RemoteDB length-prefix framing

Python is used here (not awk) because UTF-16-BE decoding in awk is painful.

### 1.2 — Protocol notes document

Write `references/adj/doc/tcp-protocol-notes.md` covering:
- The two connections (port 1051 and 12523)
- Byte-by-byte breakdown of the port-1051 handshake: `11 00 00 00 01`
- Annotated hex dump of one `0x1041` message with field labels
- Tag encoding table (`0x11`, `0x26`, `0x01`)
- Message type table with known/guessed purpose for each
- All UTF-16-BE strings decoded from the capture (track titles, artist, key, genre)
- **Hardware column in message tables** — note source as `CDJ-2000nexus (pcap)` so
  XDJ-700 and XDJ-1000 columns can be filled in during Phase 2
- Open questions for live capture to resolve

**Verification:** Run the script, confirm it prints
`"We're All We Need feat. Zoë Johnston (16 Bit Lolitas Remix)"` from frame 323.

```sh
geany-progress done 1 \
  -r "references/adj/doc/tcp-protocol-notes.md" \
  -r "references/adj/tools/decode-tcp-pcap.sh" \
  -w "String encoding on TCP is UTF-16-BE, not LE as in .pdb files — never mix these"
```

---

## Phase 2 — adj TCP Logger for Live Capture

**Goal:** When `adj` runs in Virtual CDJ mode (`-v`), also open TCP port 1051, accept
inbound connections and attempt outbound connects to each discovered player. Log all TCP
traffic to a file with timestamps. The TUI must remain fully functional — TCP work runs
in background threads and never touches stdout.

### New files

**`references/adj/src/adj_tcp_log.h`**
```c
#ifndef ADJ_TCP_LOG_H
#define ADJ_TCP_LOG_H
#include <cdj/vdj.h>

int  adj_tcp_log_init(vdj_t* v, const char* log_path);
void adj_tcp_log_connect(uint32_t player_ip);   // call on each new discovery
void adj_tcp_log_stop(void);

#endif
```

**`references/adj/src/adj_tcp_log.c`** — outline:
```c
// adj_tcp_log_init:
//   open log file (append mode)
//   bind TCP server on port 1051 with SO_REUSEADDR; handle EADDRINUSE gracefully
//   start accept_thread
//
// accept_thread: accept() loop, spawns conn_thread per inbound connection
//
// adj_tcp_log_connect: spawns outbound connect_thread to player_ip:1051
//   non-blocking connect with poll(); log ECONNREFUSED without noise
//
// conn_thread (shared by inbound/outbound):
//   send handshake: 11 00 00 00 01
//   read loop: hex-dump each recv() to log file with:
//     [HH:MM:SS.mmm] [DIR player_ip:port] NNNN: XX XX XX XX  |....|
//   on EOF or error: log disconnect and exit
//
// adj_tcp_log_stop:
//   set stop flag, close listener fd, join threads, fclose log
//
// Log file is written with fflush after each message — safe to tail -f during session
```

### Modify `references/adj/src/adj_vdj.c`

In `adj_vdj_init()`, after `vdj_exec_discovery` succeeds, add:
```c
extern const char* tcp_log_path;   // set from CLI before adj_vdj_init
if (tcp_log_path) {
    adj_tcp_log_init(v, tcp_log_path);
}
```

In `adj_discovery_ph()`, on `CDJ_KEEP_ALIVE` for a new member:
```c
if (tcp_log_path) {
    adj_tcp_log_connect(d_pkt->ip_addr);
}
```

### Modify `references/adj/src/adj.c` and `adj_cli.c`

Add flag `--tcp-log <file>` (or `-T <file>`):
```
    -T <file>  log TCP ProLink stream to file when using -v (Virtual CDJ mode)
```

Store path in a `static const char* tcp_log_path` global (or add to `adj_seq_info_t`
if the struct already has a conf/options block).

### Makefile addition in `references/adj/Makefile`

```makefile
target/adj_tcp_log.o: src/adj_tcp_log.c src/adj_tcp_log.h
	$(CC) $(CFLAGS) -Isrc -I$(LIBCDJ_INCLUDE) -c src/adj_tcp_log.c -o $@
```

Add `target/adj_tcp_log.o` to the `OBJS` variable.

### Live test procedure (requires physical XDJs)

Capture each model separately so their messages can be diffed cleanly.

**Session A — XDJ-700 only (connect only the 700, leave 1000 off)**
```sh
cd references/adj && make
./target/adj -v -i eth0 -T /tmp/tcp-xdj700-$(date +%s).log
```

**Session B — XDJ-1000 only (connect only the 1000, leave 700 off)**
```sh
./target/adj -v -i eth0 -T /tmp/tcp-xdj1000-$(date +%s).log
```

**Session C — both models together**
```sh
./target/adj -v -i eth0 -T /tmp/tcp-both-$(date +%s).log
```

For each session:
1. Load the same track on the deck(s), let it play for 60 seconds
2. `tail -f /tmp/tcp-*.log` in another terminal — watch for `0x1041` messages on track load
3. Confirm UTF-16-BE strings decode correctly (compare with deck display)
4. Note any message types not seen in the pcap

**Cross-model comparison (after sessions A and B):**

Run `decode-tcp-pcap.sh` equivalent against each log and diff the handshake and
non-string bytes side by side. Specifically look for:
- Bytes that differ between XDJ-700 and XDJ-1000 handshake messages → likely firmware
  version or model ID fields
- Bytes that are identical across XDJ-700, XDJ-1000, and the CDJ-2000nexus pcap →
  protocol constants, safe to define in `cdj_tcp.h`
- Message types present on one model but absent on the other → optional/capability-gated features
- String fields: do both models send the same track metadata fields in the same order?

Add a **"Model Comparison"** table to `references/adj/doc/tcp-protocol-notes.md`:

| Offset | CDJ-2000nexus (pcap) | XDJ-1000 (live) | XDJ-700 (live) | Interpretation |
|--------|---------------------|-----------------|----------------|----------------|
| 0–4    | `11 87 23 49 ae`    | ?               | ?              | ProLink magic  |
| 5–9    | `11 ff ff ff fe`    | ?               | ?              | player ID?     |
| ...    | ...                 | ...             | ...            | ...            |

Session C (both models) will show whether the XDJs send different messages to adj
depending on which other models are present on the network.

Document all new findings in `references/adj/doc/tcp-protocol-notes.md`.

```sh
geany-progress done 2 \
  -r "references/adj/src/adj_tcp_log.c" \
  -r "references/adj/src/adj_tcp_log.h" \
  -r "references/adj/doc/tcp-protocol-notes.md" \
  -w "TCP port 1051 may conflict if another ProLink member is present — handle EADDRINUSE, log and continue" \
  -w "Capture XDJ-700 and XDJ-1000 separately before running both together — mixed sessions are harder to diff" \
  -w "Live test required before Phase 3 — do not write the libcdj parser stub until message framing is confirmed"
```

---

## Phase 3 — Protocol Stub in libcdj

**Goal:** Based on Phases 1–2 findings, write the initial `cdj_tcp.h` in libcdj with
message-type constants, tag constants, and any struct definitions that are confidently
understood. Mark everything uncertain clearly. This file is a living document.

**Do not implement a message parser here.** `cdj_tcp_parse_message()` is deferred until
Phase 2 live capture confirms message boundaries and the length field location.

### New file: `src/c/cdj_tcp.h`

```c
// ProLink TCP protocol — WORK IN PROGRESS
// Based on: references/adj/doc/LinkInfo-tcp.pcapng + live capture
// See: references/adj/doc/tcp-protocol-notes.md

#pragma once
#include <stdint.h>

#define CDJ_TCP_PORT              1051
#define CDJ_REMOTEDB_PORT        12523

#define CDJ_TCP_MAGIC            "\x11\x87\x23\x49\xae"
#define CDJ_TCP_MAGIC_LEN        5

// Both sides send this immediately after TCP connect
#define CDJ_TCP_HANDSHAKE        "\x11\x00\x00\x00\x01"
#define CDJ_TCP_HANDSHAKE_LEN    5

// Tag type bytes within messages
#define CDJ_TCP_TAG_INT          0x11  // + 4-byte big-endian uint32
#define CDJ_TCP_TAG_STRING       0x26  // + 4-byte char count + char_count*2 bytes UTF-16BE
#define CDJ_TCP_TAG_BYTE         0x01  // structure TBD /* UNCONFIRMED */

// Message types — upper 16 bits of 4-byte field at offset 10
// Lower 16 bits appear to be flags/version; exact meaning TBD /* UNCONFIRMED */
#define CDJ_TCP_MSG_REQ          0x1040  // request / session handshake
#define CDJ_TCP_MSG_TRACK        0x1041  // track metadata push (confirmed from pcap)
#define CDJ_TCP_MSG_END          0x1042  // end of burst
#define CDJ_TCP_MSG_ACK          0x1020  // client acknowledgement
#define CDJ_TCP_MSG_QUERY        0x1030  // query before track burst /* UNCONFIRMED */

// TCP strings are UTF-16-BE.
// CAUTION: rekordbox .pdb files use UTF-16-LE — these are NOT interchangeable.
```

### New file: `src/c/cdj_tcp.c`

Minimal — handshake helper only until framing is confirmed:
```c
#include "cdj_tcp.h"
#include <unistd.h>

int
cdj_tcp_handshake(int fd)
{
    return write(fd, CDJ_TCP_HANDSHAKE, CDJ_TCP_HANDSHAKE_LEN) == CDJ_TCP_HANDSHAKE_LEN ? 0 : -1;
}
```

Declare in `cdj_tcp.h`: `int cdj_tcp_handshake(int fd);`

### Update `references/adj/src/adj_tcp_log.c`

Replace the inline handshake bytes with `#include <cdj/cdj_tcp.h>` and
`cdj_tcp_handshake(fd)`.

**Completion criteria:**
- `cdj_tcp.h` compiles cleanly included from a libcdj translation unit
- Every `/* UNCONFIRMED */` constant has a note in `tcp-protocol-notes.md` explaining what live capture needs to confirm it
- Protocol notes have a "Confirmed vs Hypothetical" section

```sh
geany-progress done 3 \
  -r "src/c/cdj_tcp.h" \
  -r "src/c/cdj_tcp.c" \
  -r "references/adj/doc/tcp-protocol-notes.md" \
  -w "cdj_tcp_parse_message() is intentionally absent — add it only after message length field is confirmed by live capture"
```

---

## Appendix: Annotated Hex Dump of Key PCAP Frames

### Frame 208 — RemoteDB handshake (port 12523)
```
00 00 00 0f  52 65 6d 6f 74 65 44 42  53 65 72 76  65 72 00
[  len=15 ]  R  e  m  o  t  e  D  B   S  e  r  v   e  r  \0
```
Server responds: `04 1b` (meaning unknown).

### Frames 214/215 — TCP 1051 handshake (both sides send identical)
```
11 00 00 00 01
```
Tag `0x11` (int), value `0x00000001` = 1.

### Frame 216 — First real message (client → XDJ, 37 bytes)
```
11 87 23 49 ae  -- ProLink magic
11 ff ff ff fe  -- tag 0x11, value 0xfffffffe (self / broadcast player ID?)
10 00 00 0f     -- message type fragment (full layout TBD)
01 14 00 00 00 0c 06 00 00 00 00 00 00 00 00 00 00 00
11 00 00 00 03  -- tag 0x11, value 3
```

### Frame 323 excerpt — Track info (0x1041, 1302 bytes total)
```
26 00 00 00 3c  -- tag 0x26, 60 chars, UTF-16-BE:
  → "We're All We Need feat. Zoë Johnston (16 Bit Lolitas Remix)"

26 00 00 00 0f  -- 15 chars
  → "Above & Beyond"

26 00 00 00 2f  -- 47 chars
  → "Thing Called Love (Feat. Richard Bedford) - EP"

26 00 00 00 0b  -- 11 chars
  → "F#, 2b, +9"   (key + mode + pitch?)

26 00 00 00 03  -- 3 chars
  → "F#"           (key short form?)

26 00 00 00 07  -- 7 chars
  → "Trance"       (genre)
```
