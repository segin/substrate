# cp Design And Test Guide

## Overview
`bin/cp/` now contains a production-focused `cp` implementation with modular components:

- `cp_opts.[ch]`: CLI parsing, option conflict rules, size parsing, preserve set parsing.
- `cp_path.[ch]`: dynamic and safe path helpers.
- `cp_hardlink.[ch]`: device+inode hash map for hardlink graph preservation and cycle set.
- `cp_atomic.[ch]`: atomic overwrite helper (`.cp.*` temp + fsync + rename + cleanup).
- `cp_preserve.[ch]`: metadata preservation (mode/owner/timestamps) plus best-effort xattr/ACL/flags.
- `cp_copy.[ch]`: copy engine (file/dir/symlink/special, recursion policy, sparse logic, overwrite policy).
- `cp.c`: program entry, signal handling, help/version, context lifecycle.

## Copy Engine Semantics

### Destination Resolution
- Multi-source copy requires destination directory.
- Existing destination directory receives `basename(source)` entries.
- Single source to non-directory path is copied directly to destination path.

### Overwrite Policy
- `-f`, `-i`, and `-n` are last-flag-wins.
- `-i` prompts on tty; non-tty default is `no` unless overridden.
- `-n` skips existing paths.
- `-f` suppresses diagnostics but still records failure in exit status.

### Symlink Policy
- Non-recursive default follows symlink targets unless `-P` is set.
- Recursive default is physical (`-P`).
- `-H` follows only command-line symlinks.
- `-L` follows all symlinks.
- Physical copy mode reads link text and recreates link object.

### Sparse Handling
- Preferred path uses `SEEK_DATA`/`SEEK_HOLE` when supported.
- Fallback scans read buffers and uses destination `lseek` for zero-only blocks.
- Destination is truncated to source logical size when holes are used.

### Reflink Handling
- `--reflink=auto` attempts clone first (FICLONE on supported hosts), then falls back to regular copy.
- `--reflink=always` requires clone support and fails if clone cannot be performed.
- `--reflink=never` skips clone attempts.

### Performance Policy
- Buffer size selection:
  - explicit `-b/--buffer-size` if provided;
  - else max(default, source `st_blksize`, destination `st_blksize`).
- Host builds can use `copy_file_range` and `sendfile` when sparse mode is `never`.
- Portable read/write loop is always available fallback.

### Hardlink Preservation
- `-a` or `--preserve=links` records `(st_dev, st_ino) -> destination path`.
- Subsequent matches create hard links instead of duplicating data.
- If hardlink recreation fails with cross-device error, copy falls back to data copy.

### Atomic Replace Strategy
- Overwrite path:
  1. Create unique temporary file in destination directory.
  2. Copy data and apply preservation to temp inode.
  3. fsync temp file.
  4. rename temp over destination.
  5. cleanup temp on any error.
- Toggle with `--no-atomic-replace`.
- `--remove-destination` removes destination path first (important for symlink replacement semantics).

## Security Notes
- Uses path-based operations where portable descriptor-only APIs are unavailable.
- Avoids fixed-size destination path buffers.
- Cleans partial outputs on interruption/failure.
- Signal handling marks stop flag and halts traversal cleanly.

## Platform Fallbacks
- xattr/ACL/flags are best-effort and compiled when host headers/APIs exist.
- Unsupported metadata APIs do not abort full copy by default.
- Symlink creation may be unavailable on reduced target libc/syscall surface.

## Test Matrix

### Unit Tests
- `test_opts`: parsing, preserve modes, size conversion, option conflicts.
- `test_path`: basename/join/dirname/path edge cases.
- `test_hardlink_map`: map correctness and set behavior.
- `test_atomic`: temporary file + commit path.
- `test_sparse`: sparse helper behavior (`all-zero` detection).

### Integration Tests
`tests/test_integration.sh` validates:
- file->file, files->directory
- recursive symlink modes (`-H/-L/-P`)
- hardlink graph preservation under `-a`
- `-l` success and cross-filesystem failure
- `-s` target text preservation
- sparse copy size behavior
- metadata preservation (`-p`)
- xattr preservation when supported
- atomic replace behavior
- `-n` and non-tty `-i` semantics

### Property Tests
`tests/test_property.sh` generates randomized directory trees (files, symlinks, hardlinks, sparse-like files) and compares source/destination manifests and content digests after `cp -a`.

### Fuzz Tests
- `fuzz_opts_path`: option parser and path handling.
- `fuzz_sparse_preserve`: sparse helper and preserve parsing surfaces.

Seed corpus:
- `bin/cp/tests/fuzz/corpus_opts_path/`
- `bin/cp/tests/fuzz/corpus_sparse_preserve/`

### Stress Test
`tests/test_stress.sh` creates wide/deep trees and validates full-copy count and sampled file equality.

## Running Locally

```sh
# Main host test suite
make -C bin/cp NATIVE_BUILD=1 ci

# Unit only
make -C bin/cp NATIVE_BUILD=1 unit

# Fuzz (bounded)
make -C bin/cp NATIVE_BUILD=1 fuzz-run
```

## CI
`tests/ci/test-cp.sh` runs:
- host `cp` build + unit/integration/property/stress + bounded fuzz,
- ASAN/UBSAN variant (clang when available),
- optional valgrind smoke.

## Fuzz Crash Reproduction
If a fuzzer writes a crashing input `crash-*`:

```sh
cd bin/cp
./fuzz_opts_path crash-<id>
# or
./fuzz_sparse_preserve crash-<id>
```

Use `ASAN_OPTIONS=halt_on_error=1` for deterministic sanitizer termination.

## Decision Log
- Last-flag-wins conflict resolution (`-f/-i/-n`) for deterministic CLI semantics.
- Default recursive mode is physical (`-P`) to avoid accidental symlink expansion.
- Atomic replacement enabled by default for safer overwrites.
- Metadata preservation errors are non-fatal unless they indicate unrecoverable write path failure.
