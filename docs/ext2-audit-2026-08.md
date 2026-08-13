# ext2/3/4 driver audit — 2026-08

> **Status: worked through.** Every finding below has been addressed —
> fixed in the driver, or (for the handful that are deliberate
> refusals) verified safe and recorded as a known gap in
> `sys/fs/ext2/TASKS.md`. The commits reference findings by their ID
> (`git log --grep 'BM-01'`), and the regression scenarios added to
> `tests/sys/fs/ext2/run-host-tests.sh` cover the four criticals plus
> the interoperability highs, each gated on a host-side `e2fsck -fn` of
> the image substrate wrote. The finding text is kept in the past
> tense of the audit for traceability; read it as "what was wrong",
> not "what is wrong".

Comprehensive audit of `sys/fs/ext2/` (ext2.c 4320 + ext2.h 478 +
ext2_hash.c 209 + ext2_xattr.c 421 lines) against the **ext4 on-disk
format specification** (kernel.org v4.19, "Data Structures and
Algorithms", `Documentation/filesystems/ext4/ondisk/`).

Method: nine analysis lenses (superblock/mount, group descriptors +
allocators, inode structure/lifecycle, block mapping + extent trees,
directories + htree, xattrs, checksums end-to-end, concurrency/error
paths, VFS-surface semantics), each lens's findings adversarially
verified by an independent agent that re-read the cited code and spec
text. 119 raw findings raised; the verify pass confirmed all of them
(several severities corrected up or down), and cross-lens duplicates
are merged below into **78 unique defects: 4 critical, 19 high,
29 medium, 21 low, 5 info**. Line numbers refer to the tree at commit
`e7764a5c3`.

Reachability ground rules used for grading: the driver refuses unknown
INCOMPAT, ro-mounts unknown ROCOMPAT, refuses RECOVER, and accepts
FTYPE, META_BG, EXTENTS, FLEX_BG, CSUM_SEED, 64BIT (INCOMPAT) plus
SPARSE_SUPER, LARGEFILE, DIR_NLINK, HUGE_FILE, EXTRA_ISIZE, GDT_CSUM,
METADATA_CKSUM (ROCOMPAT) — so **default `mkfs.ext4` images mount
read-write**, and anything that corrupts such an image is graded
against that fact.

## Critical

### C1. Lazy-init (uninit) block groups: `bg_flags` never read; allocator trusts garbage bitmaps
`ext2.c:2769` / `ext2.h:201` — merges BG-01, SB-03, CK-02. spec 2.2.5, 2.3.2, 2.3.3.

`ext2_group_desc_t` names offset 0x12 `bg_pad`; it is the spec's
`bg_flags`, and `EXT4_BG_BLOCK_UNINIT` / `EXT4_BG_INODE_UNINIT` /
`bg_itable_unused` appear nowhere in the driver. On GDT_CSUM and
METADATA_CKSUM filesystems — both in `EXT2F_ROCOMPAT_SUPP`, i.e. every
default mkfs.ext4 image — untouched groups ship with UNINIT set and
their on-disk bitmap blocks **deliberately uninitialized**.
`ext2_alloc_block()` (2761–2826) and `ext2_alloc_inode()` (2900–2956)
select any group with a nonzero free count and scan the raw on-disk
bitmap as authoritative. On a zeroed device the uninit bitmap reads
all-free, so the allocator hands out the group's first blocks — the
backup superblock / GDT copy in sparse-super groups. Worse: the bitmap
is written back but UNINIT is never cleared, so a later Linux mount
*ignores* that bitmap, considers the just-allocated blocks free, and
re-allocates them — silent cross-linked data corruption both ways.

Fix: model `bg_flags`; synthesize the bitmap for UNINIT groups
(in-group metadata marked used), clear the flag + zero the real bitmap
on first allocation, maintain `bg_itable_unused`. Until then, drop
GDT_CSUM/METADATA_CKSUM from the rw SUPP set (force ro) — the
mechanism already exists at ext2.c:2369–2376.

### C2. Maximal initialized extent (`ee_len == 32768`) decodes as length 0 — 128 MiB ranges read as holes
`ext2.c:784` — BM-01. spec 2.4.2.3.

`len = ex[i].e_len & 0x7FFF` turns the spec-legal, Linux-routine
maximum initialized extent length 32768 (`EXT_INIT_MAX_LEN`, emitted
for any ≥128 MiB contiguous run at 4 KiB blocks) into 0, so the
coverage test never matches and the entire range resolves as a hole:
reads return zeros instead of data; overwrites fail ENOSPC (the extent
allocator refuses the "sparse" write). Correct decode:
`len = raw <= 32768 ? raw : raw - 32768; uninit = raw > 32768;`.

### C3. METADATA_CKSUM/GDT_CSUM mounted read-write, but only the inode checksum is ever recomputed — first write bricks the volume
`ext2.h:114`, `ext2.c:435/511/2826` — merges CK-01, SB-01/CK-03/CY-01
(superblock), SB-02/BG-02/CK-04 (group desc), BG-03/CK-05 (bitmap
csums), DE-01/CK-06 (dirent tails). spec 2.2.8.

The only write path that recomputes a checksum is
`ext2_write_inode()`. Everything else goes checksum-stale on disk:

- **Superblock `s_checksum` (0x3FC)** — `ext2_flush_super()`'s RMW
  patches the 204-byte prefix (including the free counts the
  allocators change) while preserving the *old* checksum; primary and
  every sparse backup.
- **Group-descriptor `bg_checksum` (+30)** — `ext2_flush_group_desc()`
  memcpy's the 32-byte in-memory descriptor (which doesn't model the
  csum field) over the on-disk one after free counts changed. The
  crc16 flavor for GDT_CSUM-only filesystems is entirely
  unimplemented (no crc16 in the tree) — neither verified nor written.
