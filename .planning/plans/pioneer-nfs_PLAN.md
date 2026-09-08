# Pioneer NFS Client & Server — Implementation Plan

## Background & Context

Pioneer XDJ/CDJ players export an **NFSv2 over UDP** server that crate-digger's `FileFetcher`
uses to retrieve `export.pdb` from rekordbox media. The protocol is standard RFC 1094 NFS v2 /
RFC 1094-Appendix-A Mount v1 **with one critical Pioneer quirk: all filenames and mount paths
are encoded as UTF-16LE** (not ASCII as the RFC specifies).

### What already exists in `src/x/`
- `xdr.c` / `nfs.h` / `mount.h` — rpcgen-generated XDR codec using **glibc's `rpc/rpc.h`**
- `vdj_list_exports.c` — lists exports successfully
- `vdj_nfs_explore.c` — gets `NFSERR_ACCES (13)` on MNT; likely because the calling machine
  was not on the 169.254.x.x auto-IP network and not participating in CDJ ProLink discovery
- `src/x/` code links against libnsl/glibc ONC RPC — **this plan replaces it with no-library C**

### Key facts from crate-digger analysis
- `FileFetcher.java` uses **AUTH_NONE** — no special credentials needed by the player
- Paths sent as `path.getBytes(StandardCharsets.UTF_16LE)` — **no BOM**, raw UTF-16LE bytes
- Read chunk size: 2048 bytes (avoid IP fragmentation)
- Portmapper on UDP 111; mountd on dynamic port (query portmapper); NFS on UDP 2049
- File structure: mount path `/C/` (USB) or `/B/` (SD); then `PIONEER/rekordbox/export.pdb`
- Access control is almost certainly network-based: caller must be on 169.254.x.x subnet and
  recognized as a CDJ ProLink member (ports 50000/50001 discovery must be active)

