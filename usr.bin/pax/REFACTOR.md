# PAX Refactoring Checklist

This document tracks technical debt and future refactoring tasks for the `pax` utility.

## 1. Archive formats
- [ ] Implement full PAX extended header support (`pax_fmt.c`).
- [ ] Implement `newc` SVR4 cpio format fully (checksums).
- [ ] Implement format auto-detection logic in `ar_read`.

## 2. Core I/O
- [ ] Implement full blocking I/O in `ar_io.c`.
- [ ] Implement media volume switching (multi-volume archives).
- [ ] Implement `gzip`/`bzip2` compression piping.

## 3. Substitution
- [ ] Implement regex engine or link against `libregex` when available.
- [ ] Support full POSIX BRE substitution syntax.

## 4. File Traversal
- [ ] Implement recursive directory traversal (`fts` or manual recursion).
- [ ] Handle symlinks and hardlinks correctly (link cache).
- [ ] Implement loop detection (visiting same directory twice).

## 5. Security
- [ ] Audit for path traversal vulnerabilities (`../../`).
- [ ] Implement `secure_path()` check.
- [ ] Test with fuzzing inputs.

## 6. Library Extraction
- [ ] Extract format handling into `libarchive` compatible library in `usr.lib/archive/`.
- [ ] Expose public headers in `include/archive.h`.