- **`bg_{block,inode}_bitmap_csum_lo/hi`** — bitmaps are written
  through on every alloc/free, their descriptor checksums never.
- **Dirent tails (`ext4_dir_entry_tail`)** — every dirent add/remove
  rewrites the block without recomputing `det_checksum`; the
  reuse-a-deleted-entry path can even consume the 12-byte tail itself
  as free space, and new directory blocks are created with no tail.

Consequence: mount a metadata_csum image rw, `touch /mnt/a`, unmount —
the driver's **own next mount refuses** ("superblock checksum
mismatch", ext2.c:2398; "bg descriptor csum mismatch", 2637), Linux
refuses too, and e2fsck flags every touched structure. A substrate
root on a metadata_csum image bricks its own next boot after the first
write. Same short-term fix as C1: force these volumes read-only until
the write side is complete.

### C4. `rename()` of a name onto itself deletes the file
`ext2.c:3599` — DE-02. POSIX rename.

No same-entry/same-inode check anywhere (kern_rename passes basenames
straight through). With old==new: the target lookup finds the same
entry, `ext2_unlink()` removes it (links 1→0, orphaned under the
rename's pin), `ext2_add_entry()` re-adds the name, then
`ext2_remove_entry(old_parent, old_name)` removes the **just-added**
entry again. Returns 0; dropping the pins completes the deferred
delete, freeing inode and data. `mv "$f" "$f"` = silent data loss.
Empty directories die the same way via the rmdir branch. Fix: return 0
when source and target resolve to the same inode (POSIX no-op).

## High

### H1. META_BG accepted but the GDT is always read/written as one contiguous run
`ext2.c:2553/2596`, flush at 494–512 — SB-04/BG-04/CK-07. spec 2.2.4.

`s_first_meta_bg` (0x104) is never read. On a meta_bg image with >1
metagroup (>128 groups at 4 KiB), descriptors past the first metagroup
are read from blocks that are file data on the real layout — garbage
becomes `bg_inode_table` etc. — and the deferred free-count flush
*writes descriptors over file data*. Remove META_BG from
`EXT2F_INCOMPAT_SUPP` or implement the layout.

### H2. 64BIT: superblock high words never checked — >2³²-block filesystems mount as a silently truncated view
`ext2.c:2562` — SB-05, CK-08. spec 2.3.1 (0x150/0x158).

The mount verifies per-descriptor `bg_*_hi` only. `s_blocks_count_hi`
is never read, so an 18 TiB 64-bit fs mounts as a ~2 TiB view: reads
past the truncated count return silent zeros; an rw mount rewrites
free counts from the truncated view. Refuse when 0x150/0x158 are
nonzero — the driver's own stated 64BIT policy.

### H3. Uninitialized extents treated as initialized: reads leak stale disk data, writes invisible to Linux
`ext2.c:785`, `ext2.h:136–140` — BM-02. spec 2.4.2.3.

Ranges under an uninitialized extent must read as zeros; the resolver
returns the mapped block, so `cat` on a `fallocate`d file returns
previous files' deleted contents (cross-file information disclosure).
Writes into the range RMW the physical block but never clear the
uninit bit, so Linux reads the file as zeros — data silently lost to
every other implementation. (The header comment "data still reads as
zeros" is wrong.)

### H4. Fast-symlink boundary off-by-one, both directions
`ext2.c:3895` (write), `1778` (read) — BM-03/MS-02, BM-08. spec 2.4.2.1.

`ext2_symlink` inlines `target_len <= 60` — a 60-byte target fills
i_block with no NUL; e2fsck treats `i_size >= 60` fast symlinks as
invalid and clears them; Linux's reader assumes NUL termination.
Inline only `< 60`. Read side decides fast-vs-slow from `i_size <= 60`
alone; the correct discriminator involves i_blocks/xattr state, so a
short *slow* symlink (legal, and produced whenever an xattr block
exists) returns block-pointer bytes as the target.

### H5. Data-path write errors ignored; extent physical addresses never range-checked
`ext2.c:1334/1346` + 12 indirect-table write sites + `3447`; resolver
`786–790` — BM-04.

`ext2_write_block()` refuses out-of-range blocks by returning 0 —
and every data-path caller discards the return, so `write(2)` reports
full success while the data went nowhere. `ext4_extent_resolve()`
checks only `phys >> 32`, not `s_blocks_count`, so a crafted extent
pointing past the device yields silent data discard (write) / zeros
(read) with no error. Device I/O errors convert to success the same
way. Check returns, fail with -EIO via `errp`, range-check resolved
extents.

### H6. Partial failure in `ext2_free_inode_blocks` leaves the on-disk inode pointing at freed blocks
`ext2.c:3167–3186` — BM-05.

Direct blocks are freed (bitmap write-through) *before* the indirect
walk; if the indirect free fails (-EIO on a bad pointer, -ENOMEM),
truncate/unlink return without ever writing the inode — on disk the
bitmap says free while the inode still references the blocks. Next
allocation cross-links two files. Commit the cleared inode first
(Linux ordering), free from a snapshot of the pointers.

### H7. `ext2_add_entry` double-counts `i_blocks` for every directory growth
`ext2.c:3455` — BM-06.

`ext2_alloc_inode_block()` already accounts the new block (1139 etc.);
line 3455 adds it again. Every directory grown past one block fails
e2fsck ("i_blocks is 24, should be 16") on every image substrate
writes. Fix: delete line 3455.

### H8. unlink/rmdir of an extent-mapped file mutates the directory, then errors — `rm` is broken on default ext4 images
`ext2.c:4086→4108` — BM-07/CY-06/DE-03. (Consequence goes beyond the
documented "extent teardown unimplemented" limitation.)

`ext2_free_inode_blocks()` deliberately refuses extent inodes
(-EOPNOTSUPP), but unlink calls it *after* removing the dirent and
returns the error without `ext2_write_inode`/`ext2_free_inode`: name
gone, inode+blocks permanently leaked, `rm` reports "Operation not
supported". Every regular file on a default ext4 image is
extent-mapped. `ext2_rename` over an existing extent file ignores the
failure (3580); `ext2_node_close`'s deferred path (124–126) frees the
inode while leaving its blocks allocated. Decide before mutating:
either refuse up front, or accept the documented leak deliberately
(remove entry, write dtime, free inode, return 0).

### H9. Nodes returned by finddir/alloc_node are unpinned — recyclable while in use
`ext2.c:1623/2307` — CY-02.

Returned children have `pin_count = 0`; any such slot is fair game for
the recycler (1544), which memsets the ctx and rebuilds the shared
`fs_node_t` for a different inode. The VFS walks multi-component paths
through bare returns and pins only at the end (vfs.c:960→1000); driver
paths do the same (unlink's victim across sleeping I/O, rmdir's
emptiness scan, symlink's slow path, rename's ancestry walk). A
concurrent lookup burst recycles the slot mid-use: A ends up
reading/writing a different file than it resolved, or decrements the
link count of an unrelated inode. alloc_node should return pinned
references with explicit release.

### H10. `ext2_unmount` tears down the cache with no lock and no pin/busy check
`ext2.c:4291–4318` — CY-03/MS-03.

Frees scratch buffers, memsets ctx (including a possibly-held mutex),
and `kfree(fs)` without `ext2_node_cache_lock`, pin, or lock checks.
`MNT_FORCE` bypasses the VFS busy check, and mid-lookup threads with
no fd are invisible to it: a reader sleeping in `ext2_readdir` resumes
parsing freed heap and unlocks a destroyed mutex. Deferred orphan
deletes are also silently dropped. Take the cache lock, refuse -EBUSY
on pinned/locked slots, quiesce before `kfree(fs)`.

### H11. `ext2_free_inode`'s cache eviction checks pin but not `ctx->lock`
`ext2.c:3018–3038` — CY-04.

The recycler requires `pin_count==0 && lock.locked==0`; this eviction
checks only pin. A node can be locked-while-unpinned (finddir returns
unpinned children; readdir holds their lock across sleeping disk I/O):
concurrent removal frees the scratch buffers under the sleeping reader
and memsets the mutex it holds. Mirror the recycler's check.

### H12. Check-then-act directory ops race: duplicate names, rmdir of refilled dirs
`ext2.c:3822/3854, 3972/4041, 3874/3935, 3494/3512, 4176/4182` — CY-05.

The EEXIST probe and the insert are separate critical sections; two
concurrent creates of one name both pass the probe and both insert —
two live identical dirents (e2fsck: duplicate entries). rmdir's
emptiness check is likewise decoupled from removal, so a concurrent
create inside the victim is orphaned. Serialize probe+mutate per
directory; make add_entry re-check under its lock.

### H13. Metadata read failures ignored: stale buffer contents get modified and written back
`ext2.c:3348/3376, 3732/3757, 3629/3643` (dirents); `1154/1176/1191…`
(indirect chains); readers `1889, 2210` — CY-07/DE-07 + BM-11.

`ext2_read_block()` zero-fills only on the range reject; on device I/O
failure the buffer keeps the *previous* block's image. Directory
modify paths splice an entry into that stale image and commit it over
the target block — one transient read error becomes on-disk directory
corruption (real entries destroyed, other block's entries duplicated).
Indirect-chain alloc paths dereference and write back equally stale
pointer arrays. Check every metadata read for `!= block_size`, abort
-EIO before modifying.

### H14. `i_extra_isize` stamped 22 — misaligned, off-by-2 from spec — on every inode substrate creates
`ext2.c:649` — IN-01/SB-06/XA-01. spec 2.4.1 (field "includes this field").

`want = write_bytes - 128 - 2` = 22; correct is 24 (through
i_crtime_extra), and mkfs's `s_want_extra_isize` (32) is ignored.
22 fails e2fsck pass 1 (`extra_isize & 3`), so every file created on a
256-byte-inode volume trips PR_1_EXTRA_ISIZE. Use 24 minimum; better,
honor `s_want_extra_isize` at create.

### H15. Inline-xattr destruction: unvalidated `i_extra_isize`, read-side slop-zeroing + 152-byte write window
`ext2.c:373–378, 641–652` — IN-02/XA-02 (verifier-upgraded).

For an on-disk `i_extra_isize` in [4,21], the ibody xattr area starts
inside bytes 132..151 — the window every `ext2_write_inode` rewrites
from the (read-side-zeroed) struct while force-bumping extra_isize to
22: one chmod/utimes/write permanently destroys the xattrs
(security labels included), no error. And `i_extra_isize` is never
bounded to `inode_size - 128`, so a corrupt huge value defeats the
zeroing and later reads interpret xattr bytes as timestamps. Only
write through `128 + min(on_disk_extra, inode_size-128)`; validate
4-aligned `extra_isize ≤ inode_size-128` on read.

### H16. Dirent `file_type` written unconditionally — corrupts 16-bit `name_len` on non-FTYPE filesystems
`ext2.c:3373/3389/3444, 4016/4022` — MS-01/DE-05/SB-11. spec 2.4.3.1.

`EXT2F_INCOMPAT_FTYPE` is never consulted outside the SUPP mask.
Without the feature (rev-0 images — e.g. the Slackware-era a.out
images this project boots — and no-FTYPE rev-1), byte 7 is the high
byte of a 16-bit `name_len`: every entry substrate creates reads as
`name_len = len + 256*ft` on Linux — "directory entry has invalid
length", file unreachable. Gate write (and read→DT_*) on the feature
bit.

### H17. `sparse_super2` ignored — backup-superblock flush overwrites file data at group starts
`ext2.c:453–480` — SB-07. spec 2.3.1 (`s_backup_bgs` 0x24C).

Backup placement is decided solely from ROCOMPAT bit 0x1. On a
sparse_super2 image only the groups in `s_backup_bgs` hold backups;
the first block of every other group is ordinary data — which
`ext2_flush_super()` overwrites with superblock images on every
deferred-count flush. COMPAT bits never block mounting, so this hits
valid images rw. Honor `s_backup_bgs`, or simply stop rewriting
backups at runtime (Linux never does).

### H18. `ext2_rename` accepts "." and ".." names
`ext2.c:3526` — DE-04.

mknod/symlink/mkdir/rmdir all guard dot names; rename doesn't.
`rename("/mnt/a/.", "/mnt/b/x")` deletes a's own "." entry and gives
the directory a second parent — structural corruption from one
syscall. Add the same -EINVAL guard for both names.

### H19. rmdir emptiness check treats a hole or malformed block as end-of-directory
`ext2.c:4144` + readdir `1888` — DE-06.

`ext2_dir_is_empty()` trusts readdir's NULL, but readdir aborts the
whole walk on an unmapped block (`block_num == 0 → break`). A
directory with a hole at block 0 and 100 live entries in block 1
reports empty; rmdir frees it, mass-orphaning the children. Walk raw
blocks; treat hole/unreadable/malformed as *not empty*.

## Medium

| # | ids | where | defect |
|---|-----|-------|--------|
| M1 | BG-05/CY-12/MS-05 | 530, 4269–4280 | `sync(2)`/`fsync` never call `ext2_sync_meta`; deferred free counts flush only on threshold(256)/unmount. rw→ro remount strands them permanently (`ext2_sync_meta` early-outs on `fs->readonly`, and remount doesn't flush first). |
| M2 | BG-06/SB-17 | 2752 | `s_r_blocks_count` reserve not enforced by the allocator — any user consumes the root reserve down to zero (statfs f_bavail already advertises the truth; write outruns it). |
| M3 | BG-07 | 2959 | Even with C1 fixed: `bg_itable_unused` and INODE_ZEROED are never maintained on inode allocation — e2fsck accounting complaints on gdt_csum images. |
| M4 | BM-09 | 3241 | `truncate` to a smaller non-zero length returns -EOPNOTSUPP — POSIX `ftruncate` gap (shrink-to-0 and grow work). |
| M5 | BM-10/CY-13/DE-15 | 1321–1329, 3854, 3424 | errno collapses: extent-refusal and -EFBIG both surface as ENOSPC; create-family reports ENOSPC as -EIO (add_entry returns bare -1 for out-of-space); ENAMETOOLONG lost the same way. |
| M6 | CY-08 | 3618–3643 | rename's `..` rewrite uses the *moved* directory's scratch buffers without taking that node's lock — races its concurrent readers. |
| M7 | CY-09 | 3497, 4089, 3611 | `i_links_count` read-modify-writes without the node lock (link/unlink/rename/mkdir) — lost updates under concurrency; wrong link counts persisted. |
| M8 | CY-10 | 4108→4110 | unlink/rmdir free block-bitmap bits (write-through) *before* committing the deleted inode — a crash in the window cross-links the blocks with the next file, contradicting the design comment's stated crash cost (ext2.h:278–284). Commit inode first. |
| M9 | CY-11 | 2826, 2956 | Allocators ignore bitmap write-back failure — block handed out while the on-disk bitmap still says free; next mount double-allocates. |
| M10 | CY-14 | 426–430, 470 | `ext2_super_rmw` substitutes **zeros** for the whole 1024-byte superblock when the preservation read fails, then writes it: one transient read error wipes `s_journal_inum`, `s_hash_seed`, `s_desc_size`, `s_checksum_seed`, `s_checksum`. Abort -EIO instead. |
| M11 | CY-15 | 116–144 | Deferred-close teardown drops the cache lock before invalidating the slot; a lookup of the recycled inode number lands on the half-torn slot via the pinned-refresh branch and gets `ctx->fs == NULL` → NULL-offset deref on next use. Invalidate before unlocking. |
| M12 | IN-03 | 1712–1717, 1445 | 32-bit uid/gid: `l_i_uid_high`/`l_i_gid_high` (osd2) never read or written — files owned by uid>65535 stat wrong, and any setattr writeback truncates ownership on disk. |
| M13 | IN-04 | syscall.c:1613 | `st_blocks` computed from file length, not `i_blocks` — sparse files report dense sizes (`du` wildly wrong); xattr/indirect overhead invisible. |
| M14 | MS-04/SB-10 | 2311–2429 | `s_state` never managed: mount doesn't check EXT2_VALID_FS / honor `s_errors`, doesn't clear the clean bit in use, unmount doesn't set it — crashes leave "clean" filesystems that fsck skips; error-state volumes mount rw silently. |
| M15 | MS-06/IN-06 | 1445, 3841 | Device nodes: only the old 16-bit `i_block[0]` encoding is read/written; Linux's new encoding (`i_block[1]`, minor>255 or dyn-major) reads back as rdev 0. |
| M16 | MS-07 | 3838, 3998, 3886 | Created files/dirs owned by the *parent directory's* owner; symlinks hardcoded root:root — never the creating process's euid/egid (and the syscall layer never fixes it up). |
| M17 | MS-08/DE-14 | 3088–3097, 3505, 3584 | `S_IFSOCK` missing from all three type maps: sockets get FT_UNKNOWN (mknod) or FT_REG_FILE (link/rename) in dirents. |
| M18 | DE-08/MS-09 | 3376/3392 vs 3457 | Parent dir mtime/ctime updated only on the new-block path — the common split/reuse insertions leave parent times stale (POSIX requires update on every entry add/remove; remove does it). |
| M19 | DE-09 | 3577/3580 | rename discards the target-removal return; a removal failing before the entry is gone → `add_entry` creates a **duplicate name**. Remove-then-add also opens a POSIX-forbidden window where the target name doesn't exist. |
| M20 | DE-10 | 3555–3567 | rename cycle check is best-effort: bounded to 256 ancestors, and any `..` lookup failure silently ends the walk — deep or partially-corrupt trees can splice a detached cycle. |
| M21 | DE-11 | 4047 | No DIR_NLINK handling and no EMLINK cap: parent `i_links_count` (uint16) wraps 65535→0 at ~65k subdirectories; spec wants the 1-means-uncounted convention. |
| M22 | DE-12 | 3640 | Cross-parent dir rename never invalidates the moved dir's cached `..` dcache entry (the cycle walk itself populates it) — `cd b; cd ..` lands in the old parent; subsequent cycle checks climb the wrong ancestry. |
| M23 | DE-13 | 3442, 3751, 2434 | 64 KiB blocks accepted at mount but `rec_len` is uint16: full-block entry writes `rec_len = 0` (truncated 65536); coalesce can wrap. The 0xFFFF encoding convention is unimplemented. Reject log_block_size > 5 or implement it. |
| M24 | SB-08/DE-16 | 2038, 2412 | htree hash signedness taken from the dx_root byte alone; superblock `s_flags` (0x160) signed/unsigned-hash bits never read. On an unsigned-hash fs whose root says legacy/half_md4 (the common case), lookups of names with high-bit bytes compute the signed hash, land in the wrong leaf, and the *authoritative* htree-miss path can report ENOENT for existing files (currently masked by the linear-scan fallback, EXT2-08). |
| M25 | SB-09 | 2613 | `bg_block_bitmap`/`bg_inode_bitmap`/`bg_inode_table` never sanity-checked at mount (in-range, non-overlapping): crafted descriptors redirect metadata I/O anywhere in-range; a bitmap write lands on file data. |
| M26 | XA-03 | 3147–3191 | Deleting an inode never releases `i_file_acl`: the xattr block leaks on every delete — and the code never touches `h_refcount`, so when the write side arrives, blind freeing would destroy shared xattr blocks (mkfs dedups them across thousands of inodes). |
| M27 | XA-04 | ext2_xattr.c:379 | `getxattr("system.posix_acl_access")` always returns -ERANGE: the empty-suffix rejection (`slen == 0`) fires before the walker that the EXT2-25 fix made match empty names — that fix is dead code; listxattr advertises names getxattr can't fetch; grow-and-retry callers loop forever. |
| M28 | XA-05 | 3836, 641–652 | Recycled inode slots keep the previous file's ibody xattr bytes (only 152 of 256 bytes ever written; free doesn't scrub): after e2fsck repairs extra_isize (needed anyway per H14), the old file's xattrs *resurrect* on the new file — cross-file metadata/security-label leak. Zero the full record at create. |
| M29 | XA-06 | syscall.c:2092 | get/listxattr have no permission or namespace gating: any user reads `user.*` off files they can't open and `trusted.*` outright (Linux restricts trusted.* to CAP_SYS_ADMIN and gates user.* on file read permission). |

## Low

| # | ids | where | defect |
|---|-----|-------|--------|
| L1 | BG-08 | 2965 | Allocated inode number never bounded to `s_inodes_count` (last-group tail bits). |
| L2 | BM-12 | 723–796 | Corrupt extent tree (bad magic/depth) resolves as all-holes — file reads as zeros with no EIO; corruption indistinguishable from a sparse file. |
| L3 | BM-13 | 3618–3627 | rename's `..` rewrite passes possibly-NULL scratch buffers to `ext2_get_block_num` (only `block_buf` is checked after the lazy kmallocs). |
| L4 | BM-14 | 3232, 1354 | Extending past EOF (truncate-up or write) doesn't zero the stale tail of the final partial block — old bytes reappear inside the new range (POSIX requires zeros). |
| L5 | BM-15 | 1784 | `ext2_readlink` ignores a short `ext2_inode_read` and returns `link_size` anyway — uninitialized kernel buffer bytes returned to userspace on I/O error. |
| L6 | CY-16 | 273, 576 etc. | Internal helpers (`read_inode`/`write_inode` etc.) return bare -1, not -errno; currently masked by caller remapping but contract-fragile (per project errno directive). |
| L7 | CK-09/XA-08 | — | metadata_csum read-side gaps beyond the documented ones: dirent-tail, htree-node, extent-block-tail and xattr-block (`h_checksum`) checksums never verified (consistent with read-only tolerance; documents the full list). |
| L8 | DE-17 | 1519–1532 | The pinned stale-slot refresh in `alloc_node` rebuilds the fs_node but keeps the *previous* directory's dcache and readdir cursor — stale-name hits on the recycled inode. |
| L9 | IN-05 | 1406, 1359 | On-disk 32-bit timestamps treated as unsigned (pre-1970 → year ~2106; spec says signed + epoch bits); data-write paths update seconds but never the `*_extra` nsec fields, leaving stale nanoseconds. |
| L10 | IN-07/SB-16 | 2429 | `s_last_orphan` ignored at mount: cleanly-recoverable orphan lists (from a Linux crash) leak inodes/blocks until fsck. |
| L11 | MS-10 | 3906 | `ext2_symlink` accepts targets longer than `block_size-1`, writing a symlink whose `i_size` exceeds what one block holds — e2fsck deletes it; the driver itself can't read it back whole. Refuse -ENAMETOOLONG. |
| L12 | MS-11 | 1775 | readlink clamps to `size-1` for NUL termination — truncation is undetectable and `sys_readlink` returns one byte short of POSIX (which fills the full buffer, no NUL). |
| L13 | MS-12 | 1793, 1841 | atime never persisted on read/readdir/readlink (no relatime-style policy either). |
| L14 | MS-13 | 1711 | `ext2_setattr` doesn't bump ctime for UID/GID/MODE/times changes (chmod path does; chown/utimensat don't). |
| L15 | MS-14 | 1800, 3194, 4061 | `EXT4_IMMUTABLE_FL`/`APPEND_FL` ignored by write/truncate/unlink/rename. |
| L16 | MS-15 | 2684 | Mount bookkeeping absent: `s_mnt_count`/`s_mtime`/`s_last_mounted` never set at mount, `s_wtime` never on write. |
| L17 | MS-16 | 4257 | statfs: `f_flags = 0` (ro mounts claim rw), `f_fsid` empty, mount names blank. |
| L18 | MS-17 | 3824–3829 | `ext2_mknod` accepts `S_IFLNK` (creates a degenerate empty symlink); `S_IFDIR` returns -EISDIR where POSIX mknod prescribes -EPERM. |
| L19 | SB-12 | 473 | Backup superblocks stamped with the primary's `s_block_group_nr` (0) instead of their own group number. |
| L20 | SB-13/SB-14/SB-15 | 2471, 2565, 2483 | Mount validation gaps: `s_first_data_block` unvalidated against block size (GDT location hardcoded independently of it); `s_desc_size` accepts non-power-of-2 and <64-with-64BIT; unknown `s_rev_level >= 2` silently treated as rev 1. |
| L21 | XA-07 | ext2_xattr.c:328 | xattr reader accepts layouts Linux rejects (unaligned `i_extra_isize`, values overlapping the entry table) — read-tolerance, but asymmetric with Linux. |

## Info / notes

- **BM-16** (`ext2.c:840`): triple-indirect boundary `ptrs³` overflows
  uint32 at block sizes ≥ 8 KiB — unreachable for the file sizes the
  32-bit `i_size` allows today, but a latent trap and it interacts
  with the 64 KiB-block acceptance (M23).
- **CY-17** (`ext2.c:1487`): the root-pin watchdog re-pins on loss;
  the unbalanced `close_fs` it papers over is still undiagnosed
  (`ext2_root_pin_lost` counter exists for exactly this).
- **DE-18** (documented, safe): all INDEX_FL directory modifications
  refuse -EOPNOTSUPP and the flag is never cleared — correct
  corruption-avoidance, but it makes every htree directory on a real
  ext4 image effectively read-only (no create/unlink/rename in /usr,
  /etc, … of a Linux-made image).
- **MS-18**: `fs_attr`/getattr carry no nlink/blocks, so `st_nlink`
  is fabricated by the syscall layer — hardlinked files stat wrong.
- **MS-19**: header drift — duplicate `ext2_read_block` declaration
  (ext2.h:405/420), dead `last_readdir_*` fields, stale
  "substrate doesn't have a ro mount flag" comment (it does now).

## Verified clean — do not re-litigate

The verify pass confirmed these areas sound against the spec:

- Superblock magic/log_block_size (≤6)/blocks_per_group/
  inodes_per_group/inode-size validation and the group-count
  derivation from `s_first_data_block` (EXT2-24); the GDT-size caps
  (EXT2-28); desc_size stride on read *and* write (the 64-byte RMW
  preserves the high half).
- Inode checksum algorithm (seed chaining, csum-field zeroing,
  extra_isize-conditional hi-half — matches FreeBSD `ext2_ei_csum`)
  on both verify and rewrite; superblock and group-descriptor
  *mount-time* verification, including the tampered-image refusal.
- Extent-resolver bounds discipline (eh_ecount clamped to node
  capacity, depth cap + strictly-decreasing-depth cycle guard, hi-word
  refusal), and the EXT2-11 hi/lo encoding fixes in the append path.
- Dirent iteration hardening in readdir/finddir/scan_leaf
  (rec_len < 8 / block-overrun / name_len-vs-rec_len clamps, the
  advance-to-next-block anti-spin guards), byte-offset readdir
  cursors, negative-dcache protocol (only cached on clean walks).
- htree hash functions bit-for-bit vs FreeBSD (all six variants,
  padding prep, EOF clamp); htree root parse bounds; the EXT2-08
  linear fallback after an htree miss.
- Allocator invariants: EXT2-17 tail-bit skip, EXT2-23 first_ino
  handling, double-free accounting guards, negative-offset refusals
  in inode read/write (the lseek attack), EXT2-12 4 GiB size refusal,
  xattr walker's EXT2-14 overflow-free bounds.

## Suggested priority

1. **Stop the bleeding on default ext4 images**: force ro when
   GDT_CSUM/METADATA_CKSUM present (kills C1+C3 exposure in one line)
   and remove META_BG from INCOMPAT_SUPP (H1); fix C2 (one-line
   decode) and C4 (same-inode rename no-op); fix H8 so `rm` works.
2. **Interop of what we write**: H7 (one-line delete), H14, H16, H4,
   H17, M18.
3. **Crash/robustness**: H5, H6, H13, M8, M10.
4. **Lifetime/concurrency**: H9–H12, M11 (these are one design change:
   pinned references + lock-honoring teardown).
5. The medium/low conformance tail as background work.