### What we don't know yet (gaps requiring real XDJ testing)
- Exact file handle encoding Pioneer uses (32 bytes, opaque — we'll observe it)
- Whether NFS access requires prior CDJ ProLink participation or just correct network range
- Exact portmapper behaviour (standard vs proprietary)
- Whether `exportExt.pdb` is present on XDJ-1000 and what Pioneer does with it
- Additional directories beyond `/PIONEER/rekordbox/` in the exported filesystem

---

## Target file structure

```
src/nfs/
  xdr.h / xdr.c           # Phase 1: XDR codec, pure C, no rpc/rpc.h
  rpc.h / rpc.c           # Phase 1: ONC-RPC message framing over UDP
  nfs_types.h             # Phase 2: struct defs (replaces nfs.h / mount.h from rpcgen)
  utf16.h / utf16.c       # Phase 2: UTF-16LE encode/decode without iconv
  portmap_client.c        # Phase 2: portmapper query
  mount_client.h / .c     # Phase 2: Mount v1 client
  nfs_client.h / .c       # Phase 3: NFS v2 client
  nfs_server.h / .c       # Phase 5: NFS v2 server
  mount_server.h / .c     # Phase 5: Mount v1 server
  fake_pdb.h / .c         # Phase 6: minimal export.pdb generator

src/bin/
  xdj_explore.c           # Phase 4: standalone XDJ NFS client tool
  nfs_server_test.c       # Phase 6: standalone fake-XDJ NFS server
```

The existing `src/x/` directory is kept as reference but is NOT linked into any build target.

---

## Phase 1 — XDR/ONC-RPC Foundation

**Goal:** A pure C implementation of XDR encoding/decoding and ONC-RPC UDP message
framing with no dependency on `rpc/rpc.h`, libnsl, or any library beyond POSIX sockets.

### ONC-RPC wire format (RFC 1831)

RPC call message (big-endian XDR):
```
xid          : uint32   — random transaction ID
msg_type     : uint32   — 0 = CALL
rpcvers      : uint32   — 2
prog         : uint32   — program number (100005 for mount, 100003 for nfs)
vers         : uint32   — version (1 for mount, 2 for nfs)
proc         : uint32   — procedure number
cred_flavor  : uint32   — 0 = AUTH_NONE
cred_len     : uint32   — 0
verf_flavor  : uint32   — 0 = AUTH_NONE
verf_len     : uint32   — 0
[procedure args follow]
```

RPC reply message:
```
xid          : uint32
msg_type     : uint32   — 1 = REPLY
reply_stat   : uint32   — 0 = MSG_ACCEPTED
verf_flavor  : uint32   — 0
verf_len     : uint32   — 0
accept_stat  : uint32   — 0 = SUCCESS
[procedure results follow]
```

XDR primitives (all big-endian, 4-byte aligned):
- `uint32`: 4 bytes
- `opaque[n]`: n bytes padded to 4-byte boundary with zeros
- `var_bytes(max)`: 4-byte length + bytes padded to 4-byte boundary
- `bool`: 0 or 1 as uint32
- `optional<T>`: uint32 discriminant (0=absent, 1=present) followed by T if present

### Files to create

**`src/nfs/xdr.h`** — XDR cursor type and encode/decode functions:
```c
typedef struct {
    uint8_t *buf;
    uint32_t pos;
    uint32_t len;
    int      encode;   /* 1 = encode (write), 0 = decode (read) */
} xdr_t;

int xdr_uint32(xdr_t *x, uint32_t *v);
int xdr_opaque(xdr_t *x, uint8_t *buf, uint32_t len);
int xdr_varbytes(xdr_t *x, uint8_t **buf, uint32_t *len, uint32_t maxlen);
int xdr_bool(xdr_t *x, int *v);
int xdr_optional(xdr_t *x, int *present, void *obj,
                 int (*encode_fn)(xdr_t *, void *));
```

**`src/nfs/rpc.h`** / **`src/nfs/rpc.c`** — UDP socket + RPC call:
```c
typedef struct {
    int       sock;           /* UDP socket fd */
    uint32_t  prog;
    uint32_t  vers;
    struct sockaddr_in server_addr;
    int       timeout_ms;     /* retransmit timeout */
} rpc_client_t;

int  rpc_client_init(rpc_client_t *c, const char *host, uint16_t port,
                     uint32_t prog, uint32_t vers, int timeout_ms);
void rpc_client_destroy(rpc_client_t *c);

/* Encode args via args_fn, send, receive, decode result via res_fn.
 * Returns 0 on success, -1 on transport error, >0 on RPC/accept error. */
int  rpc_call(rpc_client_t *c, uint32_t proc,
              int (*args_fn)(xdr_t *, void *), void *args,
              int (*res_fn)(xdr_t *, void *),  void *res);
```

`rpc_call` implementation:
1. Allocate 65535-byte send/recv buffers on the stack
2. Write RPC call header (xid = random, prog, vers, proc, AUTH_NONE both cred/verf)
3. Call `args_fn` to serialize args into the buffer
4. `sendto()` the buffer
5. `recvfrom()` with `select()`-based timeout and retry up to 3 times
6. Decode reply header, check xid match, check MSG_ACCEPTED / SUCCESS
7. Call `res_fn` to deserialize results

### Test criteria
- Can encode/decode all XDR primitive types round-trip
- `rpc_call` sends a UDP packet and receives a response on loopback
- Unit test: encode a uint32 array, decode it, compare

### Completion marker
```sh
geany-progress done 1 \
  -r src/nfs/xdr.h \
  -r src/nfs/xdr.c \
  -r src/nfs/rpc.h \
  -r src/nfs/rpc.c \
  -w "XDR strings are NOT null-terminated in XDR — always track length separately" \
  -w "Pioneer paths are var_bytes (opaque), not XDR strings — correct type is crucial"
```

---

## Phase 2 — Portmapper + Mount Client + UTF-16LE

**Goal:** Query the portmapper on port 111 to find the mountd port, then issue
Mount v1 EXPORT and MNT calls. Implement UTF-16LE encoding without iconv.

### Portmapper (RFC 1833 / program 100000 version 2)

Only one procedure needed:
```
PMAPPROC_GETPORT (proc=3):
  args: { prog: uint32, vers: uint32, prot: uint32 (17=UDP), port: uint32 }
  result: uint32 (port number, 0 = not registered)
```

### Mount protocol types (RFC 1094 Appendix A)

```c
/* src/nfs/nfs_types.h */
#define FHSIZE      32
#define MNTPATHLEN  1024
#define MNTNAMLEN   255
#define MAXNAMLEN   255
#define NFSMAXPATHLEN 1024
#define MAXDATA     8192
#define COOKIESIZE  4

typedef struct { uint8_t data[FHSIZE]; } fhandle_t;

typedef struct {            /* variable-length opaque bytes (UTF-16LE path) */
    uint8_t  *val;
    uint32_t  len;
} dirpath_t;

typedef struct {
    uint32_t  status;       /* 0 = OK, errno otherwise */
    fhandle_t directory;    /* valid when status == 0 */
} fhstatus_t;

typedef struct export_entry {
    dirpath_t           filesystem;
    struct export_entry *next;
} export_entry_t;
```

XDR codec functions for each type — `xdr_fhandle`, `xdr_dirpath`, `xdr_fhstatus`, etc.

### Mount procedures

```c
/* src/nfs/mount_client.h */
int mount_getport(const char *host, uint16_t *port_out);
int mount_export(rpc_client_t *c, export_entry_t **list_out);
int mount_mnt(rpc_client_t *c, dirpath_t *path, fhstatus_t *result);
void mount_export_free(export_entry_t *list);
```

### UTF-16LE without iconv (`src/nfs/utf16.h` / `src/nfs/utf16.c`)

Pioneer uses UTF-16LE with no BOM. File paths only contain ASCII characters
(`/`, `A-Z`, `a-z`, `0-9`, `.`, `_`), so the encoding is trivial:

```c
/* ASCII → UTF-16LE: each byte becomes two bytes (low byte = char, high byte = 0x00) */
int utf8_to_utf16le(const char *src, uint8_t *dst, uint32_t dst_max, uint32_t *len_out);

/* UTF-16LE → ASCII (safe for Pioneer paths which are all ASCII-range) */
int utf16le_to_utf8(const uint8_t *src, uint32_t src_len, char *dst, uint32_t dst_max);
```

No iconv dependency needed — Pioneer paths are always pure ASCII characters in UTF-16LE.

### Test criteria
- Query a real (or mock) portmapper and get back a port number
- Round-trip dirpath encode/decode with a UTF-16LE path

### Completion marker
```sh
geany-progress done 2 \
  -r src/nfs/nfs_types.h \
  -r src/nfs/utf16.c \
  -r src/nfs/mount_client.c \
  -r src/nfs/portmap_client.c \
  -w "Pioneer sends UTF-16LE paths WITHOUT BOM — don't add BOM prefix" \
  -w "mount_client must accept pre-encoded UTF-16LE paths from EXPORT response directly"
```

---

## Phase 3 — NFS v2 Client

**Goal:** Implement the NFS procedures needed to read files from an XDJ: NULL, GETATTR,
LOOKUP, READDIR, READ, STATFS.

Write/mutating procedures (CREATE, REMOVE, MKDIR, etc.) return `NFSERR_ROFS` when
implemented as server — not needed in the client.

### NFS types (extend `nfs_types.h`)

```c
/* File attributes — from nfs.x FAttr */
typedef struct {
    uint32_t type;       /* FType: 1=reg, 2=dir, 3=blk, 4=chr, 5=lnk */
    uint32_t mode;
    uint32_t nlink;
    uint32_t uid;
    uint32_t gid;
    uint32_t size;
    uint32_t blocksize;
    uint32_t rdev;
    uint32_t blocks;
    uint32_t fsid;
    uint32_t fileid;
    uint32_t atime_sec;  uint32_t atime_usec;
    uint32_t mtime_sec;  uint32_t mtime_usec;
    uint32_t ctime_sec;  uint32_t ctime_usec;
} fattr_t;

typedef struct {
    uint8_t  *val;       /* UTF-16LE bytes */
    uint32_t  len;
} filename_t;

typedef struct {
    fhandle_t dir;
    filename_t name;
} diropargs_t;

typedef struct {
    uint32_t  status;
    fhandle_t file;
    fattr_t   attributes;
} diropres_t;

typedef struct {
    fhandle_t file;
    uint32_t  offset;
    uint32_t  count;
    uint32_t  totalcount;
} readargs_t;

typedef struct {
    uint32_t  status;
    fattr_t   attributes;
    uint8_t  *data;
    uint32_t  data_len;
} readres_t;

typedef struct {
    uint32_t  fileid;
    filename_t name;
    uint8_t   cookie[COOKIESIZE];
} dir_entry_t;

typedef struct {
    uint32_t     status;
    dir_entry_t *entries;   /* NULL-terminated linked list */
    int          eof;
} readdirres_t;
```

### NFS client procedures

```c
/* src/nfs/nfs_client.h */
int nfs_getport(const char *host, uint16_t *port_out);

int nfs_null(rpc_client_t *c);
int nfs_getattr(rpc_client_t *c, fhandle_t *fh, fattr_t *attrs_out);
int nfs_lookup(rpc_client_t *c, diropargs_t *args, diropres_t *res_out);
int nfs_readdir(rpc_client_t *c, fhandle_t *dir,
                uint8_t cookie[COOKIESIZE], uint32_t count,
                readdirres_t *res_out);
int nfs_read(rpc_client_t *c, readargs_t *args, readres_t *res_out);
int nfs_statfs(rpc_client_t *c, fhandle_t *fh, uint32_t *tsize_out);

void nfs_readdirres_free(readdirres_t *res);
void nfs_readres_free(readres_t *res);
```

### Pioneer LOOKUP path strategy
Pioneer cannot handle multi-component paths in a single LOOKUP. Each path element
must be looked up individually from the parent handle, exactly as `FileFetcher.java` does:

```c
/* Walk path components one at a time from root handle */
int nfs_path_lookup(rpc_client_t *c, fhandle_t *root,
                    const char *path,  /* ASCII, e.g. "PIONEER/rekordbox/export.pdb" */
                    fhandle_t *fh_out, fattr_t *attr_out);
```

### Test criteria
- `nfs_null` succeeds against localhost loopback test server
- `nfs_lookup` correctly encodes a UTF-16LE filename for a single path element

### Completion marker
```sh
geany-progress done 3 \
  -r src/nfs/nfs_client.h \
  -r src/nfs/nfs_client.c \
  -w "NFS v2 READDIR: count field is max RESPONSE bytes, not max entries — be generous (8192)" \
  -w "Pioneer LOOKUP: each path component must be looked up separately from parent handle"
```

---

## Phase 4 — XDJ Explorer Binary & Auth Investigation

**Goal:** A standalone `target/xdj-explore` binary that connects to a real XDJ (given IP),
works around the NFSERR_ACCES issue, and fetches `export.pdb`.

### Auth investigation

The existing `src/x/vdj_nfs_explore.c` gets NFSERR_ACCES. Likely causes:
1. **Network range**: XDJ only serves devices on 169.254.x.x. Must have auto-IP configured.
2. **CDJ ProLink participation**: XDJ may check if the caller has announced on ports 50000/50001.
3. **AUTH_UNIX with uid=0**: Some NFS servers require AUTH_UNIX. Try this as a fallback.

AUTH_UNIX credential format:
```
cred_flavor : uint32 = 1 (AUTH_UNIX)
cred_len    : uint32 = total bytes of credential body
  stamp     : uint32 (arbitrary)
  machinename: string<255>
  uid       : uint32 = 0
  gid       : uint32 = 0
  gids_len  : uint32 = 0
```

Add `rpc_call_unix_auth()` variant to `rpc.c`.

### `src/bin/xdj_explore.c` — standalone explorer

```
Usage: xdj-explore <ip> [--fetch-pdb <output.pdb>] [--verbose]

Steps:
1. Query portmapper (UDP 111) for mountd port
2. Query portmapper for NFS port
3. MOUNTPROC_EXPORT → print export list with decoded UTF-16LE paths
4. For each export: MOUNTPROC_MNT → get root handle
5. NFSPROC_READDIR → list root directory
6. NFSPROC_LOOKUP "PIONEER" then "rekordbox" then "export.pdb"
7. NFSPROC_GETATTR → print file size
8. [--fetch-pdb] NFSPROC_READ loop → save to local file
9. Print timing statistics
```

### Build target (add to Makefile)

```make
target/xdj-explore: src/nfs/xdr.o src/nfs/rpc.o src/nfs/utf16.o \
                    src/nfs/mount_client.o src/nfs/nfs_client.o \
                    src/bin/xdj_explore.o
	$(CC) $(CFLAGS) -o $@ $^
```

### Test plan (requires real XDJ hardware)
1. Configure auto-IP: `sudo ip addr add 169.254.x.x/16 dev <iface>` or use `avahi-autoipd`
2. Run `target/xdj-explore 169.254.x.x --verbose`
3. If NFSERR_ACCES: enable CDJ ProLink discovery via `vdj` before running the explorer
4. Document exact wire captures using `tcpdump -i any udp port 111 or port 2049 or port 48276`
5. Success: prints directory listing and saves `export.pdb`

### Expected directory structure on XDJ (based on crate-digger docs)
```
/C/
  PIONEER/
    rekordbox/
      export.pdb
      exportExt.pdb   (maybe)
    USBANLZ/          (track analysis files, .DAT / .EXT)
      0001/
        00000001/
          ANLZXXXX.DAT
          ANLZXXXX.EXT
```

### Completion marker
```sh
geany-progress done 4 \
  -r src/bin/xdj_explore.c \
  -w "NFSERR_ACCES: if not fixed by auto-IP, must run CDJ ProLink discovery on port 50000 first" \
  -w "Pioneer does NOT support multi-component paths in LOOKUP — must traverse one element at a time" \
  -w "Document exact portmapper ports from real hardware before Phase 5"
```

---

## Phase 5 — NFS v2 Server

**Goal:** A pure C NFSv2 + Mount v1 server over UDP with a virtual filesystem backed by
callbacks. Read-only. Can be bound to port 2049 (requires root) or a custom port.

### Server architecture

Virtual filesystem via function pointer table:

```c
/* src/nfs/nfs_server.h */

typedef struct {
    /* Called on MOUNTPROC_EXPORT — fill exports list */
    int (*list_exports)(void *ctx, export_entry_t **list_out);

    /* Called on MOUNTPROC_MNT — validate path, return root file handle */
    int (*mount)(void *ctx, dirpath_t *path, fhandle_t *fh_out);

    /* Called on NFSPROC_GETATTR */
    int (*getattr)(void *ctx, fhandle_t *fh, fattr_t *attrs_out);

    /* Called on NFSPROC_LOOKUP — name is UTF-16LE */
    int (*lookup)(void *ctx, fhandle_t *dir, filename_t *name,
                  fhandle_t *fh_out, fattr_t *attrs_out);

    /* Called on NFSPROC_READDIR */
    int (*readdir)(void *ctx, fhandle_t *dir, uint32_t cookie,
                   uint32_t max_bytes, dir_entry_t **entries_out, int *eof_out);

    /* Called on NFSPROC_READ */
    int (*read)(void *ctx, fhandle_t *fh, uint32_t offset, uint32_t count,
                uint8_t *buf_out, uint32_t *bytes_out, fattr_t *attrs_out);

    /* Called on NFSPROC_STATFS */
    int (*statfs)(void *ctx, fhandle_t *fh, uint32_t *tsize_out,
                  uint32_t *bsize_out, uint32_t *blocks_out,
                  uint32_t *bfree_out, uint32_t *bavail_out);
} nfs_ops_t;

typedef struct {
    int         mount_fd;   /* UDP socket for mount protocol */
    int         nfs_fd;     /* UDP socket for NFS protocol */
    uint16_t    mount_port;
    uint16_t    nfs_port;
    nfs_ops_t  *ops;
    void       *ctx;
    int         running;
} nfs_server_t;

int  nfs_server_init(nfs_server_t *s, uint16_t mount_port, uint16_t nfs_port,
                     nfs_ops_t *ops, void *ctx);
int  nfs_server_poll(nfs_server_t *s, int timeout_ms);  /* non-blocking single iteration */
void nfs_server_destroy(nfs_server_t *s);
```

### Request dispatch loop

`nfs_server_poll` calls `recvfrom` on both sockets with `select()`, then:
1. Decodes the RPC call header from the datagram
2. Dispatches to the correct procedure handler based on (prog, vers, proc)
3. Calls the appropriate `ops` function pointer
4. Encodes the result as an RPC reply
5. Sends the reply to the caller's address

Error handling: if a procedure is not implemented, reply with `PROC_UNAVAIL`.
Write procedures (CREATE, WRITE, etc.) reply with `NFSERR_ROFS`.

### File handle design

For the fake server, encode file handles as 32 bytes with a simple inode-like scheme:
```
[0-3]  : magic number (0xCDCD0001)
[4-7]  : filesystem ID
[8-11] : inode number (32-bit)
[12-31]: padding zeros
```

### Portmapper registration

The standard portmapper (`rpcbind`) on port 111 maintains the service registry. To register:
```
PMAPPROC_SET (proc=1):
  args: { prog, vers, prot (17=UDP), port }
  result: bool (success)
```

If portmapper is not available (or we don't want root), serve on a fixed port and document it.
In the embedded (mock XDJ) use case, the vdj will also serve portmapper, or XDJs will use
a hardcoded port.

### Thread model

`nfs_server_poll` is single-threaded and non-blocking. Caller controls the event loop.
For the test binary, a simple `while (running) nfs_server_poll(&s, 100)` loop suffices.
For embedding in `vdj`, it becomes one more fd in the existing pselect loop.

### Test criteria
- Mount a loopback server from another process and receive `NFS_OK`
- READDIR returns at least one entry
- READ returns data for a known file handle

### Completion marker
```sh
geany-progress done 5 \
  -r src/nfs/nfs_server.h \
  -r src/nfs/nfs_server.c \
  -r src/nfs/mount_server.h \
  -r src/nfs/mount_server.c \
  -w "UDP NFS: max UDP payload is 65507 bytes — ensure responses fit, especially READDIR" \
  -w "READDIR cookie: use entry index as cookie (simple, stateless) — not pointer/address" \
  -w "Must copy recvfrom address before calling ops, since ops might reuse the buffer"
```

---

## Phase 6 — Fake PDB Exporter & Test Binary

**Goal:** Generate a minimal `export.pdb` in memory with one fake track "always playing",
serve it via the NFS server, and build `target/nfs-server-test` as a standalone test.

### Fake PDB structure

From the crate-digger `exports.adoc` analysis, a minimal `export.pdb` needs:
- File header (at offset 0): `len_page`, `num_tables`, table pointers
- At minimum: one **tracks** table (type 0x00) with one track row
- Track row must have: `title`, `artist_id=0`, `album_id=0`, `id=1`, `file_path`, `filename`
- DeviceSQL strings: short ASCII for simple names

A truly minimal DB can be a single 4096-byte page containing:
- File header at page 0
- Tracks table page with one track row pointing to static strings

```c
/* src/nfs/fake_pdb.h */

typedef struct {
    const char *title;
    const char *filename;
    const char *file_path;
    uint32_t    duration_secs;
    uint32_t    bpm_x100;      /* e.g. 12800 = 128.00 BPM */
} fake_track_t;

/* Returns malloc'd buffer containing a valid export.pdb, sets *len_out */
uint8_t *fake_pdb_generate(const fake_track_t *track, uint32_t *len_out);
```

### Pioneer-style virtual filesystem layout

The NFS server's `ops` will expose:
```
/                     (root, inode 1, NFDIR)
  PIONEER/            (inode 2, NFDIR)
    rekordbox/        (inode 3, NFDIR)
      export.pdb      (inode 4, NFREG, contents = fake_pdb_generate())
```

File handles: simple inode scheme as defined in Phase 5.

All directory entries encoded as UTF-16LE filenames (Pioneer convention).

### Standalone test binary (`src/bin/nfs_server_test.c`)

```
Usage: nfs-server-test [--mount-port PORT] [--nfs-port PORT] [--title TITLE]

Starts the NFS server and serves a single fake export.pdb.
Another terminal can verify with:
  target/xdj-explore 127.0.0.1 --fetch-pdb out.pdb
```

Build target:
```make
target/nfs-server-test: src/nfs/xdr.o src/nfs/rpc.o src/nfs/utf16.o \
                        src/nfs/mount_client.o src/nfs/mount_server.o \
                        src/nfs/nfs_client.o src/nfs/nfs_server.o \
                        src/nfs/fake_pdb.o src/bin/nfs_server_test.o
	$(CC) $(CFLAGS) -o $@ $^
```

### Self-test

Add to `Makefile` test target:
```sh
target/nfs-server-test --mount-port 7004 --nfs-port 7005 &
sleep 1
target/xdj-explore 127.0.0.1 --mount-port 7004 --nfs-port 7005 --fetch-pdb /tmp/test.pdb
kill %1
# verify /tmp/test.pdb is a valid PDB
```

### Completion marker
```sh
geany-progress done 6 \
  -r src/nfs/fake_pdb.c \
  -r src/bin/nfs_server_test.c \
  -w "export.pdb page size must be at least 4096 — smaller pages confuse crate-digger" \
  -w "DeviceSQL strings: flag byte 0x40 = long ASCII, body = 2-byte length + 1 pad + data" \
  -w "Verify generated PDB with crate-digger Java library independently before Phase 7"
```

---

## Phase 7 — Embedded NFS in Mock XDJ (vdj Integration)

**Goal:** Integrate the NFS server into `vdj_t` so that a mock XDJ (`target/vdj`) also
exports an NFS server that real XDJs can query.

### API extension to `vdj.h`

```c
/* src/c/vdj_nfs.h */
#include "nfs_server.h"

typedef struct {
    nfs_server_t    server;
    fake_track_t    track;   /* current "playing" track, updated by vdj state */
    uint8_t        *pdb_data;
    uint32_t        pdb_len;
} vdj_nfs_t;

int  vdj_nfs_init(vdj_t *v, uint16_t mount_port, uint16_t nfs_port);
int  vdj_nfs_poll(vdj_t *v, int timeout_ms);
void vdj_nfs_update_track(vdj_t *v, const fake_track_t *track);
void vdj_nfs_destroy(vdj_t *v);
```

### Integration with `vdj_pselect`

The NFS server's two sockets (mount_fd and nfs_fd) are added to `vdj_t`'s
pselect fd set. `vdj_nfs_poll` is called when either socket is readable.

No new threads required — the existing single-threaded pselect loop handles it.

### Port selection

XDJs expect NFS on port 2049 (requires root) and mountd on the port reported by portmapper.
Options:
1. Use portmapper registration with custom ports (e.g. 2049/4749 for root, or 7003/7004 for testing)
2. When running as root, bind to standard ports 2049 and a portmapper-registered mount port
3. Add `--nfs-port` and `--mount-port` flags to `target/vdj`

### Portmapper integration

For a complete Pioneer mock, `vdj` should also respond to portmapper queries.
Add a third UDP socket on port 111 (or proxy through system portmapper via `PMAPPROC_SET`).

Pioneer XDJs will query portmapper to find the mount port. If we register with the
system portmapper via `rpc_call(PMAP, PMAPPROC_SET, ...)`, no portmapper code needed on our side.

### Track state update

When `vdj`'s play state changes (e.g. BPM, track loaded), call `vdj_nfs_update_track()`:
- Regenerate the in-memory `export.pdb` with updated track info
- Update `fake_track_t` — title can encode BPM, current bar position, etc.

### Test with real XDJ hardware

1. Run `sudo target/vdj --nfs-port 2049 --mount-port <portmapper-registered>`
2. On a real XDJ, browse USB media — it should see our virtual track
3. Use Wireshark/tcpdump to verify the NFS traffic matches real XDJ-to-XDJ traffic

### Completion marker
```sh
geany-progress done 7 \
  -r src/c/vdj_nfs.h \
  -r src/c/vdj_nfs.c \
  -w "NFS port 2049 requires root — warn at startup if not root and port unavailable" \
  -w "Real XDJ may query specific portmapper port — test with tcpdump before assuming port" \
  -w "PDB regeneration on track update may add latency — consider double-buffering the pdb_data"
```

---

## Open questions (to answer with real hardware)

| # | Question                                        | How to answer                                                            |
|---|-------------------------------------------------|--------------------------------------------------------------------------|
| 1 | Why does `mount_mnt` return NFSERR_ACCES?       | tcpdump on XDJ, try AUTH_UNIX uid=0, try after CDJ discovery             |
| 2 | Does XDJ check if caller is a CDJ on ProLink?   | Run `vdj` (ProLink participant) then run `xdj-explore` from same process |
| 3 | What are the exact portmapper-reported ports?   | `rpcinfo <xdj-ip>` from auto-IP network                                  |
| 4 | Does XDJ NFS serve port 2049 or a dynamic port? | See portmapper query result                                              |
| 5 | Is `exportExt.pdb` present on XDJ-1000?         | List directory after Phase 4                                             |
| 6 | What does the XDJ file handle look like?        | Capture MNT response, inspect 32 bytes                                   |
| 7 | Does real XDJ browse our fake PDB correctly?    | Phase 7 test                                                             |

---

## Dependencies between phases

```
Phase 1 (XDR/RPC)
  └─ Phase 2 (Mount client + UTF-16)
       └─ Phase 3 (NFS client)
            └─ Phase 4 (xdj-explore binary)   ← requires real XDJ hardware
       └─ Phase 5 (NFS server)
            └─ Phase 6 (fake PDB + test server)
                 └─ Phase 7 (vdj integration)  ← requires real XDJ hardware
```

Phases 1–3 and 5–6 can be built and tested on localhost.
Phases 4 and 7 require a Pioneer XDJ or CDJ on the network.

---

## Files NOT to change

- `src/x/` — existing rpcgen output, keep as reference only
- `src/c/` — existing libcdj/libvdj source, modified only in Phase 7
- `Makefile` — add new targets in each phase, never remove existing ones
