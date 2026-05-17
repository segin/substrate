# ext2/3/4 driver — FreeBSD-parity roadmap

Reference: `~/freebsd/sys/fs/ext2fs/` (~16k LOC across 14 .c files).
Substrate's current driver: `sys/fs/ext2/ext2.c` (~2.9k LOC).

The goal is feature-complete parity with FreeBSD's `ext2fs(5)` so we
can mount any default-format ext2/ext3/ext4 image read-write without
data loss.  This is a multi-week effort; the punch list below tracks
what's done and what's next.

## Done

- [x] Mount-time feature-flag gate — refuse mount on unknown
      `INCOMPAT`, ro-mount on unknown `ROCOMPAT`.  Per-bit defines
      and `EXT2F_*_SUPP` masks match FreeBSD's ext2fs.h naming.
- [x] **ext4 extent-tree resolver** — when an inode has
      `EXT4_EXTENTS_FL` set, `ext2_get_block_num` walks the inline
      header → index nodes → leaf extents and returns the physical
      block.  Multi-level (depth ≤ EXT4_EXT_DEPTH_MAX) supported.
      Sparse-region holes return 0 like the legacy block-pointer
      path.  16 TiB cap due to substrate uint32_t block numbers.
- [x] `chmod`, `chown`, `utimes/utimensat/futimens` persist to the
      on-disk inode through the new `setattr_fs` op.
- [x] **256-byte inode + nanosecond timestamps** —
      `EXT2F_ROCOMPAT_EXTRA_ISIZE`.  read_inode loads the extra
      region (i_extra_isize, i_*time_extra, i_crtime,
      i_crtime_extra); write_inode preserves the trailing xattr
      area via read-modify-write.  fs_attr grows `*_nsec` fields
      and utimensat() forwards `tv_nsec`.
- [x] **Metadata checksums** (`EXT2F_ROCOMPAT_METADATA_CKSUM`):
      software CRC-32C (Castagnoli) primitive at sys/kern/crc32c.c.
      Mount-time verify of (a) the superblock, (b) every group
      descriptor.  read_inode verifies each inode's csum; write_inode
      recomputes chksum_lo / chksum_hi after every write so
      setattr/chmod/utimes don't leave the inode csum-broken.
- [x] **htree directory index** (`EXT2F_COMPAT_DIRHASHINDEX`) — finddir
      hash-routes to the indexed leaf for single-level dirs.  Hash
      functions ported verbatim from FreeBSD (legacy, half_md4, tea,
      signed and unsigned variants).  Hash seed loaded from
      superblock at mount.  Falls back to linear scan for multi-
      level indexes or unknown hash versions.

## In progress / next

- [ ] **htree multi-level (`h_ind_levels` > 0)** — currently we
      fall back to linear scan.  Rare in practice (>~3M entries
      per dir) but a proper FreeBSD parity needs index nodes.
- [ ] **Inode write path for extent files** — currently
      `ext2_inode_write` walks the legacy pointer array and we
      refuse with EROFS on extent files.  Need an
      `ext4_extent_alloc` / `ext4_extent_split` pair to grow
      extents on write.
- [ ] **Block-bitmap walker for extent files** —
      `ext2_free_blocks` likewise needs an extent-aware variant
      so truncate/unlink frees the underlying physical blocks.
- [ ] **Extended attributes** (`EXT2F_COMPAT_EXT_ATTR`) — substrate
      has no xattr surface yet.  Port `ext2_extattr.c` + add
      `sys_getxattr`/`setxattr`/`listxattr`/`removexattr`.  Read
      the post-128 in-inode block plus the block at `i_file_acl`.
- [ ] **Bitmap + extent-tail checksums** — extends metadata_csum
      to block bitmaps (stored in bg descriptor), inode bitmaps
      (likewise), and the per-extent-block ext4_extent_tail at
      the end of each extent index/leaf block.
- [ ] **Journal replay** (`EXT2F_INCOMPAT_RECOVER`) — refuse mount
      of dirty ext3/4 unless we can replay.  Even a read-only
      replay (parse journal, apply committed transactions to an
      in-memory shadow) would let us mount dirty filesystems.
- [ ] **64-bit block addresses** (`EXT2F_INCOMPAT_64BIT`) — bump
      substrate's `daddr_t` analog to 64-bit through the driver.
      Required for any modern Linux-generated ext4 > 16 TiB.
- [ ] **htree creation on rename/insert** — once the writeable
      side is in, the directory-write path needs to update the
      htree when entries are added/removed.
- [ ] **Multi-mount protection** (`EXT2F_INCOMPAT_MMP`) — block
      that detects concurrent mounts via shared storage.  Low
      priority; substrate doesn't ship a multi-node deployment.

## Automated test harness

Lives at `tests/sys/fs/ext2/run-host-tests.sh` — invoked
on-developer-host and boots substrate in QEMU with a generated
test image attached as the second AHCI drive.  Each scenario
prep_rootfs's `dist/etc/fstest.conf` with the device/mount/fs
triple plus a `check=` command.  `etc/rc.d/99-fstest` consumes
the config in the guest and emits `FSTEST: ...` lines the harness
greps for.

Current scenarios (41 assertions, all passing):
- `mount-ext2`, `mount-ext3`, `mount-ext4-extents` — feature-bit
  routing and basic mount sanity per filesystem rev.
- `htree-listing` + finddir spread-probe — readdir traverses all
  500 entries of an `e2fsck -fD`-promoted htree dir; finddir
  resolves 18 representative names across the hash range.
- `setattr-persist` + `setattr-persist-on-metadata-csum` —
  utimensat persists across umount/remount, including on
  metadata_csum'd images (proves the per-inode csum recompute).
- `ro-mount-on-unsupported-rocompat` — quota ROCOMPAT triggers
  read-only mount; touch returns EROFS.
- `extent-large-read` — 1 MiB ext4 file backed by a depth-1
  extent tree, content + size both match end-to-end.
- `csum-verify-ok` + `csum-verify-fail` — superblock metadata_csum
  validates clean images and refuses tampered ones.
- `bg-csum-verify` — same shape for per-group-descriptor csums.
- `refuse-ext4-extent-write` — extent files are read-only at the
  EROFS layer; writing produces no change in file size.
- `refuse-ext4-64bit` — INCOMPAT_64BIT triggers clean refusal.
