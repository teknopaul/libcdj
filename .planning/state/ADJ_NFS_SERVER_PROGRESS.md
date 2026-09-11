# adj NFS Server — Progress

| Phase | Description | Status |
|-------|-------------|--------|
| 1 | Write adj_nfs.c + Makefile | complete |
| 2 | Wire into adj_vdj.c + CLI flag | complete |

## Review notes

### Phase 1 — Write adj_nfs.c + Makefile

⚠ The fake_pdb_generate track metadata is a placeholder — update title/bpm once we have real track data from the XDJ TCP stream

⚠ portmap_set failures are logged but non-fatal — NFS server still runs, XDJ just won't find it via portmapper

Files for review:
- `/home/teknopaul/bzr_workspace/libcdj/adj/src/adj_nfs.c`
- `/home/teknopaul/bzr_workspace/libcdj/adj/Makefile`

### Phase 2 — Wire into adj_vdj.c + CLI flag

⚠ adj_nfs_stop must be called before vdj_destroy — NFS poll thread must exit before the event loop closes

⚠ Test rpcinfo BEFORE connecting XDJs — once the XDJ probes portmapper and gets no response it may cache the failure

Files for review:
- `/home/teknopaul/bzr_workspace/libcdj/adj/src/adj_vdj.c`
- `/home/teknopaul/bzr_workspace/libcdj/adj/src/adj.c`
