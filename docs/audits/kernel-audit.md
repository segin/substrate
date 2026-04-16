# Security Audit Report: Substrate Kernel (`sys/`)

**Date:** April 16, 2026 (Updated: April 16, 2026 — Phase 3 FS driver scan)  
**Scope:** Full codebase review of `sys/` — kernel core (`kern/`), VM subsystem (`vm/`), process management (`pm/`), VFS/filesystems (`vfs/`, `fs/`), exec/personality (`exec/`), architecture code (`arch/`), drivers (`drivers/`), kernel libraries (`lib/`). Phase 3: Minix, FAT, UDF filesystem drivers.  
**Method:** Manual code review, cross-reference with AGENTS.md patterns, pattern analysis for unsafe operations. Phase 2: targeted deep scan of syscall boundaries, driver DMA paths, pmap/TLB, ext2 parsing, exec credentials. Phase 3: line-by-line review of minix.c/h, fat.c/h, udf.c/h, udf_write.c.  

## Summary

| Severity | Count | Resolved |
|----------|-------|----------|
| CRITICAL | 16 | 16 |
| HIGH     | 11 | 11 |
| MEDIUM   | 10 | 7 |
| LOW      | 5 | 5 |
| **Total** | **42** | **39** |

---

## CRITICAL ISSUES






## HIGH SEVERITY ISSUES

## MEDIUM SEVERITY ISSUES

### 30. Spinlock Panic on Double-Acquire: Non-Recoverable DoS — UNRESOLVED

**File:** [sys/kern/spinlock.c](sys/kern/spinlock.c#L14-L16)  
**Severity:** MEDIUM — Denial of Service

**Issue:** If a spinlock holder takes an interrupt that also tries to acquire the same spinlock, the kernel panics. While this is a legitimate deadlock detection, it's non-recoverable and a driver bug could take down the entire system.

**Fix:** Require `spinlock_acquire_irqsave()` for locks used in interrupt context (disable interrupts first). Consider a warning instead of panic for debug builds.

---

### 32. Name Cache: Stale Entries After Unlink/Rename — UNRESOLVED

**File:** [sys/vfs/vfs_cache.c](sys/vfs/vfs_cache.c)  
**Severity:** MEDIUM — Logic Error / Stale Data

**Issue:** After `unlink()` or `rename()`, the name cache may still return stale entries if `cache_purge()` is not called for all affected paths. This can cause phantom files to appear in lookups or deletions to appear to fail.

**Fix:** Implement generation-number-based invalidation, or ensure every VFS mutation path calls `cache_purge()` on affected vnodes.

---

### 35. Pipe Implementation: Potential Missed Wakeup — UNRESOLVED

**File:** [sys/fs/pipe.c](sys/fs/pipe.c)  
**Severity:** MEDIUM — Deadlock

**Issue:** The pipe sleep/wake pattern releases the mutex before yielding and re-acquires after. If a wakeup is delivered between the mutex release and the actual sleep, the thread misses the wakeup and sleeps indefinitely.

**Fix:** Use proper condvar semantics where the sleep is atomic with the mutex release.

---

## LOW SEVERITY ISSUES

---

---

## FALSE POSITIVES (ANALYZED AND REJECTED)

### FP-1. sys_brk PMM Address Confusion — NOT A BUG

The initial audit flagged the cleanup path in `sys_brk()`:
```c
pmm_free_block((void*)(pa_batch[k] + 0xC0000000));
```
However, `pa_batch[k]` stores physical addresses (`pa_virt - 0xC0000000`), and the cleanup correctly adds 0xC0000000 back to reconstruct the virtual address that `pmm_free_block()` expects. The conversion is consistent and correct.

### FP-2. Signal Bounds Check — NOT A BUG

`kern_sigaction()` checks `sig <= 0 || sig > NSIG` before accessing `sig_actions[sig - 1]`. Since `sig` can be at most `NSIG`, `sig - 1` is at most `NSIG - 1`, which is valid for the `NSIG`-sized array. The bounds check is correct.

### FP-3. Symlink Loop Detection — NOT A BUG

The symlink resolution in `vfs_lookup.c` correctly checks `nlink++ >= MAXSYMLINKS` and returns `ELOOP`. Symlink target buffers are allocated from the UMA zone and properly freed on error paths. The target + remaining path check (`target_len + rem_len >= 1024`) prevents buffer overflow. While multiple buffers are allocated on the heap (not stack), they are freed in each iteration.

### FP-4. vm_fault Uninitialized Page Leak — NOT A BUG

The page fault handler zeros all anonymous pages: the `VM_OBJ_TYPE_DEFAULT` path calls `page_zero()`, and the else branch (VNode type fallback for extended regions) also calls `page_zero()`. All paths to userspace mapping go through `page_zero()`.

### FP-5. Process Fields Missing Initialization — NOT A BUG

`proc_create()` calls `memset(proc, 0, sizeof(*proc))` early, zeroing all fields including `p_flag`, `state`, and `exit_code`. All fields are then explicitly initialized. The `memset` covers any fields not individually set.

---

## RECOMMENDED PRIORITY ORDER

1. **#12** — `sys_brk()` no upper bound — trivial kernel PTE overwrite from userspace, immediate ring 0 code exec
2. **#6** — `sys_set_thread_area()` TLS base — kernel read/write via GS segment, trivially exploitable
3. **#13, #14** — `fchmod`/`fchown` no permission checks — trivial root shell exploit chain
4. **#1, #2, #3, #16** — User/kernel boundary violations in `copyinstr`, futex — kernel information leak and crash vectors
5. **#4** — sysctl privilege escalation — trivially exploitable by any local user
6. **#15** — `kern_chroot` no privilege check + Linux personality copyin bypass — compound vulnerability
7. **#5** — ELF integer overflow — local privilege escalation via crafted binary
8. **#11** — ELF no setuid/setgid — all privilege-elevation binaries broken
9. **#7, #8** — pmap_fork TLB / deferred shootdown races — SMP memory corruption
10. **#9, #10** — VirtIO 9P DMA / ext2 BGD overflow — device and filesystem corruption
11. **#19** — `sys_get_robust_list()` arbitrary kernel write
12. **#17, #18** — Mutex/vm_object races — crash under load
13. **#27** — `sys_acct` no permission check — audit trail manipulation
14. **#20** — Fork ordering — zombie leaks
15. **#22** — sysctl_init SMP race — boot corruption
16. **#25, #26** — VFS vnode/mount races
17. Everything else in severity order

---

## Phase 3: Filesystem Driver Audit (Minix, FAT, UDF)

All Phase 3 findings have been resolved.
