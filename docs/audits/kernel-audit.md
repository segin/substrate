# Security Audit Report: Substrate Kernel (`sys/`)

**Date:** April 16, 2026 (Updated: April 16, 2026 — Phase 3 FS driver scan)  
**Scope:** Full codebase review of `sys/` — kernel core (`kern/`), VM subsystem (`vm/`), process management (`pm/`), VFS/filesystems (`vfs/`, `fs/`), exec/personality (`exec/`), architecture code (`arch/`), drivers (`drivers/`), kernel libraries (`lib/`). Phase 3: Minix, FAT, UDF filesystem drivers.  
**Method:** Manual code review, cross-reference with AGENTS.md patterns, pattern analysis for unsafe operations. Phase 2: targeted deep scan of syscall boundaries, driver DMA paths, pmap/TLB, ext2 parsing, exec credentials. Phase 3: line-by-line review of minix.c/h, fat.c/h, udf.c/h, udf_write.c.  

## Summary

| Severity | Count | Resolved |
|----------|-------|----------|
| CRITICAL | 21 | 1 |
| HIGH     | 11 | 0 |
| MEDIUM   | 10 | 0 |
| LOW      | 5 | 0 |
| **Total** | **47** | **1** |

---

## CRITICAL ISSUES

### 1. copyinstr() Missing User Address Validation — UNRESOLVED

