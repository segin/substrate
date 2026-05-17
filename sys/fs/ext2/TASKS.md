# ext2/3/4 driver — FreeBSD-parity roadmap

Reference: `~/freebsd/sys/fs/ext2fs/` (~16k LOC across 14 .c files).
Substrate's current driver: `sys/fs/ext2/` (ext2.c + ext2_hash.c +
ext2_xattr.c, ~3.2k LOC total).

The goal was feature parity with FreeBSD's `ext2fs(5)` so we can
mount any default-format ext2/ext3/ext4 image read-write without
data loss.  The major-feature work is done; what remains is mostly
edge-case enrichment and write-path completeness.

## Done

- [x] Mount-time feature-flag gate — refuse mount on unknown
      `INCOMPAT`, ro-mount on unknown `ROCOMPAT`.  Per-bit defines
      and `EXT2F_*_SUPP` masks match FreeBSD's ext2fs.h naming.
- [x] **64-bit block addresses** (`EXT2F_INCOMPAT_64BIT`) —
      reads 64-byte group descriptors; rejects the mount if any
      bg's high-half block address is non-zero (substrate's
      internal addressing stays uint32_t).  Matches FreeBSD's
      EXT2F_INCOMPAT_SUPP set.
- [x] **ext4 extent-tree resolver** (read) — when an inode has
      `EXT4_EXTENTS_FL` set, `ext2_get_block_num` walks the inline
      header → index nodes → leaf extents and returns the physical
      block.  Multi-level (depth ≤ EXT4_EXT_DEPTH_MAX) supported.
- [x] **ext4 extent-tree append-write** — `ext4_extent_alloc_inode_block`
      handles empty-list / contiguous-extend / new-single-extent
      cases for depth-0 inline headers.  Sparse / multi-level /
      header-full cases refuse cleanly with -EROFS; freed blocks
      are released on partial-failure.
- [x] `chmod`, `chown`, `utimes/utimensat/futimens` persist to the
      on-disk inode through the new `setattr_fs` op.
- [x] **256-byte inode + nanosecond timestamps** —
      `EXT2F_ROCOMPAT_EXTRA_ISIZE`.  read_inode loads the extra
      region (i_extra_isize, i_*time_extra, i_crtime,
      i_crtime_extra); write_inode preserves the trailing xattr
      area via read-modify-write.  fs_attr carries `*_nsec` fields
      and utimensat() forwards `tv_nsec` end-to-end.
- [x] **Metadata checksums** (`EXT2F_ROCOMPAT_METADATA_CKSUM`):
      software CRC-32C (Castagnoli) primitive at sys/kern/crc32c.c.
      Mount-time verify of (a) the superblock, (b) every group
      descriptor (32-byte and 64-byte layouts).  read_inode verifies
      each inode's csum; write_inode recomputes chksum_lo / chksum_hi
      after every write so setattr/chmod/utimes don't leave the
      inode csum-broken.
- [x] **htree directory index** (`EXT2F_COMPAT_DIRHASHINDEX`) —
      finddir hash-routes to the indexed leaf for single-level dirs.
      Hash functions ported verbatim from FreeBSD (legacy, half_md4,
      tea, signed and unsigned variants).  Hash seed loaded from
      superblock at mount.  Falls back to linear scan for multi-
      level indexes or unknown hash versions.
- [x] **Extended attributes** (`EXT2F_COMPAT_EXT_ATTR`) — read-side
      complete.  Inline (in-inode) xattr area + block-stored xattr
      at i_file_acl, with inline winning when both define a name.
      12 Linux-shape syscalls plumbed: {l,f}{get,list}xattr
      implemented; {l,f}{set,remove}xattr stub to -ENOSYS pending
      a backend write path.
- [x] **Journal replay refusal** (`EXT2F_INCOMPAT_RECOVER`) —
      same behaviour as FreeBSD: refuse the mount.  Neither
      driver replays the JBD2 journal in-kernel; users must run
      e2fsck -p first.  Parity is by-design refusal.

## In progress / next

- [ ] **htree multi-level (`h_ind_levels` > 0)** — currently we
      fall back to linear scan.  Rare in practice (>~3M entries
      per dir) but FreeBSD does walk index nodes.
- [ ] **Extent-tree split / grow-indepth** — substrate's append
      path stops when the inline header fills.  FreeBSD's
      ext4_ext_create_new_leaf / ext4_ext_grow_indepth handle
      promoting the tree.
- [ ] **Extent-tree remove / truncate** — currently truncate of
      extent files goes through the legacy path (and is largely a
      no-op because i_block[] doesn't hold pointers).  Need
      ext4_ext_remove_space to free leaf blocks.
- [ ] **xattr write side** — sys_{l,f}{set,remove}xattr stubbed at
      -ENOSYS.  Need to grow the inline area / block, recompute the
      block's stored hash, and (if metadata_csum) refresh the block
      csum.
- [ ] **Bitmap + extent-tail checksums** — metadata_csum extends
      to block bitmaps (csum in bg descriptor), inode bitmaps,
      and the per-extent-block ext4_extent_tail at the end of each
      extent index/leaf block.  We don't verify those today.
- [ ] **htree creation on rename/insert** — once the directory-
      write path is xattr-aware, it also needs to update or
      create the htree.
- [ ] **Multi-mount protection** (`EXT2F_INCOMPAT_MMP`) — block
      that detects concurrent mounts via shared storage.  Low
      priority; substrate doesn't ship a multi-node deployment.

## Automated test harness

Lives at `tests/sys/fs/ext2/run-host-tests.sh` — invoked on-
developer-host and boots substrate in QEMU with a generated test
image attached as the second AHCI drive.  Each scenario
prep_rootfs's `dist/etc/fstest.conf` with the device/mount/fs
triple plus a `check=` command.  `etc/rc.d/99-fstest` consumes
the config in the guest and emits `FSTEST: ...` lines the harness
greps for.

Current scenarios (47 assertions, all passing):
- `mount-ext2`, `mount-ext3`, `mount-ext4-extents`,
  `mount-ext4-64bit`, `mount-ext4-64bit-with-csum` — feature-bit
  routing per filesystem rev.
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
- `xattr-read` — block-stored or inline xattrs visible via `ls -@l`.
- `extent-append-write` — empty extent file accepts write of
  "EXTENT_PAYLOAD\n"; payload survives umount + re-mount.
