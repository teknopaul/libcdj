# ProLink TCP Recon — Progress

| Phase | Description | Status |
|-------|-------------|--------|
| 1 | PCAP Static Analysis | complete |
| 2 | adj TCP Live Capture | complete |
| 3 | Protocol Stub in libcdj | complete |

## Review notes

### Phase 1 — PCAP Static Analysis

⚠ String encoding is UTF-16-BE on TCP, not LE as in .pdb files — never mix these

Files for review:
- `/home/teknopaul/bzr_workspace/libcdj/references/adj/doc/tcp-protocol-notes.md`
- `/home/teknopaul/bzr_workspace/libcdj/references/adj/tools/decode-tcp-pcap.sh`

### Phase 2 — adj TCP Live Capture

⚠ TCP port 1051 may conflict if another ProLink member is present — handle EADDRINUSE, log and continue

⚠ Capture XDJ-700 and XDJ-1000 separately before running both together — mixed sessions are harder to diff

⚠ Live test required before Phase 3 — do not write the libcdj parser stub until message framing is confirmed

Files for review:
- `/home/teknopaul/bzr_workspace/libcdj/references/adj/src/adj_tcp_log.c`
- `/home/teknopaul/bzr_workspace/libcdj/references/adj/src/adj_tcp_log.h`
- `/home/teknopaul/bzr_workspace/libcdj/references/adj/doc/tcp-protocol-notes.md`

### Phase 3 — Protocol Stub in libcdj

⚠ cdj_tcp_parse_message() is intentionally absent — add it only after message length field is confirmed by live capture

Files for review:
- `/home/teknopaul/bzr_workspace/libcdj/src/c/cdj_tcp.h`
- `/home/teknopaul/bzr_workspace/libcdj/src/c/cdj_tcp.c`
- `/home/teknopaul/bzr_workspace/libcdj/references/adj/doc/tcp-protocol-notes.md`
