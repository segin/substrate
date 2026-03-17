# Minix FS Driver Audit (2026-03-16)

## Scope
- File: `sys/fs/minix/minix.c`
- Focus: correctness, data integrity, and safety behavior in mount/read/write/readdir/link/unlink paths.

## Summary
- Audit completed.
- Fixed during this pass:
  - `readdir` now skips deleted entries instead of terminating directory iteration early.
  - `symlink` allocation failure now rolls back inode allocation.
  - `link` now handles both Minix V1 and V2 link count fields correctly.
  - mount now accepts `MINIX_V2_Magic_14` in addition to existing Minix magics.
- Remaining high-risk design gaps still present (not fully solved in this pass):
  - zone allocator remains a mock counter, not bitmap-backed allocator.
  - append/extend behavior still depends on pre-existing zones (no full zone growth path).

## Findings

### F1 (Fixed): `readdir` stops on first deleted entry
- Severity: High
- Impact: directory traversal and lookup can fail when tombstones exist.
- Fix: iterate full directory file and return the Nth live (`inode != 0`) entry.

### F2 (Fixed): inode leak on `symlink` allocation failure
- Severity: Medium
- Impact: inode bitmap leak on failure path.
- Fix: free allocated inode when `kmalloc` for symlink inode wrapper fails.

### F3 (Fixed): `link` used V1 inode layout unconditionally
- Severity: High
- Impact: incorrect link count update on Minix V2 images.
- Fix: branch on filesystem magic and update V1/V2 inode link count fields safely.

### F4 (Fixed): mount rejected valid Minix V2 14-char magic
- Severity: Medium
- Impact: valid Minix V2 variant may fail to mount.
- Fix: include `MINIX_V2_Magic_14` in accepted magic set.

### F5 (Open): zone allocation is not bitmap-backed
- Severity: Critical
- Impact: potential zone reuse/corruption and no on-disk free-space accounting fidelity.
- Current state: `minix_alloc_zone()` uses monotonic counter.
- Recommendation: implement zmap scanning allocator with persistence and rollback semantics.

### F6 (Open): file growth path is incomplete
- Severity: High
- Impact: append/extend can fail silently where new zones should be allocated.
- Current state: write path breaks on holes without allocating.
- Recommendation: implement zone growth + indirect block allocation for V1/V2.

## Verification Notes
- Host-level compile/tests are required after this patch set:
  - `tests/sys/host_test_minix_vuln`
  - regression pass over directory traversal and link/unlink tests.
