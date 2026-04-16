# Security Audit Report: Substrate Kernel (`sys/`)

**Date:** April 16, 2026 (Updated: April 16, 2026 — Phase 3 FS driver scan)  
**Scope:** Full codebase review of `sys/` — kernel core (`kern/`), VM subsystem (`vm/`), process management (`pm/`), VFS/filesystems (`vfs/`, `fs/`), exec/personality (`exec/`), architecture code (`arch/`), drivers (`drivers/`), kernel libraries (`lib/`). Phase 3: Minix, FAT, UDF filesystem drivers.  
**Method:** Manual code review, cross-reference with AGENTS.md patterns, pattern analysis for unsafe operations. Phase 2: targeted deep scan of syscall boundaries, driver DMA paths, pmap/TLB, ext2 parsing, exec credentials. Phase 3: line-by-line review of minix.c/h, fat.c/h, udf.c/h, udf_write.c.  

## Summary

| Severity | Count | Resolved |
|----------|-------|----------|
| CRITICAL | 16 | 11 |
| HIGH     | 11 | 0 |
| MEDIUM   | 10 | 0 |
| LOW      | 5 | 0 |
| **Total** | **42** | **11** |

---

## CRITICAL ISSUES




### 5. ELF Loader: Integer Overflow in Segment Size Validation — UNRESOLVED

