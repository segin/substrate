# Executable Identity and ELF Metadata Cache

## Overview

Substrate identifies executable images by backing object identity, not by the
pathname text passed to `execve()`.

The current cache key is:

- filesystem identity: `fs_node->mp` when present, otherwise the node address
- object identity: `fs_node->inode` when non-zero, otherwise the node address

This keeps repeated launches of the same image, including alias paths that
resolve to the same backing object, on one metadata identity.

## Inode Contract

Filesystem-backed executable nodes are expected to present a stable `inode`
value through lookup and `stat(2)`.

Current behavior:

- ext2/minix already use on-disk inode numbers directly
- FAT synthesizes stable inode values for entries that do not have a useful
  cluster-based identity, such as zero-cluster files and FAT12/16 root entries

The kernel uses that identity in `execve()` rather than the original pathname.

## Cached Metadata

The ELF metadata cache stores immutable image parsing results:

- `Elf32_Ehdr`
- full program-header table
- detected OSABI/personality hint
- `PT_INTERP` path
- computed `AT_PHDR` value for AUXV setup

The cache does not store:

- argv/envp content
- stack images
- process names
- mutable process state
- loaded text/data pages

That boundary keeps the cache useful without changing `argv[0]`, comm naming,
or personality dispatch semantics.

Hot-cache validation is therefore defined in terms of repeated metadata work,
not wall-clock promise alone:

- repeated launches of the same backing object should avoid repeat ELF header
  and program-header reads
- caller-specific `argv[0]`, `AT_EXECFN`, process naming, and personality
  selection remain per-exec decisions layered on top of the cached metadata

## Invalidation

Cache entries are invalidated by metadata mismatch on lookup:

- different backing identity
- different file length
- different `mtime`
- different `ctime`

If any of those differ, Substrate reparses the ELF image and refreshes the
cache entry.

## BusyBox and Hard-Link Behavior

BusyBox-style multi-call binaries and hard-linked aliases should share one ELF
metadata identity when they resolve to the same `(filesystem, inode)` object.

That means:

- alias pathnames do not create redundant ELF-header/program-header reparsing
- personality detection and interpreter discovery are reused for the shared
  backing object
- `argv[0]` remains path-specific because it is still copied from the caller's
  argument vector during stack setup
