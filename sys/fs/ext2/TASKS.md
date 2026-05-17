# ext2/3/4 driver — FreeBSD-parity roadmap

Reference: `~/freebsd/sys/fs/ext2fs/` (~16k LOC across 14 .c files).
Substrate's current driver: `sys/fs/ext2/ext2.c` (~2.6k LOC).

The goal is feature-complete parity with FreeBSD's `ext2fs(5)` so we
can mount any default-format ext2/ext3/ext4 image read-write without
data loss.  This is a multi-week effort; the punch list below tracks
what's done and what's next.

## Done

- [x] Mount-time feature-flag gate — refuse mount on unknown
      `INCOMPAT`, warn on unknown `ROCOMPAT`.  Per-bit defines and
      `EXT2F_*_SUPP` masks match FreeBSD's ext2fs.h naming.
- [x] **ext4 extent-tree resolver** — when an inode has
      `EXT4_EXTENTS_FL` set, `ext2_get_block_num` walks the inline
      header → index nodes → leaf extents and returns the physical
      block.  Multi-level (depth ≤ EXT4_EXT_DEPTH_MAX) supported.
      Sparse-region holes return 0 like the legacy block-pointer
      path.  16 TiB cap due to substrate uint32_t block numbers.
- [x] `chmod`, `chown`, `utimes/utimensat/futimens` persist to the
      on-disk inode through the new `setattr_fs` op.

## In progress / next

- [ ] **Read-only mount flag** — substrate's vfs has no `MS_RDONLY`
      plumbing.  Add it, then refuse-rw-mount when ROCOMPAT bits we
      don't support are set (matches FreeBSD behaviour).
- [ ] **htree directory index** (`EXT2F_COMPAT_DIRHASHINDEX`) —
      currently we ignore the index and linear-scan the directory.
      Linear scan still works because htree builds on top of the
      legacy dir-block layout, but we lose O(log n) lookup.  Port
      `ext2_htree.c` + `ext2_hash.c`.
- [ ] **256-byte inode + nanosecond timestamps** —
      `EXT2F_ROCOMPAT_EXTRA_ISIZE`.  When inode_size > 128, the
      tail of the inode holds `i_atime_extra`, `i_ctime_extra`,
      `i_mtime_extra`, `i_crtime`, `i_crtime_extra` (each 32-bit
      nanosecond fields).  Match substrate's int64_t timestamps to
      these so stat() reports sub-second precision.
- [ ] **Inode write path for extent files** — currently
      `ext2_inode_write` walks the legacy pointer array; calling it
      on an extent-tree file would corrupt the extent header.
      Need an `ext4_extent_alloc` / `ext4_extent_split` pair to
      grow extents on write.
- [ ] **Block-bitmap walker for extent files** —
      `ext2_free_blocks` likewise needs an extent-aware variant
      so truncate/unlink frees the underlying physical blocks.
- [ ] **Extended attributes** (`EXT2F_COMPAT_EXT_ATTR`) — substrate
      has no xattr surface yet.  Port `ext2_extattr.c` + add
      `sys_getxattr`/`setxattr`/`listxattr`/`removexattr`.
- [ ] **Metadata checksums** (`EXT2F_ROCOMPAT_METADATA_CKSUM`) —
      verify on read, update on write.  Needs `crc32c` in the
      kernel + per-block-type checksum locations.  Port
      `ext2_csum.c`.
- [ ] **Journal replay** (`EXT2F_INCOMPAT_RECOVER`) — refuse mount
      of dirty ext3/4 unless we can replay.  Even a read-only
      replay (parse journal, apply committed transactions to an
      in-memory shadow) would let us mount dirty filesystems.
- [ ] **64-bit block addresses** (`EXT2F_INCOMPAT_64BIT`) — bump
      substrate's `daddr_t` analog to 64-bit through the driver.
      Required for any modern Linux-generated ext4 > 16 TiB.
- [ ] **htree creation on rename/insert** — once the index reader
      lands, the directory-write path needs to update the htree
      when entries are added/removed.
- [ ] **Multi-mount protection** (`EXT2F_INCOMPAT_MMP`) — block
      that detects concurrent mounts via shared storage.  Low
      priority; substrate doesn't ship a multi-node deployment.

## Automated test harness

Initial sketch lives at `tests/sys/fs/ext2/` with:
  - `mkfs-and-mount.sh` — create an ext2, ext3, ext4 image with
    debian's mkfs.ext{2,3,4}, mount each on substrate via the
    QEMU image, dmesg-grep for the feature lines we expect.
  - `extent-read.sh` — create a 2 MiB file on an ext4 image filled
    with a known PRNG stream, sha256 from substrate's `cat`
    matches.
  - `htree-lookup.sh` — directory with 10000 entries, finddir of
    the last must complete < 50ms (verifies htree linear-scan
    fallback isn't pathological).
  - `setattr-persist.sh` — `touch -t 2020010100 file && umount &&
    mount && stat file` shows the right timestamp.
  - `feature-refuse.sh` — mkfs.ext4 with -O 64bit; mount must fail
    cleanly (no kernel panic) with the expected refusal message.

Tests run inside the QEMU image as part of `make check-fs` on the
substrate-build host.