**File:** [sys/exec/formats/elf.c](sys/exec/formats/elf.c#L536-L541)  
**Severity:** CRITICAL — Privilege Escalation / Kernel Memory Write

**Issue:** The kernel-space boundary check for ELF segments evaluates `vaddr + phdr.p_memsz` which is a 32-bit unsigned addition that can overflow. While the code checks `(vaddr + phdr.p_memsz) < vaddr` to detect overflow, this check occurs in the same `||` expression as the `>= 0xC0000000` check. On some compiler optimization levels, the undefined behavior from overflow may cause the whole expression to be elided.

More critically, `segment_pages` is computed as `(va_end - va_start) / 0x1000` — if `va_end` wraps due to `p_memsz` being near `0xFFFFFFFF`, `va_end` becomes a small number, `segment_pages` becomes huge, and the page allocation loop will either exhaust memory or underflow.

**Problematic Code:**
```c
if (vaddr >= 0xC0000000 || (vaddr + phdr.p_memsz) >= 0xC0000000 || (vaddr + phdr.p_memsz) < vaddr) {
```

**Impact:** A crafted ELF binary could map segments into kernel address space, overwriting kernel code/data.

**Fix:** Check overflow explicitly before the addition:
```c
if (phdr.p_memsz > 0xFFFFFFFF - vaddr) {
    kprint("ELF: Segment size overflow\n");
    kfree(image, sizeof(*image));
    return 0;
}
if (vaddr >= 0xC0000000 || (vaddr + phdr.p_memsz) >= 0xC0000000) {
```

---

### 7. pmap_fork() Local-Only TLB Flush on SMP — COW Race — UNRESOLVED

**File:** [sys/arch/i386/pmap.c](sys/arch/i386/pmap.c#L616-L620)  
**Severity:** CRITICAL — Memory Corruption / Data Loss

**Issue:** `pmap_fork()` marks parent page table entries read-only for COW, then calls `pmap_invalidate_all()` which is a LOCAL-CPU-ONLY TLB flush. On SMP, other CPUs retain stale WRITABLE TLB entries for the parent's pages. A parent thread running on another CPU can write to a COW page without faulting, corrupting the child's shared physical page.

**Problematic Code:**
```c
if (cr3 == src_pmap->pdir_phys) {
    pmap_invalidate_all();  // LOCAL flush only! Other CPUs still have writable TLB entries
}
```

**Impact:** Silent data corruption between parent and child after fork. Parent writes to COW pages without triggering COW fault, corrupting child's memory.

**Fix:** Replace with `pmap_shootdown_all()` to IPI all CPUs:
```c
if (cr3 == src_pmap->pdir_phys) {
    pmap_shootdown_all();  // IPI all CPUs to flush stale writable entries
}
```

---

### 8. Deferred TLB Shootdown: Unprotected Static Globals on SMP — UNRESOLVED

**File:** [sys/arch/i386/pmap.c](sys/arch/i386/pmap.c#L1421-L1445)  
**Severity:** CRITICAL — TLB Corruption / Memory Access Violation

**Issue:** `pmap_shootdown_defer()` uses static global `deferred_pages[16]` and `deferred_count` with NO locking. On SMP, concurrent pmap operations on different CPUs will race on these globals, corrupting the deferred page list or missing entries entirely.

**Problematic Code:**
```c
static uint32_t deferred_pages[16];  // Global, no lock
static int deferred_count = 0;       // Global, no lock

void pmap_shootdown_defer(uintptr_t va) {
    if (deferred_count < 16) {
        deferred_pages[deferred_count++] = va;  // Two CPUs race here
    }
}
```

**Impact:** Missed TLB shootdowns cause stale TLB entries to persist, allowing access to freed/remapped physical pages. Can cause silent memory corruption.

**Fix:** Either make deferred state per-CPU (preferred), or protect with a spinlock:
```c
static DEFINE_PER_CPU(uint32_t, deferred_pages[16]);
static DEFINE_PER_CPU(int, deferred_count);
```

---

### 9. VirtIO 9P DMA Uses Virtual Addresses Instead of Physical — UNRESOLVED

**File:** [sys/drivers/virtio/virtio_9p.c](sys/drivers/virtio/virtio_9p.c#L85-L95)  
**Severity:** CRITICAL — DMA Corruption / Wrong Memory Access

**Issue:** `virtio_9p_send()` writes buffer pointers directly into vring descriptors without converting from virtual to physical addresses. VirtIO descriptors contain physical addresses for device DMA. The VirtIO block driver correctly converts (`(uint32_t)&hdr - 0xC0000000`), but the 9P driver does not.

**Problematic Code:**
```c
// WRONG: passes virtual address (>= 0xC0000000) to device
v9p.desc[id0].addr = (uint64_t)(uint32_t)out_buf;
v9p.desc[id1].addr = (uint64_t)(uint32_t)in_buf;
```

**Compare with VirtIO block (CORRECT):**
```c
vblk.desc[id0].addr = (uint64_t)((uint32_t)(uintptr_t)&hdr - 0xC0000000);
```

**Impact:** Device DMA hits wrong physical addresses. Reads return garbage; writes corrupt random physical memory. The 9P filesystem is completely non-functional due to this.

**Fix:**
```c
v9p.desc[id0].addr = (uint64_t)((uint32_t)(uintptr_t)out_buf - 0xC0000000);
v9p.desc[id1].addr = (uint64_t)((uint32_t)(uintptr_t)in_buf - 0xC0000000);
```

---

### 10. ext2: Block Group Descriptor Table Fixed-Size Array Overflow — UNRESOLVED

**File:** [sys/fs/ext2/ext2.c](sys/fs/ext2/ext2.c#L14-L15)  
**Severity:** CRITICAL — Heap/BSS Buffer Overflow

**Issue:** `ext2_bgd_table[64]` is a static array of 64 block group descriptors, but `group_count` is derived from the superblock's `s_blocks_count / s_blocks_per_group` with NO bounds check. A crafted ext2 filesystem with >64 block groups causes the BGD read loop to write past the array, corrupting adjacent BSS data.

**Problematic Code:**
```c
static ext2_group_desc_t ext2_bgd_table[64]; // Max 64 block groups

// Mount code:
ext2_fs.group_count = (ext2_fs.sb.s_blocks_count + ext2_fs.blocks_per_group - 1) / ext2_fs.blocks_per_group;
// No check: if (ext2_fs.group_count > 64) ...

uint32_t bgd_size = ext2_fs.group_count * sizeof(ext2_group_desc_t);
for (uint32_t i = 0; i < bgd_blocks && i < 2; i++) {
    ext2_read_block(&ext2_fs, bgd_block + i,
                   ((uint8_t *)ext2_bgd_table) + i * ext2_fs.block_size);
    // Writes up to 2 * block_size bytes into 64-entry array
}
```

**Impact:** BSS corruption from crafted filesystem image. Could be exploited via USB drive or disk image mount.

**Fix:**
```c
if (ext2_fs.group_count > 64) {
    kprint("EXT2: Too many block groups (%u > 64)\n", ext2_fs.group_count);
    return NULL;
}
```

---

### 11. ELF Loader: No setuid/setgid Credential Handling — UNRESOLVED

**File:** [sys/exec/formats/elf.c](sys/exec/formats/elf.c)  
**Severity:** CRITICAL — Privilege Escalation Mechanism Broken

**Issue:** The ELF loader and exec subsystem have NO handling for `S_ISUID` or `S_ISGID` file permission bits. There are zero references to `setuid`, `seteuid`, `S_ISUID`, or credential changes anywhere in `sys/exec/`. Setuid binaries (like `su`, `passwd`, `login`) execute with the caller's UID/GID, completely defeating the setuid mechanism.

**Impact:** No setuid/setgid support means `su`, `sudo`, `passwd`, `login`, and any other privilege-changing utility cannot function. Any program requiring elevated privileges via setuid is broken.

**Fix:** In the exec path, after loading the binary and before returning to userspace:
```c
if (vnode->v_mode & S_ISUID)
    current_process->euid = vnode->v_uid;
if (vnode->v_mode & S_ISGID)
    current_process->egid = vnode->v_gid;
```

---



## HIGH SEVERITY ISSUES

### 17. Mutex Adaptive Spin: Use-After-Free on Owner Thread — UNRESOLVED

**File:** [sys/kern/mutex.c](sys/kern/mutex.c#L66-L85)  
**Severity:** HIGH — Race Condition / Crash

**Issue:** The adaptive spin path loads `m->owner`, then dereferences `owner->state` without holding any lock. Between the load and the dereference, another CPU could release the mutex, the owner thread could exit and be freed, making `owner->state` a use-after-free.

**Problematic Code:**
```c
thread_t *owner = __atomic_load_n(&m->owner, __ATOMIC_ACQUIRE);
if (owner) {
    if (m->locked && __atomic_load_n(&m->owner, __ATOMIC_RELAXED) == owner) {
        if (owner->state != THREAD_RUNNING) {  // UAF if owner freed
```

**Fix:** Either hold a lock during the check, use thread reference counting, or simply remove the `owner->state` check from the fast path.

---

### 18. vm_object_deallocate() Race: Concurrent Teardown Without Lock — UNRESOLVED

**File:** [sys/vm/vm_object.c](sys/vm/vm_object.c#L55-L85)  
**Severity:** HIGH — Use-After-Free / Double-Free

**Issue:** After the atomic decrement reaches zero, the function tears down shadow, pager, and pages without any lock. If another CPU is concurrently accessing the object (e.g., via a page fault traversing the shadow chain), it could see partially-torn-down state.

**Problematic Code:**
```c
if (__sync_sub_and_fetch(&object->ref_count, 1) == 0) {
    vm_object_t *shadow = object->shadow;
    object->shadow = NULL;  // Race window: no lock held
    // ... free pages, shadow, pager
}
```

**Fix:** Add a per-object lock or use a global VM object lock for teardown, or use RCU-style deferred freeing.

---

### 19. sys_get_robust_list() Direct Userspace Write Without copyout() — UNRESOLVED

**File:** [sys/kern/futex.c](sys/kern/futex.c#L296-L300)  
**Severity:** HIGH — Kernel Crash / Arbitrary Kernel Write

**Issue:** Output values are written directly to userspace pointers without copyout():

**Problematic Code:**
```c
*head_ptr = target->robust_list;     // Direct write to userspace!
*len_ptr = target->robust_list_len;  // Direct write to userspace!
```

If `head_ptr` or `len_ptr` point to kernel space, this is an arbitrary kernel write.

**Fix:** Use `copyout()`:
```c
copyout(&target->robust_list, head_ptr, sizeof(*head_ptr));
copyout(&target->robust_list_len, len_ptr, sizeof(*len_ptr));
```

---

### 20. Fork: Child Visible to Scheduler Before proc_add_child() — UNRESOLVED

**File:** [sys/pm/process.c](sys/pm/process.c#L567-L575)  
**Severity:** HIGH — Resource Leak / Race Condition

**Issue:** `sched_fork_thread()` makes the child thread schedulable, but `proc_add_child()` is called afterward. If the child thread runs and exits (becoming a zombie) before `proc_add_child()`, the parent's `wait()` will never see it, causing a permanent zombie leak. Conversely, if the parent calls `wait()` between these two operations, it won't find the child.

**Problematic Code:**
```c
int fork_result = sched_fork_thread(child_proc, stack);  // Child is now schedulable!
// ... window where child could run and exit ...
proc_add_child(parent, child_proc);  // Only now visible to wait()
```

**Fix:** Add child to parent's child list BEFORE calling `sched_fork_thread()`, or keep the child in a `SIDL` (idle/new) state until all setup is complete.

---

### 21. IRQ Handler Dispatch: TOCTOU Between Lock Release and Handler Call — UNRESOLVED

**File:** [sys/kern/irq.c](sys/kern/irq.c) (dispatch function, not shown in read but inferred from structure)  
**Severity:** HIGH — Use-After-Free

**Issue:** The IRQ dispatch pattern (confirmed by the `free_irq` implementation) releases the spinlock before calling handlers. If `free_irq()` is called from a handler or concurrently, the `next` pointer saved before lock release may point to freed memory.

The `request_irq()` function also has a TOCTOU: it checks for conflicts under the lock, releases it, allocates memory, then re-acquires to insert. Another CPU could register the same IRQ in between.

**Fix:** Use RCU-style deferred freeing for irq_action entries, or keep the lock held during handler dispatch (with appropriate nesting considerations).

---

### 22. sysctl_init() Race: Double-Initialization on SMP — UNRESOLVED

**File:** [sys/kern/sysctl.c](sys/kern/sysctl.c#L78-L83)  
**Severity:** HIGH — Data Corruption

**Issue:** `sysctl_init()` uses a non-atomic check-and-set pattern:

**Problematic Code:**
```c
void sysctl_init(void) {
    if (sysctl_initialized) return;  // Non-atomic read
    sysctl_initialized = 1;          // Non-atomic write
    // ... mutex_init, register OIDs ...
```

On SMP, both CPUs could read `sysctl_initialized == 0`, both proceed, and `mutex_init()` would be called twice, corrupting the mutex.

**Fix:** Use atomic compare-and-swap:
```c
if (__sync_bool_compare_and_swap(&sysctl_initialized, 0, 1) == false)
    return;
```

---

### 23. ELF Segment Overlap Detection Silently Stops at 256 — UNRESOLVED

**File:** [sys/exec/formats/elf.c](sys/exec/formats/elf.c#L554-L558)  
**Severity:** HIGH — Privilege Escalation

**Issue:** The overlap detection array is fixed at 256 entries. If an ELF has more than 256 PT_LOAD segments, subsequent segments bypass overlap detection entirely, potentially allowing overlapping mappings that corrupt process memory:

**Problematic Code:**
```c
if (mapped_range_count < 256) {
    mapped_ranges[mapped_range_count].start = va_start;
    // ... silently drops tracking if >= 256
}
```

**Fix:** Reject ELF files with more than a reasonable number of PT_LOAD segments (e.g., 64), or dynamically allocate the tracking array.

---

### 24. Turnstile Allocation Failure: Silent Priority Inheritance Loss — UNRESOLVED

**File:** [sys/kern/turnstile.c](sys/kern/turnstile.c)  
**Severity:** HIGH — Priority Inversion / Deadlock

**Issue:** If turnstile allocation fails during `turnstile_block()`, the function silently returns without setting up priority inheritance. This breaks the invariant that priority-inheriting locks always boost the holder's priority, allowing unbounded priority inversion and potential deadlocks in real-time workloads.

**Fix:** Pre-allocate turnstiles per-thread at thread creation time (like FreeBSD), guaranteeing availability.

---

### 25. Vnode Reference Count Race in vref() — UNRESOLVED

**File:** [sys/vfs/vnode.c](sys/vfs/vnode.c)  
**Severity:** HIGH — Use-After-Free

**Issue:** `vref()` drops and re-acquires `v_interlock` around `vnode_freelist_remove()`. During this window, another CPU could recycle the vnode (usecount is still 0), causing a use-after-free when `vref()` re-acquires the lock and increments `v_usecount`.

**Fix:** Increment `v_usecount` BEFORE releasing the interlock, then remove from freelist. Or hold the freelist lock and interlock simultaneously.

---

### 26. Mount/Unmount Race: Check-Then-Set on v_mountedhere — UNRESOLVED

**File:** [sys/vfs/vfs_mount.c](sys/vfs/vfs_mount.c)  
**Severity:** HIGH — Data Structure Corruption

**Issue:** Mount checks `vp->v_mountedhere != NULL`, then later sets it. Without holding the vnode lock across both operations, two concurrent mounts on the same vnode could both pass the check and create a corrupted mount state.

**Fix:** Hold `vnode_lock(vp)` from the check through the assignment.

---

### 27. sys_acct() / kern_acct() Missing Root Permission Check — UNRESOLVED

**File:** [sys/kern/acct.c](sys/kern/acct.c#L40-L50)  
**Severity:** HIGH — Privilege Bypass / Audit Manipulation

**Issue:** `kern_acct()` enables or disables process accounting without checking the caller's UID/EUID. The source code literally contains the comment "In a real kernel, we would check permissions here" but no check was ever implemented. Any unprivileged user can enable accounting to an arbitrary file (writing process info) or disable it to hide activity.

**Problematic Code:**
```c
int kern_acct(const char *path) {
    // In a real kernel, we would check permissions here
    if (!path) {
        acct_stop();
        return 0;
    }
    return acct_start(path);
}
```

**Impact:** Unprivileged user can disable audit trail or redirect accounting data to an attacker-controlled file.

**Fix:**
```c
if (current_process->euid != 0)
    return -EPERM;
```

---

## MEDIUM SEVERITY ISSUES

### 28. Futex Robust List Walk: 4096-Iteration Kernel CPU Consumption — UNRESOLVED

**File:** [sys/kern/futex.c](sys/kern/futex.c#L200-L240)  
**Severity:** MEDIUM — Denial of Service

**Issue:** A malicious robust list with valid but circular pointers forces the kernel to iterate 4096 times during thread exit, consuming kernel CPU. While bounded, this is still significant during high-frequency thread exit.

**Fix:** Reduce `MAX_ROBUST_WALK` or add a time limit.

---

### 29. kmalloc Large Allocation: Secondary Integer Overflow in Pages Calculation — UNRESOLVED

**File:** [sys/vm/vm_kmem.c](sys/vm/vm_kmem.c#L116-L120)  
**Severity:** MEDIUM — Memory Corruption

**Issue:** While the initial `total < size` overflow check is correct, `(total + 4095) / 4096` can still produce 0 if `total` is 0 (from the overflow check failing silently). However, the code correctly returns NULL on overflow. The remaining risk: on architectures where `size_t` is 32-bit, `total = 0xFFFFF001 + 16 = 0xFFFFF011`, `pages = (0xFFFFF011 + 0xFFF) / 0x1000 = 0x100000`, requesting 4GB of pages — this won't succeed but wastes time.

**Fix:** Add a maximum allocation size check:
```c
if (size > KMEM_MAX_ALLOC) return NULL;
```

---

### 30. Spinlock Panic on Double-Acquire: Non-Recoverable DoS — UNRESOLVED

**File:** [sys/kern/spinlock.c](sys/kern/spinlock.c#L14-L16)  
**Severity:** MEDIUM — Denial of Service

**Issue:** If a spinlock holder takes an interrupt that also tries to acquire the same spinlock, the kernel panics. While this is a legitimate deadlock detection, it's non-recoverable and a driver bug could take down the entire system.

**Fix:** Require `spinlock_acquire_irqsave()` for locks used in interrupt context (disable interrupts first). Consider a warning instead of panic for debug builds.

---

### 31. Fork Failure: File Descriptor Leak on ldt_clone_process() Error — UNRESOLVED

**File:** [sys/pm/process.c](sys/pm/process.c#L510-L525)  
**Severity:** MEDIUM — Resource Leak

**Issue:** If `ldt_clone_process()` fails, `vm_map` and `pmap` are freed, but file descriptors that were already inherited (with incremented `f_count`) are not released:

**Problematic Code:**
```c
for(int j=0; j<MAX_FD; j++) {
    if (parent->fds[j]) {
        child_proc->fds[j] = parent->fds[j];
        child_proc->fds[j]->f_count++;  // Incremented here
    }
}
// ... later, if ldt_clone fails:
if (ldt_clone_process(child_proc, parent) != 0) {
    // vm_map and pmap cleaned up, but NOT file descriptors!
```

**Fix:** Add FD cleanup to the error path:
```c
for (int j = 0; j < MAX_FD; j++) {
    if (child_proc->fds[j]) {
        child_proc->fds[j]->f_count--;
        child_proc->fds[j] = NULL;
    }
}
```

---

### 32. Name Cache: Stale Entries After Unlink/Rename — UNRESOLVED

**File:** [sys/vfs/vfs_cache.c](sys/vfs/vfs_cache.c)  
**Severity:** MEDIUM — Logic Error / Stale Data

**Issue:** After `unlink()` or `rename()`, the name cache may still return stale entries if `cache_purge()` is not called for all affected paths. This can cause phantom files to appear in lookups or deletions to appear to fail.

**Fix:** Implement generation-number-based invalidation, or ensure every VFS mutation path calls `cache_purge()` on affected vnodes.

---

### 33. UDF FID Name Parsing: Unvalidated impl_use_length Offset — UNRESOLVED

**File:** [sys/fs/udf/udf.c](sys/fs/udf/udf.c)  
**Severity:** MEDIUM — Out-of-Bounds Read

**Issue:** When parsing File ID descriptors, the filename pointer is computed as `(char *)fid + 38 + fid->impl_use_length`. If `fid->impl_use_length` is crafted to be large (from a malicious UDF image), the pointer can read beyond the FID structure into unrelated kernel memory.

**Fix:** Validate that `38 + fid->impl_use_length + fid->file_id_length` does not exceed the FID's total length or the sector size.

---

### 34. FAT Cluster Chain: Missing Total-Clusters Bound Check — UNRESOLVED

**File:** [sys/fs/fat/fat.c](sys/fs/fat/fat.c)  
**Severity:** MEDIUM — Out-of-Bounds Read / Kernel Crash

**Issue:** When following cluster chains, the code checks for the EOC marker (≥ 0x0FFFFFFF) but does not validate that cluster numbers are within the valid range (2..total_clusters). A corrupted FAT table with large cluster values could cause sector calculations to overflow, reading from invalid device offsets.

**Fix:** Add `if (cluster < 2 || cluster > fs->total_clusters) return 0;` before each cluster dereference.

---

### 35. Pipe Implementation: Potential Missed Wakeup — UNRESOLVED

**File:** [sys/fs/pipe.c](sys/fs/pipe.c)  
**Severity:** MEDIUM — Deadlock

**Issue:** The pipe sleep/wake pattern releases the mutex before yielding and re-acquires after. If a wakeup is delivered between the mutex release and the actual sleep, the thread misses the wakeup and sleeps indefinitely.

**Fix:** Use proper condvar semantics where the sleep is atomic with the mutex release.

---

### 36. Request_irq TOCTOU: Gap Between Conflict Check and Insertion — UNRESOLVED

**File:** [sys/kern/irq.c](sys/kern/irq.c#L28-L60)  
**Severity:** MEDIUM — Race Condition

**Issue:** `request_irq()` checks for conflicts under `irq_lock`, releases the lock, calls `kmalloc()`, then re-acquires the lock to insert. Another CPU could register the same IRQ with conflicting flags in the gap.

**Problematic Code:**
```c
spinlock_acquire(&irq_lock);
// ... check for conflicts ...
spinlock_release(&irq_lock);   // Gap here!
action = kmalloc(sizeof(*action));
spinlock_acquire(&irq_lock);
action->next = irq_lines[irq]; // Insert without re-checking
spinlock_release(&irq_lock);
```

**Fix:** Pre-allocate the action structure, then do the check-and-insert atomically under one lock acquisition.

---

### 37. Process Group Link Copy Bug in Fork — UNRESOLVED

**File:** [sys/pm/process.c](sys/pm/process.c#L545-L556)  
**Severity:** MEDIUM — Linked List Corruption

**Issue:** Fork initially copies `p_pgrp_link` from parent, then overwrites it correctly. However, the initial copy creates a brief window where two processes share the same link pointer, which could corrupt the process group linked list if another CPU iterates it during this window.

**Problematic Code:**
```c
child_proc->p_pgrp_link = parent->p_pgrp_link; // WRONG copy first
// Then overwrite:
child_proc->p_pgrp_link = child_proc->p_pgrp->pg_members; // Correct
child_proc->p_pgrp->pg_members = child_proc;
```

**Fix:** Remove the initial stale copy; only set the correct link under `proctree_lock`.

---

## LOW SEVERITY ISSUES

### 38. Spinlock is_held() Non-Atomic Two-Read Pattern — UNRESOLVED

**File:** [sys/kern/spinlock.c](sys/kern/spinlock.c#L62-L65)  
**Severity:** LOW — Theoretical Race

**Issue:** `spinlock_is_held()` reads `locked` and `cpu_id` as two separate atomic loads. Between the two reads, the lock could be released and re-acquired by a different CPU, returning a false positive. In practice, this only matters for debug assertions.

---

### 39. Kernel printf itoa/utoa_hex: No Buffer Bounds Check in Internal Helpers — UNRESOLVED

**File:** [sys/lib/printf.c](sys/lib/printf.c#L10-L100)  
**Severity:** LOW — Buffer Overflow (Internal)

**Issue:** `itoa()` uses `char tmp[32]` and `utoa_hex()` uses `char tmp[32]`, which are sufficient for 64-bit values (max 20 decimal digits, 16 hex digits). However, neither function validates `i < sizeof(tmp)` in the conversion loop. While safe with current types, a future change to 128-bit types could overflow.

---

### 40. ELF Page Map Leak on pmap_enter Failure — UNRESOLVED

**File:** [sys/exec/formats/elf.c](sys/exec/formats/elf.c#L590-L600)  
**Severity:** LOW — Resource Leak

**Issue:** When `pmap_enter()` fails for a page in the middle of a segment, only the `page_maps` array is freed but the already-mapped pages from earlier in the loop are not unmapped or freed.

---

### 41. Random Number Generator State Not Wiped After Extraction — UNRESOLVED

**File:** [sys/kern/random.c](sys/kern/random.c)  
**Severity:** LOW — Theoretical State Recovery

**Issue:** ChaCha20 RNG state is not zeroed after generating random bytes. If the memory containing RNG state is freed and reallocated, previous outputs could theoretically be recovered.

---

### 42. elf_lookup_interpreter() May Return NULL to Caller — UNRESOLVED

**File:** [sys/exec/formats/elf.c](sys/exec/formats/elf.c#L326-L329)  
**Severity:** LOW — NULL Dereference

**Issue:** The final `return vfs_lookup(root, aliases[i].fallback)` in `elf_lookup_interpreter()` can return NULL. Callers should check the return value, and the function's contract should make NULL return clear.

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