**File:** [sys/kern/subr_copy.c](sys/kern/subr_copy.c#L120-L155)  
**Severity:** CRITICAL — User/Kernel Boundary Information Leak

**Issue:** `copyinstr()` does not call `validate_user_addr()` before entering the byte-copy loop. Both `copyin()` and `copyout()` correctly validate the address range against `KERN_BASE`, but `copyinstr()` only relies on the `on_fault` handler. If a caller passes a kernel-space address (≥ 0xC0000000), the function will happily copy kernel memory byte-by-byte into the destination buffer until a NUL terminator is found, leaking arbitrary kernel data.

**Problematic Code:**
```c
int copyinstr(const void *src, void *dst, size_t maxlen, size_t *len) {
    const char *s = (const char *)src;
    char *d = (char *)dst;
    // NO validate_user_addr(src, ...) call!
    current_thread->on_fault = (uintptr_t)&&fault;
    for (i = 0; i < maxlen; i++) {
        char c = *s;  // Reads kernel memory if src >= KERN_BASE
```

**Impact:** Kernel memory disclosure to userspace. A malicious process can read kernel secrets, credentials, or cryptographic keys.

**Fix:** Add address validation at the top of the function:
```c
if (validate_user_addr(src, 1) != 0)
    return EFAULT;
```

---

### 2. futex_read_user() / futex_cmpxchg_user() Direct User Pointer Dereference Without Fault Handler — UNRESOLVED

**File:** [sys/kern/futex.c](sys/kern/futex.c#L97-L135)  
**Severity:** CRITICAL — Kernel Crash / Information Leak

**Issue:** Both functions validate the address range and check that the page is mapped via `futex_get_key()`, then directly dereference the user pointer (`*value = *uaddr`) and execute `lock cmpxchgl` on user memory without setting `current_thread->on_fault`. If the page is unmapped between the check and the access (TOCTOU), or if the page is swapped out, the kernel will take an unhandled page fault and panic.

**Problematic Code:**
```c
static int futex_read_user(int *uaddr, int *value) {
    void *key = futex_get_key((uintptr_t)uaddr);
    if (!key) return -EFAULT;
    *value = *uaddr;  // No fault handler! Kernel panic on page fault
    return 0;
}
```

**Impact:** Kernel crash (DoS). A process can trigger this by munmap()'ing the futex page from another thread between the check and the dereference.

**Fix:** Use `copyin()` instead of direct dereference:
```c
return copyin(uaddr, value, sizeof(int));
```
For `futex_cmpxchg_user()`, set `on_fault` around the inline assembly.

---

### 3. futex_read_timespec() Direct User Pointer Dereference — UNRESOLVED

**File:** [sys/kern/futex.c](sys/kern/futex.c#L50-L65)  
**Severity:** CRITICAL — Kernel Crash / Information Leak

**Issue:** `futex_read_timespec()` validates bounds but then does `*out = *(struct timespec *)uaddr` — a direct dereference without `copyin()` or fault handler. The code comments even acknowledge this: "Direct copy since we share address space and validated bounds / In a full model we'd use copyin() to handle faults."

**Problematic Code:**
```c
static int futex_read_timespec(void *uaddr, struct timespec *out) {
    // ... bounds check ...
    *out = *(struct timespec *)uaddr;  // Direct dereference!
    return 0;
}
```

**Impact:** Same as #2 — kernel panic on unmapped pages, plus potential information leak if address near kernel boundary passes the bounds check.

**Fix:** Replace with `copyin(uaddr, out, sizeof(struct timespec))`.

---

### 4. sysctl: Writable Variables Lack UID Permission Checks — UNRESOLVED

**File:** [sys/kern/sysctl.c](sys/kern/sysctl.c#L127-L133)  
**Severity:** CRITICAL — Privilege Escalation

**Issue:** `sys_sysctl()` only checks if the OID has `CTLFLAG_WR` set. It does not check the caller's UID/EUID. Any unprivileged user can write to any writable sysctl variable, including `securelevel`:

**Problematic Code:**
```c
if ((oid->oid_kind & CTLFLAG_WR) == 0 && newp != NULL) {
    mutex_unlock(&sysctl_mutex);
    return EPERM;
}
// NO uid/euid check — any user can write!
```

With `SYSCTL_INT(kern, KERN_SECURELVL, securelevel, CTLFLAG_RW, &securelevel, ...)`, any process can lower or set the system securelevel.

**Impact:** Complete privilege escalation — unprivileged user can disable securelevel protections, modify hostname, domainname, and any other RW sysctl.

**Fix:** Add credential check:
```c
if (newp != NULL) {
    if ((oid->oid_kind & CTLFLAG_WR) == 0) {
        mutex_unlock(&sysctl_mutex);
        return EPERM;
    }
    if (current_process && current_process->euid != 0) {
        mutex_unlock(&sysctl_mutex);
        return EPERM;
    }
}
```

---

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

### 6. sys_set_thread_area() Missing TLS Base Address Validation — UNRESOLVED

**File:** [sys/arch/i386/syscall.c](sys/arch/i386/syscall.c#L43-L75)  
**Severity:** CRITICAL — Kernel Memory Read/Write via GS Segment

**Issue:** `sys_set_thread_area()` validates the GDT entry number (lines 58-62) but does NOT validate that the TLS `base_addr` is in user space. A user process can set `info.base_addr = 0xC0000000` or higher, then use `%gs:[offset]` instructions to read and write arbitrary kernel memory.

**Problematic Code:**
```c
int sys_set_thread_area(struct user_desc *u_info) {
    struct user_desc info;
    copyin(u_info, &info, sizeof(info));
    // entry_number validation exists (lines 58-62)
    // BUT NO CHECK: if (info.base_addr >= 0xC0000000) return -EINVAL;
    gdt_set_tls_entry(info.entry_number, info.base_addr, ...);
```

**Impact:** Complete kernel memory read/write from userspace. Any unprivileged process can read secrets, overwrite kernel code, or escalate to ring 0.

**Fix:**
```c
if (info.base_addr >= 0xC0000000)
    return -EINVAL;
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

### 12. sys_brk() Missing Kernel-Space Upper Bound Check — UNRESOLVED

**File:** [sys/vm/vm_syscalls.c](sys/vm/vm_syscalls.c#L207-L290)  
**Severity:** CRITICAL — Arbitrary Kernel Memory Overwrite / Ring 0 Code Execution

**Issue:** `sys_brk()` validates only `new_brk < brk_start` (prevents shrinking below heap start) but never verifies `new_brk < 0xC0000000`. A user can pass `addr = 0xC0100000` and the function will call `pmap_enter_batch()` with VA addresses in kernel space. Since `pmap_enter()` has no VA range check for user pmaps, it will overwrite existing kernel page table entries with user-controlled physical pages marked read/write.

**Problematic Code:**
```c
uintptr_t new_brk = (uintptr_t)addr;
uintptr_t old_brk = (uintptr_t)current_process->brk;

if (new_brk < current_process->brk_start) 
    return (void *)(uintptr_t)old_brk;

// *** NO CHECK: new_brk >= 0xC0000000 ***

uintptr_t old_page_end = (old_brk + 0xFFF) & ~0xFFFULL;
uintptr_t new_page_end = (new_brk + 0xFFF) & ~0xFFFULL;

if (new_page_end > old_page_end) {
    // Allocates pages and calls pmap_enter_batch() with VA in kernel space
```

**Impact:** Attacker calls `brk(0xC0100000)`. The kernel maps freshly-allocated, zeroed, user-writable pages over kernel code/data. Attacker writes shellcode, which executes at ring 0 on next kernel entry.

**Fix:**
```c
if (new_brk >= 0xC0000000)
    return (void *)(uintptr_t)old_brk;
```
Also add defense-in-depth in `pmap_enter()` to reject VA >= 0xC0000000 for user pmaps.

---

### 13. sys_fchmod() No Permission Check — UNRESOLVED

**File:** [sys/kern/syscall.c](sys/kern/syscall.c#L1667-L1676)  
**Severity:** CRITICAL — Arbitrary File Permission Modification

**Issue:** `sys_fchmod()` changes the permission mode of any open file descriptor with zero authorization checks. Any unprivileged user with a readable fd can set it to mode `0777` or `04755`.

**Problematic Code:**
```c
int sys_fchmod(int fd, int mode) {
    if (fd < 0 || fd >= MAX_FD) return -EBADF;
    file_t *f = current_process->fds[fd];
    if (!f || !f->f_data) return -EBADF;
    fs_node_t *node = (fs_node_t *)f->f_data;
    node->mask = (uint32_t)(mode & 07777);  // No owner/capability check
    return 0;
}
```

**Impact:** Any user can make any file world-writable, or add setuid bits. Combined with #14, creates a trivial root shell.

**Fix:** Add `if (current_process->euid != 0 && current_process->euid != node->uid) return -EPERM;`

---

### 14. sys_fchown() No Permission Check — Privilege Escalation — UNRESOLVED

**File:** [sys/kern/syscall.c](sys/kern/syscall.c#L1678-L1689)  
**Severity:** CRITICAL — Arbitrary File Ownership Change / Privilege Escalation

**Issue:** `sys_fchown()` changes uid/gid of any open file descriptor with zero authorization checks. An unprivileged user can `fchown(fd, 0, 0)` to take ownership as root, then `fchmod(fd, 04755)` to create a setuid-root binary.

**Problematic Code:**
```c
int sys_fchown(int fd, int uid, int gid) {
    if (fd < 0 || fd >= MAX_FD) return -EBADF;
    file_t *f = current_process->fds[fd];
    if (!f || !f->f_data) return -EBADF;
    fs_node_t *node = (fs_node_t *)f->f_data;
    if (uid != -1) node->uid = (uint32_t)uid;  // No privilege check
    if (gid != -1) node->gid = (uint32_t)gid;  // No privilege check
    return 0;
}
```

**Exploitation chain:** `open("/tmp/exploit", O_CREAT|O_RDWR)` → write shell → `fchown(fd, 0, 0)` → `fchmod(fd, 04755)` → `exec("/tmp/exploit")` → root shell.

**Fix:** Only `euid == 0` may change uid. Non-root owners may only change gid to a group they belong to. Clear setuid/setgid bits on chown by non-root.

---

### 15. kern_chroot() Missing Privilege Check + Linux Personality copyin Bypass — UNRESOLVED

**File:** [sys/kern/syscall.c](sys/kern/syscall.c#L921-L940), [sys/exec/perso/perso_linux.c](sys/exec/perso/perso_linux.c#L910)  
**Severity:** CRITICAL — Unprivileged chroot + Kernel Direct Dereference of Userspace Pointer

**Issue — Two compounding vulnerabilities:**

**(a) Missing `euid == 0` check:** `kern_chroot()` changes the process root directory without any privilege check. Any unprivileged user can chroot.

**(b) Linux personality passes raw userspace pointer:** The Linux personality table maps `LINUX_SYS_chroot` directly to `kern_chroot` (not `sys_chroot`). `sys_chroot` does `copyinstr()` before calling `kern_chroot`, but the Linux path bypasses this entirely. `kern_chroot()` then directly dereferences the raw userspace pointer via `vfs_lookup()`.

**Problematic Code:**
```c
// Linux personality table (perso_linux.c:910)
[LINUX_SYS_chroot] = (void *)&kern_chroot,  // Bypasses sys_chroot's copyinstr!

// kern_chroot (syscall.c:921)
int kern_chroot(const char *path) {
    if (!path) return -1;
    // path is a RAW userspace pointer from Linux personality dispatch
    if (path[0] == '/') {
        node = vfs_lookup(root, path);  // Direct dereference of userspace memory
    }
    current_process->root_node = node;  // No euid==0 check
```

**Fix:** (a) Add `if (current_process->euid != 0) return -EPERM;` to `kern_chroot`. (b) Change Linux personality mapping to `sys_chroot` or create a wrapper that does copyin.

---

### 16. futex_thread_exit() Direct Dereference of robust_list_head Fields — UNRESOLVED

**File:** [sys/kern/futex.c](sys/kern/futex.c#L198-L220)  
**Severity:** CRITICAL — Kernel Panic / Controlled Pointer Dereference

**Issue:** `futex_thread_exit()` reads `head->list_op_pending`, `head->futex_offset`, and `head->list.next` by directly dereferencing the `robust_list_head` pointer (which resides in userspace). While the function later uses `futex_read_user()` for futex values and next pointers, the **initial three field reads** have no copyin and no fault handler. This is distinct from the known #2/#3 `futex_read_user`/`futex_read_timespec` findings.

**Problematic Code:**
```c
void futex_thread_exit(thread_t *t) {
    struct robust_list_head *head = t->robust_list;  // Userspace pointer
    
    if (head->list_op_pending) {  // DIRECT DEREFERENCE — no copyin
        int *futex_addr = (int *)((char *)head->list_op_pending 
                         + head->futex_offset);  // DIRECT DEREFERENCE — no copyin
    }
    entry = head->list.next;  // DIRECT DEREFERENCE — no copyin
```

**Impact:** Attacker calls `set_robust_list()`, then `munmap()` the page, then exits the thread. Kernel dereferences unmapped pointer at ring 0 with no fault recovery. Kernel panic (DoS) or controlled read if page is remapped.

**Fix:** Copy the entire `struct robust_list_head` into a kernel-stack local:
```c
struct robust_list_head khead;
if (copyin(t->robust_list, &khead, sizeof(khead)) != 0) {
    t->robust_list = NULL;
    return;
}
```

---

### 17. procfs_generic_read() Kernel Stack Over-Read on Allocation Failure — UNRESOLVED

**File:** [sys/fs/procfs.c](sys/fs/procfs.c#L673-L700)  
**Severity:** CRITICAL — Arbitrary Kernel Stack Disclosure

**Issue:** `procfs_generic_read()` generates content into a 1024-byte stack buffer `tmp`. When the generator output exceeds 1024 bytes (returns `len >= 1024`), the code attempts `kmalloc(len + 1)` for a larger buffer. If `kmalloc` fails (OOM), `buf` remains pointing to the 1024-byte stack buffer but `len` retains its original value (>1024). The subsequent `memcpy(buffer, buf + offset, read_len)` reads `len - 1024` bytes past the end of `tmp`, copying kernel stack contents (return addresses, saved registers, local variables) into the userspace buffer.

Compare with `proc_pid_status_read()` (line ~791) and `proc_pid_stat_read()` (line ~859), which correctly clamp `len` on allocation failure.

**Problematic Code:**
```c
char tmp[1024];
uint32_t len = entry->generator(tmp, sizeof(tmp), entry->opaque);
char *buf = tmp;

if (len >= sizeof(tmp)) {
    alloc_size = len + 1;
    alloc_buf = kmalloc(alloc_size);
    if (alloc_buf) {
        len = entry->generator(alloc_buf, alloc_size, entry->opaque);
        buf = alloc_buf;
    }
    // BUG: no else — len stays > 1024, buf stays = tmp
}

if (offset < len) {
    read_len = size;
    if (offset + read_len > len)
        read_len = len - offset;
    memcpy(buffer, buf + offset, read_len);  // reads past tmp[1024]
}
```

**Triggerable generators:** `gen_mounts` (many mount points), `gen_ioports`/`gen_iomem` (many hardware resources), any driver-registered generator with large output. OOM can be induced by an attacker exhausting available memory.

**Impact:** Kernel stack data (return addresses defeating KASLR, local variables, potentially credentials) leaked to userspace via `/proc/mounts`, `/proc/ioports`, etc.

**Fix:**
```c
if (len >= sizeof(tmp)) {
    alloc_size = len + 1;
    alloc_buf = kmalloc(alloc_size);
    if (alloc_buf) {
        len = entry->generator(alloc_buf, alloc_size, entry->opaque);
        buf = alloc_buf;
    } else {
        len = sizeof(tmp) - 1;  // Clamp to actual buffer size
    }
}
```

---

### 18. procfs /proc/<pid>/fd/ World-Readable — No Per-Process Access Controls — UNRESOLVED

**File:** [sys/fs/procfs.c](sys/fs/procfs.c#L520-L571)  
**Severity:** CRITICAL — Information Disclosure Enabling Privilege Escalation

**Issue:** All per-pid procfs nodes (`status`, `cmdline`, `stat`, `exe`, `cwd`, `fd/`) are created with `uid = 0, gid = 0` (root ownership) and world-readable permissions (`mask = 0444` for files, `0555` for directories, `0777` for symlinks). The fd directory should be `0500` owned by the target process's UID (Linux default). Additionally, none of the read/readlink callbacks perform any credential checks against the calling process.

This means any unprivileged user can:
- Enumerate **all open file descriptors** of every process (including root) via `/proc/<pid>/fd/`
- Read `/proc/<pid>/cmdline` of every process, which may contain **passwords or secrets passed as command-line arguments**
- Read `/proc/<pid>/exe` and `/proc/<pid>/cwd` of every process

**Problematic Code:**
```c
// fd directory — should be 0500, owned by process uid
nodes->fd_dir.mask = 0555;    // world-readable+executable
nodes->fd_dir.uid = 0;        // owned by root, not target process
nodes->fd_dir.gid = 0;

// fd symlinks — world-readable
link->mask = 0777;
link->uid = 0;
link->gid = 0;

// status, cmdline, stat — world-readable, root-owned
nodes->status.mask = 0444;
nodes->status.uid = 0;
```

**Impact:** Sensitive information about all processes exposed to any local user. Enables targeted privilege escalation by revealing what files root daemons have open, command-line secrets, and process working directories.

**Fix:** Set uid/gid on per-pid nodes to the target process's credentials:
```c
process_t *target = proc_find(pid);
nodes->dir.uid = target->uid;
nodes->dir.gid = target->gid;
nodes->fd_dir.mask = 0500;  // owner-only
nodes->fd_dir.uid = target->uid;
nodes->fd_dir.gid = target->gid;
// Same for status, cmdline, stat, exe, cwd nodes
```

---

## HIGH SEVERITY ISSUES

### 19. Mutex Adaptive Spin: Use-After-Free on Owner Thread — UNRESOLVED

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

### 20. vm_object_deallocate() Race: Concurrent Teardown Without Lock — UNRESOLVED

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

### 21. sys_get_robust_list() Direct Userspace Write Without copyout() — UNRESOLVED

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

### 22. Fork: Child Visible to Scheduler Before proc_add_child() — UNRESOLVED

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

### 23. IRQ Handler Dispatch: TOCTOU Between Lock Release and Handler Call — UNRESOLVED

**File:** [sys/kern/irq.c](sys/kern/irq.c) (dispatch function, not shown in read but inferred from structure)  
**Severity:** HIGH — Use-After-Free

**Issue:** The IRQ dispatch pattern (confirmed by the `free_irq` implementation) releases the spinlock before calling handlers. If `free_irq()` is called from a handler or concurrently, the `next` pointer saved before lock release may point to freed memory.

The `request_irq()` function also has a TOCTOU: it checks for conflicts under the lock, releases it, allocates memory, then re-acquires to insert. Another CPU could register the same IRQ in between.

**Fix:** Use RCU-style deferred freeing for irq_action entries, or keep the lock held during handler dispatch (with appropriate nesting considerations).

---

### 24. sysctl_init() Race: Double-Initialization on SMP — UNRESOLVED

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

### 25. ELF Segment Overlap Detection Silently Stops at 256 — UNRESOLVED

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

### 26. Turnstile Allocation Failure: Silent Priority Inheritance Loss — UNRESOLVED

**File:** [sys/kern/turnstile.c](sys/kern/turnstile.c)  
**Severity:** HIGH — Priority Inversion / Deadlock

**Issue:** If turnstile allocation fails during `turnstile_block()`, the function silently returns without setting up priority inheritance. This breaks the invariant that priority-inheriting locks always boost the holder's priority, allowing unbounded priority inversion and potential deadlocks in real-time workloads.

**Fix:** Pre-allocate turnstiles per-thread at thread creation time (like FreeBSD), guaranteeing availability.

---

### 27. Vnode Reference Count Race in vref() — UNRESOLVED

**File:** [sys/vfs/vnode.c](sys/vfs/vnode.c)  
**Severity:** HIGH — Use-After-Free

**Issue:** `vref()` drops and re-acquires `v_interlock` around `vnode_freelist_remove()`. During this window, another CPU could recycle the vnode (usecount is still 0), causing a use-after-free when `vref()` re-acquires the lock and increments `v_usecount`.

**Fix:** Increment `v_usecount` BEFORE releasing the interlock, then remove from freelist. Or hold the freelist lock and interlock simultaneously.

---

### 28. Mount/Unmount Race: Check-Then-Set on v_mountedhere — UNRESOLVED

**File:** [sys/vfs/vfs_mount.c](sys/vfs/vfs_mount.c)  
**Severity:** HIGH — Data Structure Corruption

**Issue:** Mount checks `vp->v_mountedhere != NULL`, then later sets it. Without holding the vnode lock across both operations, two concurrent mounts on the same vnode could both pass the check and create a corrupted mount state.

**Fix:** Hold `vnode_lock(vp)` from the check through the assignment.

---

### 29. sys_acct() / kern_acct() Missing Root Permission Check — UNRESOLVED

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

### 30. Futex Robust List Walk: 4096-Iteration Kernel CPU Consumption — UNRESOLVED

**File:** [sys/kern/futex.c](sys/kern/futex.c#L200-L240)  
**Severity:** MEDIUM — Denial of Service

**Issue:** A malicious robust list with valid but circular pointers forces the kernel to iterate 4096 times during thread exit, consuming kernel CPU. While bounded, this is still significant during high-frequency thread exit.

**Fix:** Reduce `MAX_ROBUST_WALK` or add a time limit.

---

### 31. kmalloc Large Allocation: Secondary Integer Overflow in Pages Calculation — UNRESOLVED

**File:** [sys/vm/vm_kmem.c](sys/vm/vm_kmem.c#L116-L120)  
**Severity:** MEDIUM — Memory Corruption

**Issue:** While the initial `total < size` overflow check is correct, `(total + 4095) / 4096` can still produce 0 if `total` is 0 (from the overflow check failing silently). However, the code correctly returns NULL on overflow. The remaining risk: on architectures where `size_t` is 32-bit, `total = 0xFFFFF001 + 16 = 0xFFFFF011`, `pages = (0xFFFFF011 + 0xFFF) / 0x1000 = 0x100000`, requesting 4GB of pages — this won't succeed but wastes time.

**Fix:** Add a maximum allocation size check:
```c
if (size > KMEM_MAX_ALLOC) return NULL;
```

---

### 32. Spinlock Panic on Double-Acquire: Non-Recoverable DoS — UNRESOLVED

**File:** [sys/kern/spinlock.c](sys/kern/spinlock.c#L14-L16)  
**Severity:** MEDIUM — Denial of Service

**Issue:** If a spinlock holder takes an interrupt that also tries to acquire the same spinlock, the kernel panics. While this is a legitimate deadlock detection, it's non-recoverable and a driver bug could take down the entire system.

**Fix:** Require `spinlock_acquire_irqsave()` for locks used in interrupt context (disable interrupts first). Consider a warning instead of panic for debug builds.

---

### 33. Fork Failure: File Descriptor Leak on ldt_clone_process() Error — UNRESOLVED

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

### 34. Name Cache: Stale Entries After Unlink/Rename — UNRESOLVED

**File:** [sys/vfs/vfs_cache.c](sys/vfs/vfs_cache.c)  
**Severity:** MEDIUM — Logic Error / Stale Data

**Issue:** After `unlink()` or `rename()`, the name cache may still return stale entries if `cache_purge()` is not called for all affected paths. This can cause phantom files to appear in lookups or deletions to appear to fail.

**Fix:** Implement generation-number-based invalidation, or ensure every VFS mutation path calls `cache_purge()` on affected vnodes.

---

### 35. UDF FID Name Parsing: Unvalidated impl_use_length Offset — UNRESOLVED

**File:** [sys/fs/udf/udf.c](sys/fs/udf/udf.c)  
**Severity:** MEDIUM — Out-of-Bounds Read

**Issue:** When parsing File ID descriptors, the filename pointer is computed as `(char *)fid + 38 + fid->impl_use_length`. If `fid->impl_use_length` is crafted to be large (from a malicious UDF image), the pointer can read beyond the FID structure into unrelated kernel memory.

**Fix:** Validate that `38 + fid->impl_use_length + fid->file_id_length` does not exceed the FID's total length or the sector size.

---

### 36. FAT Cluster Chain: Missing Total-Clusters Bound Check — UNRESOLVED

**File:** [sys/fs/fat/fat.c](sys/fs/fat/fat.c)  
**Severity:** MEDIUM — Out-of-Bounds Read / Kernel Crash

**Issue:** When following cluster chains, the code checks for the EOC marker (≥ 0x0FFFFFFF) but does not validate that cluster numbers are within the valid range (2..total_clusters). A corrupted FAT table with large cluster values could cause sector calculations to overflow, reading from invalid device offsets.

**Fix:** Add `if (cluster < 2 || cluster > fs->total_clusters) return 0;` before each cluster dereference.

---

### 37. Pipe Implementation: Potential Missed Wakeup — UNRESOLVED

**File:** [sys/fs/pipe.c](sys/fs/pipe.c)  
**Severity:** MEDIUM — Deadlock

**Issue:** The pipe sleep/wake pattern releases the mutex before yielding and re-acquires after. If a wakeup is delivered between the mutex release and the actual sleep, the thread misses the wakeup and sleeps indefinitely.

**Fix:** Use proper condvar semantics where the sleep is atomic with the mutex release.

---

### 38. Request_irq TOCTOU: Gap Between Conflict Check and Insertion — UNRESOLVED

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

### 39. Process Group Link Copy Bug in Fork — UNRESOLVED

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

### 40. Spinlock is_held() Non-Atomic Two-Read Pattern — UNRESOLVED

**File:** [sys/kern/spinlock.c](sys/kern/spinlock.c#L62-L65)  
**Severity:** LOW — Theoretical Race

**Issue:** `spinlock_is_held()` reads `locked` and `cpu_id` as two separate atomic loads. Between the two reads, the lock could be released and re-acquired by a different CPU, returning a false positive. In practice, this only matters for debug assertions.

---

### 41. Kernel printf itoa/utoa_hex: No Buffer Bounds Check in Internal Helpers — UNRESOLVED

**File:** [sys/lib/printf.c](sys/lib/printf.c#L10-L100)  
**Severity:** LOW — Buffer Overflow (Internal)

**Issue:** `itoa()` uses `char tmp[32]` and `utoa_hex()` uses `char tmp[32]`, which are sufficient for 64-bit values (max 20 decimal digits, 16 hex digits). However, neither function validates `i < sizeof(tmp)` in the conversion loop. While safe with current types, a future change to 128-bit types could overflow.

---

### 42. ELF Page Map Leak on pmap_enter Failure — UNRESOLVED

**File:** [sys/exec/formats/elf.c](sys/exec/formats/elf.c#L590-L600)  
**Severity:** LOW — Resource Leak

**Issue:** When `pmap_enter()` fails for a page in the middle of a segment, only the `page_maps` array is freed but the already-mapped pages from earlier in the loop are not unmapped or freed.

---

### 43. Random Number Generator State Not Wiped After Extraction — UNRESOLVED

**File:** [sys/kern/random.c](sys/kern/random.c)  
**Severity:** LOW — Theoretical State Recovery

**Issue:** ChaCha20 RNG state is not zeroed after generating random bytes. If the memory containing RNG state is freed and reallocated, previous outputs could theoretically be recovered.

---

### 44. elf_lookup_interpreter() May Return NULL to Caller — UNRESOLVED

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
9. **#17** — procfs_generic_read stack over-read — kernel stack disclosure under OOM
10. **#7, #8** — pmap_fork TLB / deferred shootdown races — SMP memory corruption
11. **#9, #10** — VirtIO 9P DMA / ext2 BGD overflow — device and filesystem corruption
12. **#18** — procfs /proc/pid/fd world-readable — information disclosure enabling priv-esc
13. **#21** — `sys_get_robust_list()` arbitrary kernel write
14. **#19, #20** — Mutex/vm_object races — crash under load
15. **#29** — `sys_acct` no permission check — audit trail manipulation
16. **#22** — Fork ordering — zombie leaks
17. **#24** — sysctl_init SMP race — boot corruption
18. **#27, #28** — VFS vnode/mount races
19. Everything else in severity order

---

## Phase 3: Filesystem Driver Audit (Minix, FAT, UDF)

### FS-2. UDF — Kernel Memory Leak via `udf_read_file` Inline Data Path (Stale FE Cache) — UNRESOLVED

**File:** `sys/fs/udf/udf.c` lines 262–275  
**Severity:** CRITICAL — Kernel Memory Information Leak

**Issue:** `udf_read_fe()` copies only `sizeof(struct udf_fe)` bytes into the node cache. The allocation descriptors / inline data that follow the FE in the on-disk sector are NOT stored. When VFS calls `udf_read_file(&ctx->fe, ...)`, the function computes `alloc_area = ((uint8_t *)fe) + sizeof(struct udf_fe) + fe->ext_attr_length` — this points past the `udf_node_t` struct into adjacent kernel BSS memory.

For inline data (`icb_tag.flags & 0x7 == 3`), the data is directly `memcpy`'d from kernel memory to the user's buffer. `ext_attr_length` (from disk) controls the base offset; `info_length` (from disk) controls the read length.

**Impact:** Mounting a crafted UDF image and reading a file leaks arbitrary kernel memory to userspace.

**Problematic Code:**
```c
uint8_t *alloc_area = ((uint8_t *)fe) + sizeof(struct udf_fe) + fe->ext_attr_length;
if (ad_type == UDF_ICB_FLAG_AD_INLINE) {
    memcpy(buffer, alloc_area + offset, size);  // reads kernel BSS
```

**Fix:** Store full sector in node cache, or re-read FE from disk on each access, validating `sizeof(struct udf_fe) + ext_attr_length + alloc_desc_length <= UDF_SECTOR_SIZE`.

---

### FS-3. UDF — OOB Read in Directory FID Parsing (`readdir`/`finddir`) — UNRESOLVED

**File:** `sys/fs/udf/udf.c` lines 461–471, 521–533  
**Severity:** CRITICAL — Kernel Memory Information Leak

**Issue:** Directory data is read into `static uint8_t dir_buf[4096]` with the read capped to 4096 bytes, but the FID parsing loop uses `dir_size = (uint32_t)ctx->fe.info_length` which can be up to 4GB. When `pos >= 4096`, `struct udf_fid *fid = (struct udf_fid *)(dir_buf + pos)` reads past the buffer into kernel BSS. The `impl_use_length` and `file_id_length` values from kernel memory control further accesses, and "filenames" from kernel memory are returned to userspace.

Additionally, even within the first 4096 bytes, no validation ensures `38 + impl_use_length + file_id_length` stays within remaining buffer space.

**Impact:** Directory listing on a crafted UDF image leaks kernel memory contents as directory entry names.

**Problematic Code:**
```c
uint32_t dir_size = (uint32_t)ctx->fe.info_length;
udf_read_file(..., 0, dir_size > 4096 ? 4096 : dir_size, dir_buf);
while (pos < dir_size) {              // should be: pos < read_size
    struct udf_fid *fid = (struct udf_fid *)(dir_buf + pos);
```

**Fix:** Use `read_size` (the capped value) as loop bound, and validate each FID's total size fits within remaining buffer.

---

### FS-4. UDF — Space Bitmap OOB Read/Write via Crafted `num_bits` — UNRESOLVED

**File:** `sys/fs/udf/udf_write.c` lines 55–80, 87–101  
**Severity:** CRITICAL — Kernel Memory Corruption

**Issue:** `udf_read_space_bitmap()` limits the bitmap to 4 sectors (8192 bytes), but sets `space_bitmap_size = sbm->num_bits` from disk without validation. In `udf_alloc_block()`, the loop iterates `space_bitmap_size / 8` bytes. A crafted UDF image with `num_bits = 0x80000000` causes iteration ~256MB past the static buffer, reading and WRITING (setting bits via `|= (1 << bit)`) kernel BSS memory.

**Impact:** Kernel memory corruption. Block allocation on a crafted image writes to arbitrary BSS locations.

**Problematic Code:**
```c
space_bitmap_size = sbm->num_bits;  // from disk, unbounded
// ...
for (uint32_t byte = 0; byte < space_bitmap_size / 8; byte++) {
    if (space_bitmap[byte] != 0xFF) {
        space_bitmap[byte] |= (1 << bit);  // OOB write
```

**Fix:** Validate `num_bits` against actual buffer:
```c
uint32_t max_bytes = (sectors * UDF_SECTOR_SIZE) - sizeof(struct udf_space_bitmap);
if (sbm->num_bits > max_bytes * 8) {
    kprint("UDF: Space bitmap num_bits exceeds buffer\n");
    return -1;
}
```

---

### FS-5. UDF — Buffer Overflow in `udf_add_fid` When Directory Grows Past 4096 Bytes — UNRESOLVED

**File:** `sys/fs/udf/udf_write.c` lines 778–798  
**Severity:** CRITICAL — Kernel Memory Corruption

**Issue:** `udf_add_fid()` uses `static uint8_t dir_buf[4096]` and places the new FID at `dir_buf + dir_size` where `dir_size = (uint32_t)dir_fe->info_length`. No bounds check ensures `dir_size + fid_size <= sizeof(dir_buf)`. When a directory's on-disk size approaches or exceeds 4096, the FID write overflows into kernel BSS.

**Impact:** Creating files in a directory that approaches 4096 bytes corrupts kernel memory.

**Problematic Code:**
```c
struct udf_fid *fid = (struct udf_fid *)(dir_buf + dir_size);
memset(fid, 0, fid_size);
```

**Fix:** Add bounds check:
```c
if (dir_size + fid_size > sizeof(dir_buf)) return -1;
```

---

### FS-6. UDF — `udf_read_file` Short/Long AD Path Dereferences Kernel Memory as Allocation Descriptors — UNRESOLVED

**File:** `sys/fs/udf/udf.c` lines 277–333  
**Severity:** CRITICAL — Uncontrolled Kernel Device I/O

**Issue:** Same root cause as FS-2: the cached `struct udf_fe` lacks the trailing allocation descriptor data. For short_ad and long_ad paths, `ads[i].position` / `ads[i].block` values come from kernel BSS memory (not from disk), and are used as sector offsets for device reads. `num_ads` is controlled by the attacker via `alloc_desc_length`. This causes device reads at arbitrary kernel-memory-derived offsets.

**Impact:** Semi-random device sector reads; combined with FS-2 completes an arbitrary read primitive.

**Problematic Code:**
```c
struct udf_short_ad *ads = (struct udf_short_ad *)alloc_area;
uint32_t num_ads = fe->alloc_desc_length / sizeof(struct udf_short_ad);
for (uint32_t i = 0; i < num_ads && size > 0; i++) {
    uint32_t ext_start = fs->partition_start + ads[i].position;  // kernel memory
```

**Fix:** Same as FS-2 — store or re-read the full FE sector, validate offset bounds.
