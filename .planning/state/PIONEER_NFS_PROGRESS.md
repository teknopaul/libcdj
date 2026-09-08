# Pioneer NFS — Progress

| Phase | Description                              | Status   |
|-------|------------------------------------------|----------|
| 1     | XDR/ONC-RPC Foundation                   | complete |
| 2     | Portmapper + Mount Client + UTF-16       | complete |
| 3     | NFS v2 Client                            | complete |
| 4     | XDJ Explorer Binary & Auth Investigation | complete |
| 5     | NFS v2 Server                            | complete |
| 6     | Fake PDB Exporter & Test Binary          | complete |
| 7     | Embedded NFS in Mock XDJ                 | complete |

## Review notes

### Phase 1 — XDR/ONC-RPC Foundation

⚠ XDR strings are NOT null-terminated in XDR — always track length separately

⚠ Pioneer paths are var_bytes (opaque), not XDR strings — correct type is crucial

Files for review:
- `/home/teknopaul/bzr_workspace/libcdj/src/nfs/xdr.h`
- `/home/teknopaul/bzr_workspace/libcdj/src/nfs/xdr.c`
- `/home/teknopaul/bzr_workspace/libcdj/src/nfs/rpc.h`
- `/home/teknopaul/bzr_workspace/libcdj/src/nfs/rpc.c`

### Phase 2 — Portmapper + Mount Client + UTF-16

⚠ Pioneer sends UTF-16LE paths WITHOUT BOM — don't add BOM prefix

⚠ mount_client must accept pre-encoded UTF-16LE paths from EXPORT response directly

Files for review:
- `/home/teknopaul/bzr_workspace/libcdj/src/nfs/nfs_types.h`
- `/home/teknopaul/bzr_workspace/libcdj/src/nfs/utf16.c`
- `/home/teknopaul/bzr_workspace/libcdj/src/nfs/mount_client.c`
- `/home/teknopaul/bzr_workspace/libcdj/src/nfs/portmap_client.c`

### Phase 3 — NFS v2 Client

⚠ NFS v2 READDIR: count field is max RESPONSE bytes, not max entries — be generous (8192)

⚠ Pioneer LOOKUP: each path component must be looked up separately from parent handle

Files for review:
- `/home/teknopaul/bzr_workspace/libcdj/src/nfs/nfs_client.h`
- `/home/teknopaul/bzr_workspace/libcdj/src/nfs/nfs_client.c`

### Phase 4 — XDJ Explorer Binary & Auth Investigation

⚠ NFSERR_ACCES: if not fixed by auto-IP, must run CDJ ProLink discovery on port 50000 first

⚠ Pioneer does NOT support multi-component paths in LOOKUP — must traverse one element at a time

⚠ Document exact portmapper ports from real hardware before Phase 5

Files for review:
- `/home/teknopaul/bzr_workspace/libcdj/src/bin/xdj_explore.c`

### Phase 5 — NFS v2 Server

⚠ UDP NFS: max UDP payload is 65507 bytes — ensure responses fit, especially READDIR

⚠ READDIR cookie: use entry index as cookie (simple, stateless) — not pointer/address

⚠ Must copy recvfrom address before calling ops, since ops might reuse the buffer

Files for review:
- `/home/teknopaul/bzr_workspace/libcdj/src/nfs/nfs_server.h`
- `/home/teknopaul/bzr_workspace/libcdj/src/nfs/nfs_server.c`
- `/home/teknopaul/bzr_workspace/libcdj/src/nfs/mount_server.h`
- `/home/teknopaul/bzr_workspace/libcdj/src/nfs/mount_server.c`

### Phase 6 — Fake PDB Exporter & Test Binary

⚠ export.pdb page size must be at least 4096 — smaller pages confuse crate-digger

⚠ DeviceSQL strings: flag byte 0x40 = long ASCII, body = 2-byte length + 1 pad + data

⚠ Verify generated PDB with crate-digger Java library independently before Phase 7

Files for review:
- `/home/teknopaul/bzr_workspace/libcdj/src/nfs/fake_pdb.c`
- `/home/teknopaul/bzr_workspace/libcdj/src/bin/nfs_server_test.c`

### Phase 7 — Embedded NFS in Mock XDJ

⚠ NFS port 2049 requires root — warn at startup if not root and port unavailable

⚠ Real XDJ may query specific portmapper port — test with tcpdump before assuming port

⚠ PDB regeneration on track update may add latency — consider double-buffering the pdb_data

Files for review:
- `/home/teknopaul/bzr_workspace/libcdj/src/c/vdj_nfs.h`
- `/home/teknopaul/bzr_workspace/libcdj/src/c/vdj_nfs.c`
