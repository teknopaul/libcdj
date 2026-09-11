# XDJ Discovery Fix — Progress

| Phase | Description | Status |
|-------|-------------|--------|
| 1 | Read and understand relevant code | complete |
| 2 | Fix sub-byte in CDJ_ID_USE_REQ (0x02) | complete |
| 3 | Respond to CDJ_ID_SET_REQ (0x04) with 0x05 | complete |
| 4 | Evaluate result and clean up | pending |

## Review notes

### Phase 1 — Read and understand relevant code

Files for review:
- `/home/teknopaul/bzr_workspace/libcdj/src/c/cdj.c:846`
- `/home/teknopaul/bzr_workspace/libcdj/src/c/cdj.c:902`
- `/home/teknopaul/bzr_workspace/libcdj/src/c/cdj.c:951`
- `/home/teknopaul/bzr_workspace/libcdj/src/c/vdj_discovery.c:310`
- `/home/teknopaul/bzr_workspace/libcdj/src/c/vdj_discovery.c:363`

### Phase 2 — Fix sub-byte in CDJ_ID_USE_REQ (0x02)

⚠ Test against physical XDJ-1000 before proceeding — capture pcap to confirm sub-byte

Files for review:
- `/home/teknopaul/bzr_workspace/libcdj/src/c/cdj.c:902`

### Phase 3 — Respond to CDJ_ID_SET_REQ (0x04) with 0x05

⚠ Capture pcap to confirm 0x05 reply appears after XDJ 0x04 broadcast

⚠ If XDJ is not yet in backline when its first 0x04 arrives, the reply will be silently skipped — check timing in pcap

Files for review:
- `/home/teknopaul/bzr_workspace/libcdj/src/c/cdj.c:951`
- `/home/teknopaul/bzr_workspace/libcdj/src/c/cdj.h:316`
- `/home/teknopaul/bzr_workspace/libcdj/src/c/vdj_discovery.c:363`
