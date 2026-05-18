# Symlink open() returns the link node, not the target

## Symptom

ld.so reports `main-program needs libiconv.so.2 ? not found` when zsh
is the program and `/usr/lib/libiconv.so.2` is a symlink to
`libiconv.so.2.6.1`.  Replacing the symlink with a real file copy
makes the same zsh start fine.

## Where it should work

`sys/vfs/vfs.c:vfs_lookup` walks path components.  Each component is
resolved through `finddir_fs` → `finddir_fs_internal(node, name, 0, 1)`
with `follow_symlinks=1`.  finddir_fs_internal at line 403 detects
`FS_SYMLINK` nodes, reads the target via `node->readlink`, and
recursively calls `vfs_lookup` on the target (lines 443–447).  ext2
sets both `node->flags = FS_SYMLINK` and `node->readlink =
ext2_readlink` on IFLNK inodes (sys/fs/ext2/ext2.c:1204).  Fast
symlinks (<= 60 bytes — our case) memcpy from `inode->i_block` and
NUL-terminate.

## Hypotheses

1. `result->mp` inheritance (line 392-394) may not propagate to the
   target node when `vfs_lookup(node, link_target)` re-enters from
   line 446 — causing later operations to look up the wrong mount.
2. `current_thread->vfs_symlink_depth` bookkeeping at lines 438-442
   doesn't double-count for the absolute-path branch — possibly racy
   under SMP but we're UP.
3. The `result` returned at line 451 may be a freshly-allocated node
   whose lifetime doesn't outlive the inner vfs_lookup's
   `vfs_cross_mountpoint`.  Worth checking with ASAN-like
   instrumentation.

## Workaround

The image was rebuilt with `/usr/lib/libiconv.so.2` and `/bin/sh` as
literal file copies rather than symlinks.  Burns ~2.4 MB; safe to
revert once the real fix lands.
