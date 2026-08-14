# ext2/3/4 driver — roadmap and known gaps

Reference: the ext4 on-disk specification (kernel.org
`Documentation/filesystems/ext4/ondisk/`) and FreeBSD's
`sys/fs/ext2fs/`.  Substrate's driver: `sys/fs/ext2/` (ext2.c +
ext2_hash.c + ext2_xattr.c).

The 2026-08 audit against that spec (`docs/ext2-audit-2026-08.md`, 78
findings) is fully worked through; what follows is what the driver
does today and what it still does not do.

## Done

- Mount-time feature-flag gate — refuse mount on unknown `INCOMPAT`,
  ro-mount on unknown `ROCOMPAT`.  META_BG is deliberately NOT in the
  supported set (the layout is unimplemented; accepting it corrupted
  such images).  64BIT is accepted only when both the per-descriptor
  high halves and `s_blocks_count_hi` are zero.
- **ext4 extent trees** — read (multi-level, spec-correct `ee_len`
  decode including the 32768 maximum and uninitialized extents, which
  read as zeros), append-write, and **teardown** (recursive free, so
  `rm` and truncate-to-zero work on default ext4 images).
- **Lazy-init block groups** — `bg_flags` is modeled; BLOCK_UNINIT /
  INODE_UNINIT groups get a synthesized bitmap, the flag is cleared on
  first write, the inode table is zeroed before first use, and
  `bg_itable_unused` is maintained.
- **Checksums, read and write** — superblock, group descriptors
  (crc32c for metadata_csum, crc16 for the older gdt_csum), block and
  inode bitmaps, per-inode, and the `ext4_dir_entry_tail` on every
  directory block.  Volumes stay mountable by Linux and clean under
  e2fsck after substrate writes to them.
- **htree directory index** (read) — hash-routed lookup with the
  signed/unsigned variant taken from the superblock's `s_flags`, all
  six hash functions ported from FreeBSD, linear-scan fallback.
- **Extended attributes** (read) — inline and block-stored, POSIX ACL
  names, permission-gated at the syscall layer; the xattr block is
  refcount-released when an inode is deleted.
- 256-byte inodes: spec-correct `i_extra_isize` handling that never
  writes past the inode's own extension area (so inline xattrs
  survive), `s_want_extra_isize` honored for new inodes, full-record
  scrub on allocation.
- 32-bit uid/gid, both device-number encodings, `S_IFSOCK`, dirent
  `file_type` gated on INCOMPAT_FILETYPE, creator credentials with
  set-gid directory semantics, immutable/append inode flags, relatime
  atime, real `st_nlink`/`st_blocks`.
- Crash-ordering discipline: an inode is committed before any block it
  named is freed, so an interruption leaks blocks (fsck reclaims them)
  rather than cross-linking files.
- `s_state` management (mount marks the volume in use, unmount marks it
  clean, `s_errors` honored), `sync(2)`/`fsync(2)` flush deferred
  metadata through a VFS `syncfs` op, reserved-block enforcement.
- Journal replay refusal (`INCOMPAT_RECOVER`) — same as FreeBSD;
  neither driver replays JBD2 in-kernel.  Run `e2fsck -p` first.

## Known gaps

Each is a deliberate refusal or a bounded, documented shortfall — none
of them corrupts a filesystem.

- **htree write support.** Any structural change to a directory
  carrying `EXT2_INDEX_FL` is refused with `-EOPNOTSUPP`, because the
  index is not maintained.  Practical consequence: on a Linux-made
  image, large directories (`/usr/bin`, `/etc`) are effectively
  read-only — reads and lookups work fine.  Fixing this means
  implementing index insertion and node splitting.
- **htree multi-level** (`h_ind_levels > 0`) falls back to linear scan.
- **Extent-tree split / grow-in-depth.** The append path stops when the
  inline header fills; sparse and multi-level writes refuse cleanly.
- **Extent-file partial truncate.** Shrink-to-zero works (full
  teardown); shrink to a smaller non-zero length refuses
  `-EOPNOTSUPP` for extent-mapped files.  The legacy indirect path
  implements it.
- **xattr write side.** `{l,f}{set,remove}xattr` are `-ENOSYS`.
- **Orphan list** (`s_last_orphan`) is not processed at mount, so
  inodes orphaned by a Linux crash stay allocated until fsck.
- **Multi-mount protection** (`INCOMPAT_MMP`) unsupported — the bit is
  outside the supported set, so such volumes are refused.
- **META_BG** and **BIGALLOC** unsupported (refused at mount).
- **Read-side checksum tolerance.** Directory-tail, htree-node,
  extent-block-tail and xattr-block checksums are written correctly
  but not verified on read; a corrupt block is caught by its structural
  checks instead.  The xattr reader also accepts some layouts Linux
  rejects (unaligned `i_extra_isize`, overlapping value offsets).
- **Node references.** Driver-internal paths pin the nodes they hold
  across sleeping I/O, but `finddir` still returns an unpinned node to
  the VFS, which pins only at the end of a path walk.  Closing that
  fully means changing the VFS lookup protocol to carry a reference
  per component.
- **Root-pin watchdog.** `ext2_alloc_node` re-pins the root slot if it
  ever observes `pin_count == 0` and counts the event in
  `ext2_root_pin_lost`.  The unbalanced `close_fs` behind it has not
  been root-caused.
- **File size caps at 2 TiB.** Sizes are 64-bit now (i_size_high), but
  `i_blocks` counts 512-byte units in a uint32_t, so 2^32 sectors is the
  ceiling. Going further needs RO_COMPAT_HUGE_FILE's `l_i_blocks_high`
  plus the per-inode `EXT4_HUGE_FILE_FL` that switches `i_blocks` to
  filesystem-block units — which also means `st_blocks` is wrong today
  for any Linux-made file carrying that flag. The block map runs out
  earlier on small block sizes regardless: the triple-indirect chain
  reaches ~16 GiB at 1 KiB blocks, ~4 TiB at 4 KiB.
- 32-bit timestamps, so dates cap at 2038.

## Automated test harness

`tests/sys/fs/ext2/run-host-tests.sh` — runs on the developer host and
boots substrate in QEMU with a generated test image attached as a
second drive.  Each scenario writes `dist/etc/fstest.conf` with the
device/mount/fs triple plus a `check=` command; `etc/rc.d/99-fstest`
consumes it in the guest and emits `FSTEST: ...` lines the harness
greps for.  Scenarios that write also get a host-side `e2fsck -fn`
pass, which is what catches interoperability regressions.
