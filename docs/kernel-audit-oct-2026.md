# Audit of the Substrate kernel (`sys/`) — requirements checklist

Read-only security, memory-safety, concurrency, and architectural compliance
audit of the kernel under `sys/`, carried out 2026-10-18 at commit `a4a2cd2e0`
(`sys/` last touched at `d74c9b6e3`).  The kernel comprises 577 C, header, and
assembly files across `arch/` (`i386`, `x86-common`, `x86_64`), `boot/`,
`core/`, `vm/`, `kern/`, `pm/`, `vfs/`, `fs/` (`ext2`, `fat`, `exfat`, `udf`,
`minix`, `sysv`, `9p`, `devfs`, `procfs`, `sysfs`, `shmfs`, `pipe`, `fuse`),
`net/`, `exec/` (`formats/`, `perso/`), and `drivers/`.  Line numbers below are
those of that commit.

Every finding is written as the singular INCOSE-compliant requirement the code
does not meet, using EARS (Easy Approach to Requirements Syntax) patterns:

- **Ubiquitous**: *"The `<subsystem>` shall `<response>`."*
- **Event-driven**: *"When `<trigger>`, the `<subsystem>` shall `<response>`."*
- **State-driven**: *"While `<state>`, the `<subsystem>` shall `<response>`."*
- **Unwanted behaviour**: *"If `<condition>`, then the `<subsystem>` shall `<response>`."*
- **Optional feature**: *"Where `<feature>`, the `<subsystem>` shall `<response>`."*

Each requirement follows INCOSE rules (active voice, single *"shall"*, no
vague clauses, verifiable criterion) and is accompanied by:

- **Severity**: *Critical*, *High*, *Medium*, or *Low*.
- **Evidence**: exact file paths, line numbers, and the traced mechanics of the
  defect.
- **Basis**: *traced* — the control and data flow followed in full across the
  source tree; *reproduced* — verified by static scan across `sys/`.

Nothing here has been modified in the kernel tree.

**Verification (2026-10-18).**  Every finding was then checked against
`sys/` at commit `0f4036d53` (`sys/` unchanged since the audit) by three
independent reviews, then a judge: one traced the code path, one tried to
refute the finding, and one decided who can reach it and what follows.  The
judge's verdict, corrected severity and corrected line numbers are in
[section 8](#8-verification-results).  Each finding's severity tag gives the
original severity, the verified one where they differ, and the verdict.  Of
the 99 findings, 37 were confirmed, 54 partly confirmed (the defect is real but
part of the evidence or impact is wrong) and 8 refuted.  For one finding, one
of the three reviews returned no usable result, so its judge worked from two.

---

## 0. Summary

99 requirements the kernel does not meet, across seven areas:

| Area | ID Prefix | Critical | High | Medium | Low | Total |
| :--- | :--- | :---: | :---: | :---: | :---: | :---: |
| 1. Virtual Memory, Pmap & Allocators (`sys/vm/`, `arch/*/pmap.c`) | `SYS-VM` | 3 | 5 | 2 | 0 | 10 |
| 2. Architecture, Traps, Interrupts & Boot (`sys/arch/`, `sys/boot/`) | `SYS-ARCH` | 4 | 2 | 1 | 0 | 7 |
| 3. Kernel Core, Syscalls, Process Mgmt & IPC (`sys/kern/`, `sys/pm/`) | `SYS-KERN` | 5 | 8 | 11 | 3 | 27 |
| 4. VFS Layer & Block Cache (`sys/vfs/`, `sys/kern/syscall.c`) | `SYS-VFS` | 1 | 5 | 3 | 0 | 9 |
| 5. Filesystems (`sys/fs/`) | `SYS-FS` | 3 | 5 | 4 | 2 | 14 |
| 6. Networking, Executable Loaders & Personalities (`sys/net/`, `sys/exec/`) | `SYS-EXEC` / `SYS-NET` | 4 | 6 | 6 | 0 | 16 |
| 7. Device Drivers & Kernel Coding Rules (`sys/drivers/`, style rules) | `SYS-DRV` / `SYS-RULE` | 2 | 8 | 3 | 3 | 16 |
| **Total** | | **22** | **39** | **30** | **8** | **99** |

After verification ([section 8](#8-verification-results)):

| Area | Critical | High | Medium | Low | Not a defect | Confirmed | Partly | Refuted |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| 1. Virtual Memory | 4 | 1 | 1 | 4 | 0 | 5 | 5 | 0 |
| 2. Architecture | 2 | 1 | 0 | 3 | 1 | 1 | 5 | 1 |
| 3. Kernel Core | 2 | 6 | 9 | 9 | 1 | 15 | 11 | 1 |
| 4. VFS | 1 | 4 | 2 | 1 | 1 | 3 | 5 | 1 |
| 5. Filesystems | 1 | 4 | 4 | 4 | 1 | 6 | 7 | 1 |
| 6. Networking, Exec & Personalities | 1 | 4 | 5 | 4 | 2 | 5 | 9 | 2 |
| 7. Drivers & Coding Rules | 1 | 1 | 5 | 8 | 1 | 2 | 12 | 2 |
| **Total** | **12** | **21** | **26** | **33** | **7** | **37** | **54** | **8** |

SYS-DRV-007 was refuted as written but kept as a Low for the smaller defect
found in the same place, so 8 refuted findings yield 7 that are not defects.
The main reasons a severity came down were code with no caller (the vnode
layer's `namei`/`vfs_mount`, the NVMe driver, `uma_zdestroy`), the
application processors never running threads, guards the finding missed,
and impact overstated (a leak where a use-after-free was claimed, root-only
paths).  Three findings went up: SYS-VM-006 and SYS-DRV-003 to Critical,
SYS-VFS-007 to High.

### Headline defects

The table below is the audit's original list.  As verified, SYS-ARCH-002 and
SYS-DRV-002 are not defects, SYS-ARCH-001 and SYS-KERN-005 are High, and
SYS-KERN-002 is Medium; SYS-VM-006, SYS-ARCH-003, SYS-ARCH-004 and
SYS-DRV-003 are Critical and not listed.

| ID | Trigger / Condition | Observed Behaviour |
| :--- | :--- | :--- |
| **SYS-VM-001** | Ring 0 `copyout()` / `casuword32()` to a read-only (`PTE_W == 0`) or COW user page on i386 | `CR0.WP` is 0 on i386; Ring 0 overwrites fork-shared COW pages and global read-only vnode cache pages (`/bin/sh`, `libc.so.0`) without raising `#PF` |
| **SYS-VM-002** | `ptrace(PTRACE_POKETEXT)` or `pmap_copyout_other` on a child process | Writes directly to `P2V(pa)` without breaking COW, and on i386 without checking `va < USER32_VA_END` or `PTE_U` (overwriting `0xFE000000` signal trampoline or kernel memory) |
| **SYS-VM-003** | Pagedaemon reclaiming an inactive clean page (`vm_page_try_to_free`) | `pv_remove_all` frees `struct pv_entry` list nodes without clearing hardware PTEs, then frees the physical frame back to the buddy allocator while user PTEs still map it |
| **SYS-ARCH-001** | `rt_sigreturn` with `mc_eflags |= 0x4000` (`NT`) or `sigreturn` with a non-user `ds`/`es`/`fs`/`gs` selector or `SIGKILL` in `uc_sigmask` | Ring 0 `#TS` / `#GP` kernel panic on return to user mode, or process rendered immune to `SIGKILL` and `SIGSTOP` |
| **SYS-ARCH-002** | `iretq` faulting on x86_64 during user return | `is_iret_at_ip` tests `*(uint8_t *)ip == 0xCF`, missing the `0x48` REX.W prefix of `iretq` (`0x48 0xCF`) and panicking the x86_64 kernel |
| **SYS-KERN-001** | Unprivileged process calling `sys_thr_kill2(pid, tid, SIGKILL)` | No `signal_can_send` check and no PID 1 guard; kills any root process or `init` |
| **SYS-KERN-002** | `sysctl` write with `newptr != NULL` and `newlen == 0` on a string node | `sysctl_handle_string` executes `str[req->newlen - 1] = '\0'`, writing `0` to `str[-1]` in kernel `.data`/`.bss` |
| **SYS-KERN-004** | `set_tid_address(kernel_va)` followed by thread exit | `futex_wake_exited_thread` executes `*uaddr = 1` in Ring 0 after checking only `pmap_extract`, writing `1` to arbitrary kernel memory |
| **SYS-KERN-005** | Contended waiter acquiring a PI futex in `futex_lock_pi` | `pi_remove_waiter` frees `pi_state_t` when `ps->owner == NULL`, and lines 1315–1318 immediately write four fields into the freed slab object |
| **SYS-VFS-001** | Unprivileged process calling `truncate("/etc/passwd", 0)` | `sys_truncate` calls `truncate_fs` without checking write permission, read-only mount status, `FS_DIRECTORY`, or `RLIMIT_FSIZE` |
| **SYS-FS-001** | `write()` or `ftruncate()` growing a `shmfs` file while `mmap`ped (`map_count > 0`) | `shmfs_grow` calls `pmm_free_contiguous` on the old physical frames while user PTEs remain mapped to them |
| **SYS-EXEC-001** | Linux personality binary invoking `waitpid`, `gettimeofday`, `sigaction`, `readlink`, `getdents`, `sethostname`, etc. | 25+ entries in `linux_syscalls` point directly to internal `kern_*` functions that dereference user pointers without `copyin`/`copyout` |
| **SYS-DRV-002** | Multiple processes opening/closing `/dev/ntsync` or `/dev/ptmx` | Singleton `fs_node_t.impl` is overwritten on every `open()` and freed on the first `close()`, causing cross-process UAF |

---

## 1. Virtual Memory, Pmap & Allocators (`sys/vm/`, `sys/arch/*/pmap.c`)

- [x] **SYS-VM-001** *(Critical; confirmed)* While the i386 kernel is running with paging enabled, the processor shall have `CR0.WP` (bit 16, `0x00010000`) set on the bootstrap processor and all application processors.
  Evidence: `sys/arch/i386/boot.S:133-135` enables paging with `orl $0x80000000, %eax` (`CR0_PG` only) and `sys/arch/i386/efi_boot.c:433-436` sets `cr0 |= 0x80000000U`, whereas `sys/arch/x86_64/boot/boot.S:203-205` sets `CR0_PG | CR0_WP`. Because `CR0.WP == 0` on i386, Ring 0 stores in `copyout` (`sys/kern/subr_copy.c:99`), `casuword32` (`sys/kern/subr_copy.c:176`), and `futex_cmpxchg_user` (`sys/kern/futex.c:155`) bypass `PTE_W == 0` without raising `#PF`. Passing a fork-COW buffer or a `PROT_READ` `MAP_PRIVATE` buffer backed by `mmap_get_shared_backing_object` (`sys/vm/vm_syscalls.c:156-205`) to `read()` silently overwrites the shared physical page across processes.
  Basis: traced.

- [x] **SYS-VM-002** *(Critical; partly confirmed)* When copying data into another address space via `pmap_copyout_other` or `pmap_copy_other`, the pmap subsystem shall verify that the target virtual address is a user-accessible (`PTE_U`) address below `VM_MAXUSER_ADDRESS` backed by a direct-mapped physical page, and shall break copy-on-write before modifying a non-writable or shared page.
  Evidence: `sys/arch/i386/pmap.c:1586-1611` (`pmap_copyout_other`) and `sys/arch/x86_64/pmap.c:755-794` (`pmap_copy_other`) extract `pa = pmap_extract(pmap, va_page)` and `memcpy` directly into `P2V(pa)` without checking `PTE_W`, `vm_page->ref_count`, or `pv_list`. An unprivileged process calling `ptrace(PTRACE_POKETEXT)` on its own child overwrites shared pages in `mmap_get_shared_backing_object`. Furthermore, on i386 (`sys/arch/i386/pmap.c:1589-1600`), `pmap_copyout_other` performs no `va < USER32_VA_END` or `PTE_U` check (allowing writes to `SIG_TRAMPOLINE_ADDR` at `0xFE000000` or kernel addresses `>= 0xC0000000`) and bounds `pa` at `0x40000000` instead of `PMM_DIRECTMAP_PHYS_LIMIT` (`0x37000000`). On x86_64 (`sys/arch/x86_64/pmap.c:761-770`), `pmap_copy_other` checks `uva >= USER32_VA_END` (rejecting valid 64-bit user stack addresses at `0x7FFFFFFFE000`) and `pa >= PMAP_DMAP_LIMIT` (`4 GiB`, rejecting high-zone pages).
  Basis: traced.

- [x] **SYS-VM-003** *(Critical; confirmed)* Before freeing a physical page in `vm_page_try_to_free` or `vm_page_free`, the VM subsystem shall unmap all hardware page-table entries on `m->pv_list`, synchronize hardware dirty bits into `m->flags`, and retain the page on `inactive_queue` if reclamation fails.
  Evidence: `sys/vm/vm_page.c:861-904` (`vm_page_try_to_free`, `vm_page_reclaim_inactive_clean`) checks only `(m->flags & (PG_BUSY | PG_DIRTY | PG_VOTED)) == 0 && m->wire_count == 0` without checking `m->pv_list` or hardware `PTE_D`. It then calls `vm_page_free(m)` (`sys/vm/vm_page.c:563-620`), which calls `pv_remove_all(m)` (`:539-558`) — freeing the `struct pv_entry` list nodes without clearing the hardware PTEs in `curr->pmap` — forces `m->ref_count = 0` (`:604`), and returns the frame to `vm_phys_free_page(m)` while user PTEs still map it. In addition, `vm_page_reclaim_inactive_clean` (`:894-900`) removes `m` from `inactive_queue` before calling `vm_page_try_to_free(m)` and does not re-queue `m` if reclamation returns `0`.
  Basis: traced.

- [x] **SYS-VM-004** *(High → Medium; partly confirmed)* While scanning or mutating `active_queue` or `inactive_queue`, the VM pageout daemon shall hold `vm_page_queue_lock` and shall not dereference a cached `m->next` pointer after dropping `vm_page_queue_lock`.
  Evidence: `vm_pageout_scan_clock` (`sys/vm/vm_page.c:754`), `vm_pageout_scan_lru` (`:805`), and `vm_page_estimate_working_set` (`:1323-1334`) traverse `active_queue` and `inactive_queue` without acquiring `vm_page_queue_lock`. `vm_page_reclaim_inactive_clean` (`:890`), `vm_page_launder_inactive_dirty` (`:920`), and `vm_page_age_scan` (`:1136, 1173`) read `vm_page_t *next = m->next;` before dropping `vm_page_queue_lock`; if `next` is freed while the lock is dropped, `m->next` links into the buddy allocator free list.
  Basis: traced.

- [x] **SYS-VM-005** *(High; confirmed)* When a copy-on-write fault is resolved, the fault handler shall update the owning `vm_object` with the newly allocated `vm_page_t` and shall release any unused temporary page through `pmm_free_block`.
  Evidence: `sys/arch/x86-common/trap.c:234` calls `pmap_fault` (`sys/arch/i386/pmap.c:2364-2487`, `sys/arch/x86_64/pmap.c:1103-1155`) before `vm_fault` (`sys/vm/vm_fault.c:55-204`). When a process writes to a fork-COW page, `pmap_fault` allocates an anonymous page via `pmm_alloc_block()` (`page_new->object = NULL`) and installs it in the PTE without inserting `page_new` into `entry->object`, leaving the shadow `vm_object` pointing at the pre-COW page. Furthermore, `sys/arch/i386/pmap.c:2407-2450` allocates `virt_new = pmm_alloc_block()` before testing `single_mapper_safe` and frees it on the single-mapper path via `vm_page_free(pmm_get_page(phys_new))` instead of `pmm_free_block(virt_new)`, leaking a `memtrack` record on every single-mapper COW promotion.
  Basis: traced.

- [x] **SYS-VM-006** *(High → Critical; confirmed)* When growing or shrinking the process break in `sys_brk_locked`, the VM subsystem shall track the heap range in `current_process->vm_map` and release unmapped pages through reference-counted pmap teardown.
  Evidence: `sys/vm/vm_syscalls.c:540-707` (`sys_brk_locked`) maps pages directly into `current_process->pmap` without inserting or updating a `vm_map_entry` in `current_process->vm_map`, allowing `brk` expansion and `vm_map_find_space` (`mmap`) to overlap. On shrink, `brk_unmap_free_pages` (`sys/vm/vm_syscalls.c:79-99`) calls both `pmap_remove(pmap, va)` and `pmm_free_block(P2V(pa & ~0xFFFU))`, dropping two references on `pa` even when `pa` is COW-shared with a forked child.
  Basis: traced.

- [x] **SYS-VM-007** *(High → Low; partly confirmed)* When demand-growing the user stack in `vm_grow_user_stack`, the kernel shall zero the newly allocated page before inserting it into the process `pmap`.
  Evidence: `sys/arch/x86-common/trap.c:130-134` calls `pmap_enter(p->pmap, va, pa_phys, VM_PROT_WRITE, 0)` at line 130 and `memset(pa, 0, 0x1000)` at line 134, leaving uninitialized physical memory mapped and writable to concurrent threads in `p` during the window between `pmap_enter` and `memset`.
  Basis: traced.

- [x] **SYS-VM-008** *(High → Low; partly confirmed)* When creating a UMA zone in `uma_zcreate`, the slab allocator shall cap `zone->uz_ipers` below `UMA_ITEM_BUCKETED` (`254`).
  Evidence: `sys/vm/uma_core.c:36-37` reserves `0xFE` (`254`) and `0xFF` (`255`) in `uint8_t us_freelist[]` for `UMA_ITEM_BUCKETED` and `UMA_ITEM_ALLOCED`, but `uma_zcreate` (`sys/vm/uma_core.c:508-520`) computes `zone->uz_ipers = (4096 - sizeof(uma_slab_t)) / (zone->uz_rsize + 1)` without an upper bound. For zones with `uz_rsize <= 12` (`uz_ipers >= 254`), `us_freelist[i]` wraps `uint8_t` and collides with `0xFE`/`0xFF` at `uma_core.c:776-778`.
  Basis: traced.

- [x] **SYS-VM-009** *(Medium → Low; confirmed)* When destroying a UMA zone in `uma_zdestroy` or invoking `uz_fini` in `uma_slab_free`, the slab allocator shall drain all per-CPU and depot buckets for the zone and pass the redzone-adjusted object pointer to `uz_fini`.
  Evidence: `sys/vm/uma_core.c:576-625` (`uma_zdestroy`) frees slabs and `zone` without calling `uma_bucket_detach_full_for_zone(zone)` or draining `zone->uz_cpu[i]`, leaving dangling `ub_zone` pointers in `uma_bucket_full_depot`. `uma_slab_free` (`sys/vm/uma_core.c:826-832`) calls `uz_fini(obj, zone->uz_size)` instead of `uz_fini(obj + UMA_REDZONE_SIZE, zone->uz_size)`.
  Basis: traced.

- [x] **SYS-VM-010** *(Medium → Low; partly confirmed)* When marking a physical page used in `vm_phys_mark_used` or verifying free-list integrity in `vm_phys_check_integrity`, the physical memory manager shall update and inspect the zone counter corresponding to the page's buddy zone.
  Evidence: `sys/vm/phys_mem.c:674` decrements `vm_phys_free_count` instead of `*vm_phys_count_of(head)` (underflowing the low-zone counter when marking a `>= 4 GiB` high-zone page used), and `vm_phys_check_integrity` (`:680-735`) only inspects `vm_phys_free_lists` and ignores `vm_phys_hi_free_lists`.
  Basis: traced.

---

## 2. Architecture, Traps, Interrupts & Boot (`sys/arch/`, `sys/boot/`)

- [x] **SYS-ARCH-001** *(Critical → High; partly confirmed)* When restoring thread state in `sigreturn`, `rt_sigreturn`, or `sigsuspend`, the kernel shall clear `sigmask(SIGKILL) | sigmask(SIGSTOP)` from `sig_mask`, restrict `eflags` to `EFLAGS_USER_MASK` (`0x00000DD5`), and validate that `cs`, `ss`, `ds`, `es`, `fs`, and `gs` contain valid user-mode selectors.
  Evidence:
  1. `sys/arch/i386/signal.c:730` (`sys_sigreturn`), `sys/arch/i386/signal.c:874` (`sys_rt_sigreturn`), `sys/arch/x86_64/signal64.c:216` (`amd64_sys_sigreturn`), and `sys/kern/signal.c:730` (`kern_sigsuspend`) assign `current_thread->sig_mask` directly from user input without clearing `sigmask(SIGKILL) | sigmask(SIGSTOP)`. Because `signal_handle_pending` (`sys/kern/signal.c:1711`) computes `pending = current_thread->sig_pending & ~current_thread->sig_mask` before checking `sig == SIGKILL`, the thread becomes immune to `SIGKILL` and `SIGSTOP`.
  2. `sys/arch/i386/signal.c:857-862` (`sys_rt_sigreturn`) defines `RT_EFLAGS_USER_MASK` as `0xFFCCCDFF`, which permits bit 14 (`0x00004000`, `EFLAGS.NT`). Setting `NT` causes Ring 0 `iret` to attempt a hardware task switch via TSS backlink `0` and panic with `#TS`.
  3. `sys/arch/i386/signal.c:662-676, 819-835` does not validate `ds`, `es`, `fs`, or `gs`, and only checks `(cs & 3) == 3` and `(ss & 3) == 3`. Popping a non-present, out-of-bounds, or system selector in `isr_exit` (`sys/arch/i386/isr.S:259-270`) triggers a Ring 0 `#GP` or `#NP` panic.
  Basis: traced.

- [x] **SYS-ARCH-002** *(Critical → not a defect; refuted — closed, no change)* When an exception occurs at a kernel `iret` or `iretq` instruction during user return, `is_iret_at_ip` shall recognize both 32-bit `iret` (`0xCF`) and 64-bit `iretq` (`0x48 0xCF`).
  Evidence: `sys/arch/x86-common/trap.c:29-33` (`is_iret_at_ip`) tests only `*(const uint8_t *)ip == 0xCF`. On x86_64 (`sys/arch/x86_64/isr.S:222`), `iretq` is encoded as `0x48 0xCF`, so `*(const uint8_t *)ip` is `0x48` and `is_iret_at_ip` always returns `0`, turning any user-recoverable `iretq` fault into a Ring 0 kernel panic.
  Basis: traced.

- [x] **SYS-ARCH-003** *(Critical; partly confirmed)* When starting a new user thread, the kernel shall copy initial stack arguments via `copyout` prior to scheduling and shall initialize the per-CPU 32-bit TLS GDT descriptor (slot 6, selector `0x33`) before loading `%gs` in `new_user_thread_trampoline`.
  Evidence:
  1. `sys/arch/i386/isr.S:432-441` (`new_user_thread_trampoline`) and `sys/arch/x86_64/isr.S:291-306, 445-451` (`new_user_thread_trampoline`, `new_user_thread_trampoline64`) execute raw Ring 0 stores (`movl %eax, (%edx)` / `movq $0, (%r14)`) to the user stack pointer without `on_fault` protection. If the user stack page is unmapped or read-only when the thread is scheduled, the Ring 0 store panics the kernel.
  2. `sys/arch/x86_64/gdt.c:155` (`gdt_init_percpu`) leaves GDT slot 6 (`0x30`, selector `0x33`) zeroed (not present), and `i386_load_gs_slot` (`sys/arch/i386/sysarch.c:40`) returns early when `t->gs_base == 0`. When a 32-bit compatibility thread starts with `gs_base == 0`, `movw $0x33, %ax; movw %ax, %gs` in `sys/arch/x86_64/isr.S:304` faults in Ring 0 with `#NP`.
  Basis: traced.

- [x] **SYS-ARCH-004** *(Critical; confirmed)* When entering or emulating Virtual-8086 mode in `sys_vm86` and `vm86_gpf_handler`, the kernel shall mask `IOPL` and `NT` from user-supplied `eflags`, access guest memory exclusively through fault-safe user copy helpers, and terminate VM86 execution on unhandled opcodes.
  Evidence: `sys/arch/i386/vm86.c:120-138` (`sys_vm86`) passes `k_info.regs.eflags | 0x20200` to `vm86_enter` (`sys/arch/i386/vm86_asm.S:44-114`) without clearing `IOPL` (`0x3000`) or `NT` (`0x4000`). `vm86_gpf_handler` (`sys/arch/i386/vm86.c:156-406`) directly dereferences user pointers `(cs << 4) + ip`, `(ss << 4) + sp`, and the real-mode IVT at virtual address `0x00000000` (`:199`) in Ring 0 without `copyin`/`copyout`. On an unhandled opcode (`default:` at `:399`), `eip` is not advanced and VM86 mode is not exited, causing an infinite Ring 0 GPF loop.
  Basis: traced.

- [x] **SYS-ARCH-005** *(High → Low; partly confirmed)* When migrating a thread between CPUs or clearing FPU ownership in `fpu_forget_process`, the FPU subsystem shall synchronize per-CPU FPU state with interrupts disabled so a migrated thread never loads stale `fpu_area` state.
  Evidence: `sys/arch/x86-common/fpu.c:72-88, 515-571` tracks lazy ownership in `fpu_owner[MAX_CPUS]`. If thread `T` dirties the FPU on CPU A (`fpu_owner[A] == T`) and migrates to CPU B, `fpu_handler` on CPU B restores `T->fpu_area` from memory before CPU A has saved its live registers. `fpu_forget_process` (`sys/arch/x86-common/fpu.c:272-279`) also modifies `fpu_owner[cpu]` without disabling interrupts.
  Basis: traced.
  Resolution: `fpu_forget_process` now runs with interrupts disabled.  Migration cannot happen yet: the APs park in `hlt` and never run threads, so one CPU owns every thread's registers.  When APs run threads, a thread that owns another CPU's registers must have them saved there (an IPI) before it runs elsewhere; the comment on `fpu_owner` records this.

- [x] **SYS-ARCH-006** *(High → Low; partly confirmed)* When cloning or deactivating a process LDT in `sys/arch/i386/ldt.c`, the kernel shall release LDT storage through `ldt_free_storage` and clear the GDT LDT descriptor on x86_64 when switching to a process without an LDT.
  Evidence: `sys/arch/i386/ldt.c:389` (`ldt_clone_process`) calls `kfree(new_ldt, bytes)` instead of `ldt_free_storage(new_ldt, src_entry_count, new_is_uma)` when `src_entry_count == 8192` (`new_is_uma == 1`, allocated from `ldt_full_zone`). `ldt_activate` (`sys/arch/i386/ldt.c:302-310`) returns after `ldt_load_selector(0)` when `proc->ldt == NULL`, leaving the previous process's 16-byte LDT descriptor marked `GDT_PRESENT` in the x86_64 GDT.
  Basis: traced.

- [x] **SYS-ARCH-007** *(Medium → Low; partly confirmed)* While allocating memory in the stage-2 BIOS bootloader (`sys/boot/stage2.c`), `salloc` shall verify that the bump pointer does not collide with the bootloader stack, and `ext2_read_inode` shall reuse a static block buffer.
  Evidence: `sys/boot/stage2.c:25-30` advances `alloc_ptr` from `0x60000` with no upper-bound check against the stack at `0x90000`, and `ext2_read_inode` (`:551-596`) allocates a fresh 1 KiB buffer via `salloc(block_size)` on every inode read.
  Basis: traced.

---

## 3. Kernel Core, Syscalls, Process Management & IPC (`sys/kern/`, `sys/pm/`)

- [x] **SYS-KERN-001** *(Critical; confirmed)* When sending a signal to a thread in another process via `sys_thr_kill2`, the kernel shall verify caller permissions with `signal_can_send(current_process, target_proc)`, enforce PID 1 signal restrictions, and hold `thread_registry_lock()` while iterating `threads[]`.
  Evidence: `sys/kern/syscall.c:1661-1678` (`sys_thr_kill2`) looks up `target_proc = proc_find(pid)` and posts `sig` via `signal_post_thread` without calling `signal_can_send` and without checking `target_proc->pid == 1`. Any unprivileged user can send `SIGKILL` to any root process or `init` (PID 1). The `id == -1` loop (`:1667-1671`) also iterates `threads[]` without acquiring `thread_registry_lock()`.
  Basis: traced.

- [x] **SYS-KERN-002** *(Critical → Medium; partly confirmed)* When handling a string write in `sysctl_handle_string`, the sysctl subsystem shall not index before the beginning of the destination buffer when `req->newlen == 0`.
  Evidence: `sys/kern/sysctl.c:347-354` checks `if (req->newlen > (size_t)arg2) return ENAMETOOLONG;`, calls `sysctl_new_kernel(req, str, req->newlen)` (which returns `0` for `newlen == 0`), and executes `str[req->newlen - 1] = '\0'`, writing a NUL byte to `str[-1]`.
  Basis: traced.

- [x] **SYS-KERN-003** *(Critical → Medium; partly confirmed)* When revoking a terminal in `sys_revoke`, the kernel shall resolve relative paths from `current_process->cwd_node`, verify that the caller is root or the file owner (`euid == 0 || euid == node->uid`), and verify that `node` is a character device backed by a valid `struct tty` (`tty->magic == TTY_MAGIC`).
  Evidence: `sys/kern/syscall.c:4053-4063` (`sys_revoke`) resolves `kpath` from `fs_root`, checks no credentials, and passes `(struct tty *)node->ptr` directly to `tty_revoke` without checking `(node->flags & 0x7) == FS_CHARDEVICE` or `tty->magic == TTY_MAGIC`. Calling `revoke("/etc/passwd")` passes an `ext2` inode context pointer as a `struct tty *`.
  Basis: traced.

- [x] **SYS-KERN-004** *(Critical; confirmed)* When clearing and waking `exit_tid_ptr` in `futex_wake_exited_thread`, the kernel shall write to user memory through `futex_write_user` (`validate_uaddr` + `copyout`) rather than dereferencing `uaddr` directly in Ring 0.
  Evidence: `sys/kern/futex.c:380-384` (`sys_set_tid_address`) stores `tidptr` without validation, and `futex_wake_exited_thread` (`sys/kern/futex.c:354-370`) checks only `futex_get_key((uintptr_t)uaddr)` (`pmap_extract`) before executing `*uaddr = 1;` in Ring 0. Because kernel virtual addresses (`>= 0xC0000000` on i386) are mapped in every user `pmap`, `pmap_extract` returns non-zero for kernel addresses and `*uaddr = 1` overwrites arbitrary kernel memory on thread exit.
  Basis: traced.

- [x] **SYS-KERN-005** *(Critical → High; confirmed)* When a contended waiter acquires a priority-inheritance futex in `futex_lock_pi`, the kernel shall assign `ps->owner = current_thread` before calling `pi_remove_waiter(ps, current_thread)` so `pi_remove_waiter` does not free `ps` while it is still being initialized.
  Evidence: `sys/kern/futex.c:1404-1405` (`futex_unlock_pi`) clears `ps->owner = NULL` before waking the waiter. When the woken waiter in `futex_lock_pi` (`sys/kern/futex.c:1313-1319` and `:1326-1333`) calls `pi_remove_waiter(ps, current_thread)` (`:1008-1035`) before setting `ps->owner = current_thread`, `pi_remove_waiter` sees `ps->waiters == NULL && ps->owner == NULL` (`:1020`) and frees `ps` via `pi_state_free(ps)`. Lines 1315–1318 then write `ps->owner`, `ps->owner_tid`, `ps->owner_prio`, and `ps->boosted_prio` into the freed slab object.
  Basis: traced.

- [x] **SYS-KERN-006** *(High; confirmed)* When `setreuid` or `setregid` is called with `ruid != (uid_t)-1` (`rgid != (gid_t)-1`) or with a new effective ID not equal to the prior real ID, the kernel shall update the saved set-user-ID (`p->suid`) or saved set-group-ID (`p->sgid`) to the new effective ID.
  Evidence: `sys/kern/syscall.c:2081-2126` (`setreuid_ids`, `setregid_ids`) computes `int ruid_changed = (ruid != -1 && new_ruid != p->uid);`. When a setuid-root process (`uid = 1000, euid = 0, suid = 0`) calls `setreuid(1000, 1000)` to drop privileges, `new_ruid != p->uid` is false and `new_euid != p->uid` is false, leaving `p->suid = 0` so the process can later regain root via `seteuid(0)`.
  Basis: traced.

- [x] **SYS-KERN-007** *(High; confirmed)* When looking up a POSIX message queue descriptor in `mqd_lookup`, the kernel shall verify that `d->pid == current_process->pid`.
  Evidence: `sys/kern/posix_mqueue.c:127-136` (`mqd_lookup`) indexes the global `mqdescs[]` table and verifies `d->in_use` and `d->seq == seq`, but never checks `d->pid == current_process->pid` (recorded at `:384`), allowing any process to read, write, modify, or close another process's `mqd_t`.
  Basis: traced.

- [x] **SYS-KERN-008** *(High; confirmed)* When performing wait, post, or getvalue operations on a POSIX semaphore in `sys/kern/posix_sem.c`, the kernel shall verify that the calling process holds an open reference in `ksem_open_list` or satisfies `ksem_perm_ok`.
  Evidence: `sys/kern/posix_sem.c:99-108, 325-489` (`kern_ksem_trywait`, `kern_ksem_wait`, `kern_ksem_timedwait`, `kern_ksem_post`, `kern_ksem_getvalue`) look up global `ksem` IDs via `ksem_lookup` without checking `ksem_perm_ok` or `ksem_open_list`.
  Basis: traced.

- [x] **SYS-KERN-009** *(High; confirmed)* When attaching a System V shared memory segment at a caller-specified `shmaddr` in `kern_shmat`, the kernel shall verify that `[virt, virt + length)` does not wrap and lies within `[map->min_offset, map->max_offset]` before calling `vm_map_remove`.
  Evidence: `sys/kern/ipc_shm.c:255-317` (`kern_shmat`) checks only `virt & 0xFFF` when `have_addr` is true and immediately calls `vm_map_remove(map, virt, virt + length)` at line 308 without bounds or wrap-around validation.
  Basis: traced.

- [x] **SYS-KERN-010** *(High → Medium; partly confirmed)* When waking a thread in `sys_thr_wake`, the kernel shall restrict lookup to threads belonging to `current_process` via `thr_lookup_in_proc(id, current_process)`.
  Evidence: `sys/kern/syscall.c:1721-1730` (`sys_thr_wake`) calls global `sched_get_thread((int)id)` without checking `t->proc == current_process`, allowing any process to wake threads in other processes or kernel threads.
  Basis: traced.

- [x] **SYS-KERN-011** *(High → Medium; partly confirmed)* Before copying directory entries to user space in `kern_getdents` and `kern_getdents64`, the kernel shall zero the alignment padding bytes of each entry.
  Evidence: `sys/kern/syscall.c:1303-1407` allocates `char temp_buf[512];` on the kernel stack without `memset` and copies `reclen` (aligned up to 4 or 8 bytes) bytes into `dirp`, disclosing 1–7 bytes of uninitialized kernel stack per directory entry.
  Basis: traced.

- [x] **SYS-KERN-012** *(High; confirmed)* When setting `current_thread->on_fault` in `copyinstr` and `futex_cmpxchg_user`, the kernel shall place the fault recovery label inside the inline assembly block rather than taking the address of a C label (`&&fault`).
  Evidence: `sys/kern/subr_copy.c:216-243` (`copyinstr`) and `sys/kern/futex.c:150-168` (`futex_cmpxchg_user`) assign `current_thread->on_fault = (uintptr_t)&&fault;` around C loop bodies / separate asm statements, which `sys/kern/subr_copy.c:80-93` documents as unsafe under GCC basic-block folding and register allocation across traps.
  Basis: traced.

- [x] **SYS-KERN-013** *(High → Medium; confirmed)* While `securelevel > 0`, the sysctl subsystem shall reject any attempt to write a smaller value to `kern.securelevel` with `EPERM`.
  Evidence: `sys/kern/sysctl.c:48` registers `kern.securelevel` with generic `sysctl_handle_int` (`:303-330`) and `CTLFLAG_RW`, allowing root to lower `securelevel` back to `0` or `-1`.
  Basis: traced.

- [x] **SYS-KERN-014** *(Medium → Low; partly confirmed)* When writing a process accounting record in `acct_process` or enabling accounting in `kern_acct`, the kernel shall zero `struct acct ac`, require a regular file resolved via `vfs_lookup`, and close any previously active `acct_node`.
  Evidence: `sys/kern/acct.c:73-97` (`acct_process`) leaves `ac.ac_exitcode` and struct padding in `struct acct ac` uninitialized on the stack before `write_fs`; `kern_acct` (`:48-57`) overwrites `acct_node` without closing the prior node and uses single-component `finddir_fs(fs_root, ...)`.
  Basis: traced.

- [x] **SYS-KERN-015** *(Medium; confirmed)* When inspecting another process via `sys_proc_*` syscalls, `proc_inspect_allowed` shall deny non-root access to `P_SUGID` processes and shall be enforced on `sys_proc_threads`, `sys_proc_cwd`, `sys_proc_exe`, and `sys_proc_thr_list`.
  Evidence: `sys/kern/syscall.c:4592-4596` (`proc_inspect_allowed`) checks only `target->uid == current_process->euid` (ignoring `P_SUGID` and `target->euid`), and `sys_proc_threads` (`:4485`), `sys_proc_cwd` (`:4648`), `sys_proc_exe` (`:4663`), and `sys_proc_thr_list` (`:4678`) omit `proc_inspect_allowed`.
  Basis: traced.

- [x] **SYS-KERN-016** *(Medium; partly confirmed)* When acquiring `pi_lock` in `sys/kern/futex.c`, the kernel shall disable preemption and shall not invoke `kmalloc` while holding `pi_lock`.
  Evidence: `sys/kern/futex.c:889-896` (`pi_spinlock`) spins with `__sync_lock_test_and_set` without calling `preempt_disable()`, and `pi_waiter_alloc` (`:926`) and `pi_get_or_create` (`:957`) call `kmalloc` while holding `pi_lock`.
  Basis: traced.

- [x] **SYS-KERN-017** *(Medium → Low; confirmed)* When converting `struct timespec` timeouts in `posix_mqueue.c`, `posix_sem.c`, `futex.c`, and `umtx.c`, the kernel shall reject negative `tv_sec` on relative timeouts with `-EINVAL` and clamp large `tv_sec` values against signed 64-bit multiplication overflow.
  Evidence: `sys/kern/posix_mqueue.c:256-268`, `sys/kern/posix_sem.c:450-460`, and `sys/kern/futex.c:546, 1132` overflow signed `int64_t` on `dsec * 1000000000LL` or `total_ns * hz`, and `sys/kern/futex.c:532, 554` and `sys/kern/umtx.c:120, 128` do not check `ts.tv_sec < 0` on relative timeouts.
  Basis: traced.

- [x] **SYS-KERN-018** *(Medium → Low; partly confirmed)* When executing `UMTX_OP_NWAKE_PRIVATE` in `sys/kern/umtx.c`, the kernel shall bound `count` and compute pointer offsets without 32-bit signed integer overflow.
  Evidence: `sys/kern/umtx.c:363-378` loops `for (int i = 0; i < count; i++)` up to `INT_MAX` with `(char *)obj + i * 4`, which overflows signed `int` at `i = 0x20000000` and spins uninterruptibly in Ring 0.
  Basis: traced.

- [x] **SYS-KERN-019** *(Medium; confirmed)* When looking up an existing System V shared memory segment (`kern_shmget`) or semaphore set (`kern_semget`), the IPC subsystem shall check only the access mode bits requested in `shmflg & 0777` (`semflg & 0777`) rather than unconditionally demanding read+write (`4 | 2`).
  Evidence: `sys/kern/ipc_shm.c:174` and `sys/kern/ipc_sem.c:196` call `shm_perm_ok(s, 4 | 2)` and `sem_perm_ok(s, 4 | 2)` unconditionally, causing read-only `shmget(key, 0, 0444)` or `shmget(key, 0, 0)` by non-owners to fail with `-EACCES`.
  Basis: traced.

- [x] **SYS-KERN-020** *(Medium; confirmed)* When `SEM_UNDO` is requested in `kern_semop`, the kernel shall fail with `-ENOMEM` if `sem_undo_find` cannot allocate an undo structure, and `kern_sem_setval` / `kern_sem_setall` shall clear existing `sem_undo` adjustments for the modified semaphore(s).
  Evidence: `sys/kern/ipc_sem.c:323-338` ignores a `NULL` return from `sem_undo_find` and commits semaphore changes without recording the undo delta; `kern_sem_setval` (`:497-514`) and `kern_sem_setall` (`:532-550`) leave stale `u->adj[semnum]` entries intact.
  Basis: traced.

- [x] **SYS-KERN-021** *(Medium → Low; partly confirmed)* When setting the session login name in `sys_setlogin`, the kernel shall require superuser privileges (`current_process->euid == 0`).
  Evidence: `sys/pm/pgrp.c:307-321` (`sys_setlogin`) overwrites `s_login` without checking `current_process->euid == 0`.
  Basis: traced.

- [x] **SYS-KERN-022** *(Medium → Low; partly confirmed)* When `sys_kill` or `pgrp_find` is called with `pid == INT_MIN` or a negative `pgid`, the kernel shall reject the value with `-ESRCH` without signed negation overflow or negative hash indexing.
  Evidence: `sys/kern/signal.c:1628` evaluates `-pid` (which overflows signed `int` when `pid == INT_MIN` and remains `INT_MIN`), and `pgrp_hashval` (`sys/pm/pgrp.c:30-32`) computes `pgid % PGRP_HASH_SIZE`, returning a negative array index for negative `pgid`.
  Basis: traced.

- [x] **SYS-KERN-023** *(Medium → Low; partly confirmed)* When changing user or group IDs in `setuid`, `setgid`, `setreuid`, `setregid`, `setresuid`, and `setresgid`, the kernel shall reject negative IDs (other than `-1` where permitted) with `-EINVAL`.
  Evidence: `sys/kern/syscall.c:2015-2040` (`setuid_ids`, `setgid_ids`) accepts negative values such as `-1`, and `setre*`/`setres*` (`:2042-2126`) accept values `< -1`.
  Basis: traced.
  Resolution: `setuid`, `setgid`, `seteuid` and `setegid` refuse `-1` with `EINVAL`.  Other values `< -1` are kept: as a `uid_t` they are ordinary high ids (4294967294 is a common "nobody"), which Linux and the BSDs accept too.

- [x] **SYS-KERN-024** *(Medium → Low; partly confirmed)* When returning slab allocation site records in `sys_vm_slabs`, the kernel shall redact kernel text return addresses (`recs[i].pc`) for unprivileged callers (`current_process->euid != 0`).
  Evidence: `sys/kern/sysinfo.c:215` formats `pc=0x%08x` (`recs[i].pc`) into `si.name` and copies it out to any unprivileged caller.
  Basis: traced.

- [x] **SYS-KERN-025** *(Low; confirmed)* If kernel stack or thread allocation fails in `sched_spawn_kernel_process`, then the process manager shall destroy the newly created `process_t` (`proc_destroy(child)`) before returning.
  Evidence: `sys/pm/process.c:848-884` allocates `child = proc_create(PERS_NATIVE)` at line 852 and returns `-1` at lines 866 and 873 without calling `proc_destroy(child)`.
  Basis: traced.

- [x] **SYS-KERN-026** *(Low; confirmed)* When executing `FUTEX_WAKE_OP` in `sys_futex`, the kernel shall update `*uaddr2` using an atomic compare-and-swap loop (`casuword32`).
  Evidence: `sys/kern/futex.c:718-732` performs a non-atomic `futex_read_user` followed by `futex_write_user`.
  Basis: traced.

- [x] **SYS-KERN-027** *(Low → not a defect; refuted — closed, no change)* If a thread attempts to recursively lock a non-recursive `mutex_t` on the slow path in `mutex_lock`, then the kernel shall panic, matching the fast-path check.
  Evidence: `sys/kern/mutex.c:110-115` checks `if (m->owner == me)` with an empty comment body and proceeds to enqueue `me` on `m->turnstile` (self-deadlock), unlike line 58 which panics.
  Basis: traced.

---

## 4. VFS Layer & Block Cache (`sys/vfs/`, `sys/kern/syscall.c`)

- [x] **SYS-VFS-001** *(Critical; partly confirmed)* When truncating a file by path in `sys_truncate`, the kernel shall resolve relative paths from `current_process->cwd_node`, reject directories with `-EISDIR`, reject read-only mounts with `-EROFS`, verify write permission via `vfs_check_permissions_groups`, and enforce `RLIMIT_FSIZE`.
  Evidence: `sys/kern/syscall.c:1204-1213` (`sys_truncate`) resolves `kpath` from `root` (ignoring `cwd_node` for relative paths) and calls `truncate_fs(node, length)` immediately with no permission check, no `vfs_is_rdonly` check, no directory check, and no `RLIMIT_FSIZE` check. Any unprivileged user can truncate `/etc/passwd` or `/sbin/init`.
  Basis: traced.

- [x] **SYS-VFS-002** *(High; confirmed)* When changing file ownership via `chown`, `fchown`, `lchown`, or `fchownat`, the kernel shall verify that a non-root owner's target `gid` equals `current_process->egid` or is in `current_process->supp_groups`, clear `S_ISUID | S_ISGID` on non-root calls, reject read-only mounts, and persist changes via `setattr_fs`.
  Evidence: `sys/kern/syscall.c:3570-3776` (`sys_lchown`, `sys_fchown`, `kern_fchownat`, `sys_lchownat`) allows a non-root file owner (`euid == node->uid`) to set `gid` to any group (including `gid = 0`), and mutates only the in-memory `node->uid`/`node->gid` without calling `setattr_fs(node, &attr)`, so ownership changes are never written to disk on `ext2`.
  Basis: traced.

- [x] **SYS-VFS-003** *(High; partly confirmed)* While a process has a chroot root (`current_process->root_node != NULL`), the VFS lookup routines (`finddir_fs_internal`, `vfs_perso_component`, `vfs_perso_shadow`, and `namei`) shall resolve absolute symlinks against `current_process->root_node`, bypass global `/perso/<name>` shadowing, and preserve the `'/'` separator when expanding mid-path symlinks.
  Evidence: `sys/vfs/vfs.c:909` (`finddir_fs_internal`) and `:1076` (`vfs_perso_component`) resolve `link_target[0] == '/'` against global `fs_root` instead of `current_process->root_node`. `vfs_perso_shadow` (`sys/vfs/vfs.c:957-976`) resolves `/perso/<name>/...` against `fs_root` even when chrooted. In `sys/vfs/vfs_lookup.c:274` (`namei`), `target` and remaining path `p` (after `while (*p == '/') p++;` at `:143`) are concatenated without a `'/'` separator.
  Basis: traced.

- [x] **SYS-VFS-004** *(High → Medium; partly confirmed)* When checking access permissions on a bridged vnode in `fsnode_vop_access`, the vnode-to-`fs_node` bridge shall pass the caller's credentials rather than hardcoded `uid = 0, gid = 0`.
  Evidence: `sys/vfs/vnode_fsnode.c:194-208` (`fsnode_vop_access`) casts `ap->a_cred` to `(void)cred;` and calls `vfs_check_permissions(n, 0, 0, mode)`, granting superuser access on all bridged vnode permission checks.
  Basis: traced.

- [x] **SYS-VFS-005** *(High → Low; partly confirmed)* When looking up a directory entry on a bridged vnode in `fsnode_vop_lookup`, the vnode-to-`fs_node` bridge shall call `finddir_lstat_fs` rather than `finddir_fs` so `namei` controls symlink traversal and honors `NOFOLLOW`.
  Evidence: `sys/vfs/vnode_fsnode.c:131-132` calls `finddir_fs(dn, name)` under the inaccurate comment `/* finddir_fs() does not follow symlinks */` (`finddir_fs` in `sys/vfs/vfs.c:819` passes `follow_symlinks = 1`).
  Basis: traced.

- [x] **SYS-VFS-006** *(High; confirmed)* When seeking on a file descriptor in `sys_lseek` and `kern_lseek`, the kernel shall check `f->f_data != NULL` on `SEEK_END`, reject negative resulting offsets and signed 64-bit overflow with `-EINVAL`, and return a 64-bit `off_t` from `kern_lseek`.
  Evidence: `sys/kern/syscall.c:1150-1190` dereferences `((fs_node_t *)f->f_data)->length` without a NULL check at line 1179, allows negative `f->f_offset` (which downstream filesystems cast to unsigned `uint32_t`/`size_t`), and declares `int kern_lseek(...)` at line 1188, truncating offsets `>= 2 GiB`.
  Basis: traced.

- [x] **SYS-VFS-007** *(Medium → High; confirmed)* If file descriptor allocation, `file_t` allocation, or `copyout` fails in `kern_pipe` or `sys_pipe2`, then the kernel shall close all allocated pipe `fs_node_t` endpoints and file descriptors before returning.
  Evidence: `sys/kern/syscall.c:3351-3407` (`kern_pipe`) leaks `read_node` and `write_node` when `f1 == -1`, `f2 == -1`, or `!rf`, and leaks `write_node` when `!wf`. `sys_pipe2` (`:3488`) returns `-EFAULT` on `copyout` failure without calling `kern_close(kfds[0])` and `kern_close(kfds[1])`.
  Basis: traced.

- [x] **SYS-VFS-008** *(Medium → not a defect; refuted — closed, no change)* When waiting for a busy buffer in `biowait`, `binval_vnode`, `bio_dev_invalidate`, and `bio_dev_purge`, the block cache (`sys/vfs/bio.c`) shall sleep via `sched_sleep(bp)` (matching `getblk`) rather than calling `sleepq_add` followed by `sched_yield()`.
  Evidence: `sys/vfs/bio.c:469-471, 729-731, 851-853, 895-897` call `sleepq_add(bp, current_thread); mutex_unlock(&bio_lock); sched_yield();` without `sleepq_remove_thread`, unlike `getblk` (`:356`) which calls `sched_sleep(bp)`.
  Basis: traced.

- [x] **SYS-VFS-009** *(Medium; partly confirmed)* While traversing or modifying `mountlist` in `vfs_mount.c` and `procfs.c`, or `mp->mnt_vnodelist` in `vflush`, the VFS subsystem shall hold the corresponding list lock (`vfs_mount_lock` / `vnode_mntlist_lock`).
  Evidence: `sys/vfs/vfs_mount.c:104, 181, 252` and `sys/fs/procfs.c:423` access `mountlist` without `vfs_mount_lock` (`static` in `sys/vfs/vfs.c`), and `vflush` (`sys/vfs/vnode.c:627`) iterates `mp->mnt_vnodelist` without `vnode_mntlist_lock`.
  Basis: traced.

---

## 5. Filesystems (`sys/fs/`)

- [x] **SYS-FS-001** *(Critical; confirmed)* While a `shmfs` object has active memory mappings (`inode->map_count > 0`), `shmfs_grow` shall refuse to reallocate and free `inode->data` (`pmm_free_contiguous`).
  Evidence: `sys/fs/shmfs.c:161-182` (`shmfs_grow`) allocates new contiguous physical pages, copies `inode->data`, and calls `pmm_free_contiguous(inode->data, inode->data_cap >> 12)` without checking `inode->map_count > 0`, leaving existing user `mmap` PTEs pointing at freed physical pages that are reallocated by the kernel.
  Basis: traced.

- [x] **SYS-FS-002** *(Critical → High; partly confirmed)* In the UDF filesystem (`sys/fs/udf/`), directory modifications shall read and write the directory's data extents rather than overwriting the File Entry sector, `udf_vop_mkdir` shall extract `udf_node_t` from `dvp->v_data` rather than casting `struct vnode *` to `fs_node_t *`, `udf_node_cache` shall pin active nodes under a lock, and `udf_read_file` / `udf_unmount` / `udf_add_fid` shall bound reads and free all allocated buffers.
  Evidence:
  1. `sys/fs/udf/udf_write.c:900, 924, 956, 972` (`udf_add_fid`, `udf_remove_fid`) read and write raw sectors at `fs->partition_start + dir_block` (the directory's `struct udf_fe` metadata sector) instead of the directory's data blocks, and `udf_add_fid` (`:960`) returns `0` without calling `kfree(dir_buf, 4096)`.
  2. `sys/fs/udf/udf.c:511-539, 943` (`udf_alloc_node`) uses an unpinned, unlocked round-robin array `udf_node_cache[64]` that includes `root_node`, overwriting `root_node` and open files after 64 lookups.
  3. `sys/fs/udf/udf.c:742, 748` (`udf_vop_mkdir`) casts `struct vnode *dvp` directly to `(fs_node_t *)dvp`.
  4. `sys/fs/udf/udf.c:543` (`udf_vfs_read`) casts signed `off_t` to `uint32_t` without rejecting `offset < 0`, and `udf_read_file` (`:359-469`) never bounds short/long AD reads against `fe->info_length`.
  5. `sys/fs/udf/udf.c:765-782` (`udf_unmount`) and error paths in `udf_mount` (`:928-945`) leak `fs->space_bitmap_base` (`8 KiB`).
  Basis: traced.

- [x] **SYS-FS-003** *(Critical → High; partly confirmed)* In the Minix filesystem (`sys/fs/minix/minix.c`), double-indirect and indirect zone numbers shall be validated with `minix_zone_ok` and 64-bit `minix_zone_off`, `minix_dir_add` shall use `minix_dirent_size(fs)` on 14-char volumes, `minix_link` shall not cast V2 directory inodes to `struct minix_inode_v1 *`, `s_log_zone_size` and negative read offsets shall be validated, and `fs_node_t` allocations shall be reclaimed on close.
  Evidence:
  1. `sys/fs/minix/minix.c:174` (`minix_get_zone_v1`) and `:223` (`minix_get_zone_v2`) return `buf[double_indirect_index % entries_per_block]` without calling `minix_zone_ok(fs, out)`; `minix_set_zone_v1/v2` (`:405-515`) and `minix_free_all_zones` (`:701-834`) multiply zone indices by `MINIX_BLOCK_SIZE` in 32-bit without `minix_zone_ok` or `minix_zone_off`.
  2. `sys/fs/minix/minix.c:1109-1131` (`minix_dir_add`) hardcodes 32-byte `struct minix_dirent_v1`, corrupting 16-byte directory slots on 14-character Minix filesystems.
  3. `sys/fs/minix/minix.c:1386-1389` (`minix_link`) casts `dir->ptr` to `(struct minix_inode_v1 *)` on both V1 and V2 mounts, overwriting `i_uid`/`i_gid` on V2 inodes.
  4. `sys/fs/minix/minix.c:313` (`minix_statfs`) shifts by unvalidated `fs->sb.s_log_zone_size`; `minix_read` (`:838`) does not reject `offset < 0`; `minix_finddir` (`:996`) and `minix_read_inode` (`:575`) allocate `fs_node_t` and `minix_inode_wrapper` with no `node->close` callback, and `minix_rmdir` (`:1523-1559`) frees `target->ptr` without freeing `target`.
  Basis: traced.

- [x] **SYS-FS-004** *(High → not a defect; refuted — closed, no change)* When reading or listing extended attributes in `sys/fs/ext2/ext2_xattr.c`, the ext2 driver shall restrict the `trusted.*` (`EXT4_XATTR_INDEX_TRUSTED`) namespace to superuser callers (`current_process->euid == 0`).
  Evidence: `sys/fs/ext2/ext2_xattr.c:370-433` (`ext2_xattr_get`, `ext2_xattr_list`) serves and lists `EXT4_XATTR_INDEX_TRUSTED` attributes without checking `current_process->euid == 0`.
  Basis: traced.

- [x] **SYS-FS-005** *(High → Medium; partly confirmed)* When truncating or extending an ext2 file in `ext2_truncate` and `ext2_truncate_blocks`, the ext2 driver shall preserve all 64 bits of `length` via `ext2_inode_set_size`.
  Evidence: `sys/fs/ext2/ext2.c:5387-5409, 5500-5501, 5529` casts `length` to `(uint32_t)length` and writes `inode->i_size = new_size` and `node->length = (uint32_t)length` without updating `i_dir_acl` (`i_size_high`), corrupting files `>= 4 GiB`.
  Basis: traced.

- [x] **SYS-FS-006** *(High; confirmed)* In the FAT driver (`sys/fs/fat/fat.c`), `fat_truncate` shall allocate and link `first_cluster` when extending an empty file (`first_cluster == 0`), `fat_rename` shall check for same-file / case-only renames before freeing the destination's cluster chain, `fat_file_read` / `fat_file_write_locked` shall reject negative or `> 4 GiB` offsets and invalid cluster `< 2`, and `fat_readdir` / `fat_mount` shall free allocated buffers on error paths.
  Evidence:
  1. `sys/fs/fat/fat.c:1546-1566` (`fat_truncate`): when `ctx->first_cluster == 0` and `new_size > 0`, `cluster` starts at `0`, `ctx->first_cluster` remains `0` while `ctx->size` becomes `new_size`, and subsequent reads map cluster `0` to sector `0` (the volume boot sector).
  2. `sys/fs/fat/fat.c:2099-2117` (`fat_rename`): looks up `dst = fat_finddir(new_parent, new_name)` (case-insensitive) and frees `dctx->first_cluster` before checking whether `src` and `dst` are the same file (`mv foo.txt FOO.TXT`), destroying the file.
  3. `sys/fs/fat/fat.c:343-366` (`fat_file_read`) and `:1444-1445` (`fat_file_write_locked`): cast 64-bit `offset` / `offset + size` to `uint32_t` without bounds checks, and `fat_file_read` does not reject `ctx->first_cluster < 2`.
  4. `sys/fs/fat/fat.c:495` (`fat_readdir`) leaks `dir_buf` on cluster-loop abort, and `:1072` (`fat_mount`) sets `fs->fat_table = NULL` without calling `kfree`.
  Basis: traced.

- [x] **SYS-FS-007** *(High; partly confirmed)* When `procfs` nodes are opened, read, or reclaimed in `sys/fs/procfs.c`, the driver shall reference-count `procfs_pid_nodes_t` across open file descriptors, avoid mutating shared static `proc_task_tid_*_node` and `nodes->fd_link` structs in place, redact `WaitChan` in `/proc/<pid>/task/<tid>/status` when `!proc_may_inspect(p)`, and reject negative offsets in `proc_pid_stat_read` and `proc_pid_cmdline_read`.
  Evidence:
  1. `sys/fs/procfs.c:874-895` (`procfs_release_pid_nodes`) calls `kfree(nodes, sizeof(*nodes))` during `wait_reap()` while open `file_t` descriptors may still hold pointers to `&nodes->status`, `&nodes->maps`, etc. (use-after-free).
  2. `sys/fs/procfs.c:1631-1634, 1753-1817` (`proc_task_tid_*_node`) and `:1464-1475` (`nodes->fd_link`) overwrite singleton `fs_node_t` structures in place on every lookup.
  3. `sys/fs/procfs.c:1869-1877` prints raw kernel `t->wait_chan` addresses in world-readable (`0444`) `/proc/<pid>/task/<tid>/status` without checking `proc_may_inspect(p)`.
  4. `sys/fs/procfs.c:1255` (`proc_pid_stat_read`) and `:1288, 1303` (`proc_pid_cmdline_read`) cast signed 64-bit `offset` to 32-bit `size_t` without checking `if (offset < 0) return 0;`.
  Basis: traced.

- [x] **SYS-FS-008** *(High → Medium; partly confirmed)* In the System V filesystem (`sys/fs/sysv/sysv.c`), the driver shall validate superblock geometry (`s_isize`, `s_fsize`), inode bounds (`ino <= fs->ninodes`), and block bounds (`block_no >= fs->first_data_block && block_no < fs->nblocks`), avoid a single unlocked static `g_dirent`, and reclaim `fs_node_t` objects without leaking on lookup/close.
  Evidence: `sys/fs/sysv/sysv.c:179` (`sysv_read_inode`) never checks `ino > fs->ninodes`; `sysv_read_block_range` (`:87`) and `sysv_resolve_block` (`:106`) never check `block_no < fs->nblocks`; `g_dirent` (`:300`) is a single global struct; and `sysv_close` (`:389-405`) frees `nd` (`node->ptr`) without freeing `node`.
  Basis: traced.
  Fixed: mount refuses `s_isize <= 2` or `s_fsize <= s_isize`; `s_isize` is now taken as the first data block (as V7 and Linux do -- the driver added 2 to it, overcounting inodes and misplacing `first_data_block`); inode numbers past the table and block numbers outside `[first_data_block, nblocks)` fail.  Nodes are refcounted (lookups' hold/rele and opens alike) and freed whole at the last release, so a walk's nodes no longer leak.  `g_dirent` is safe as it is: `readdir_fs` copies it out under the VFS readdir lock.  Test: `tests/kern/mksysvimg.py` + `sysvfs.sh` (the pre-remediation kernel returns data for an inode past the table).

- [x] **SYS-FS-009** *(Medium; confirmed)* When `fat_alloc_node` (`sys/fs/fat/fat.c:675-685`) or `exfat_alloc_node` (`sys/fs/exfat/exfat.c:707-740`) finds an existing cached node with `pin > 0`, the driver shall not `memset` the in-use `fs_node_t` and context struct or match an `orphaned` slot.
  Evidence: Both `fat_alloc_node` (`fat.c:680-681`) and `exfat_alloc_node` (`exfat.c:736-739`) execute `memset(ctx, 0, sizeof(*ctx)); memset(node, 0, sizeof(*node));` on a cache hit even when `keep_pin > 0`, and `exfat_alloc_node` (`:710`) does not exclude `exfat_node_cache[i].orphaned` from `(fs, inode)` matches.
  Basis: traced.
  Fixed with SYS-NEW-001b (a6e0e3c23, 9318b0c16): a pinned slot is returned as it is, never rebuilt; a pinned slot that is orphaned or whose first cluster differs is another file, and the new one gets its own slot.

- [x] **SYS-FS-010** *(Medium → Low; partly confirmed)* In `sys/fs/ext2/ext2.c`, `ext2_read_blocks` shall check `fs->block_size * count` for multiplication overflow and shall not `memset` the caller's buffer with an unchecked product on out-of-range block errors, and `ext2_setattr` shall enforce `EXT2_IS_IMMUTABLE(ctx)`.
  Evidence: `sys/fs/ext2/ext2.c:297-301` executes `if (buffer) memset(buffer, 0, fs->block_size * count);` on the invalid-block error path without overflow checking, and `ext2_setattr` (`:2407-2475`) omits `if (EXT2_IS_IMMUTABLE(ctx)) return -EPERM;`.
  Basis: traced.

- [x] **SYS-FS-011** *(Medium → Low; confirmed)* In the 9P filesystem (`sys/fs/9p.c`), `p9_version` shall validate the server's negotiated `msize` and version string, `p9_vfs_read` shall reject negative `offset`, and `next_tag`/`next_fid` allocation shall be synchronized.
  Evidence: `sys/fs/9p.c:104-108` ignores the payload of `P9_RVERSION`, `p9_vfs_read` (`:174, 231`) casts signed `off_t` to `uint64_t` without checking `offset < 0`, and `p9_alloc_tag`/`p9_alloc_fid` (`:29-51`) increment `fs->next_tag` and `fs->next_fid` without locking.
  Basis: traced.

- [x] **SYS-FS-012** *(Medium; confirmed)* When registering a device alias in `devfs_register_alias_perso`, `sys/fs/devfs.c` shall hold `devfs_lock` around `devfs_lookup_path` and `devfs_add_entry`.
  Evidence: `sys/fs/devfs.c:745-808` (`devfs_register_alias_perso`) mutates the devfs tree without acquiring `devfs_lock`, violating the locking contract documented at line 121.
  Basis: traced.

- [x] **SYS-FS-013** *(Low; confirmed)* When `ext2_read_inode`, `ext2_write_inode_ex`, or `ext2_remove_entry` fails in `sys/fs/ext2/ext2.c`, the function shall return a negative errno (`-EIO` or `-ENOMEM`) rather than `-1` (`-EPERM`).
  Evidence: `sys/fs/ext2/ext2.c:334, 1069` return `-1` when `group >= fs->group_count`, and line 6160 returns `-1` on `kmalloc` failure.
  Basis: traced.

- [x] **SYS-FS-014** *(Low; partly confirmed)* In `sys/fs/shmfs.c` and `sys/fs/sysfs.c`, `shmfs_root_readdir` shall populate `d_ino`, and `shmfs_root_finddir` / `sysfs_finddir` / `sysfs_leaf_finddir` shall handle `"."` and `".."`.
  Evidence: `sys/fs/shmfs.c:471-487` leaves `shmfs_dirent.d_ino` unset, and `shmfs_root_finddir` (`:512-519`) and `sysfs_finddir` (`sys/fs/sysfs.c:86-132`) do not resolve `"."` or `".."`.
  Basis: traced.

---

## 6. Networking, Executable Loaders & Personalities (`sys/net/`, `sys/exec/`)

- [x] **SYS-EXEC-001** *(Critical; confirmed)* Every entry in the Linux personality syscall table (`linux_syscalls` in `sys/exec/perso/perso_linux.c`) that accepts user-space pointers shall point to a `sys_*` or `linux_sys_*` wrapper that validates and copies user memory via `copyin`/`copyout`/`copyinstr`, rather than an internal `kern_*` function.
  Evidence: `sys/exec/perso/perso_linux.c:1013-1171` wires 25+ user-callable Linux syscalls directly to `kern_*` helpers that dereference pointers in Ring 0 without user-address validation: `LINUX_SYS_waitpid` (`:1020` -> `kern_waitpid`), `LINUX_SYS_link` (`:1022` -> `kern_link`), `LINUX_SYS_unlink` (`:1023` -> `kern_unlink`), `LINUX_SYS_chdir` (`:1024` -> `kern_chdir`), `LINUX_SYS_time` (`:1025` -> `kern_time`), `LINUX_SYS_stime` (`:1034` -> `kern_stime`), `LINUX_SYS_access` (`:1043` -> `kern_access`), `LINUX_SYS_rename` (`:1049` -> `kern_rename`), `LINUX_SYS_mkdir` (`:1050` -> `kern_mkdir`), `LINUX_SYS_rmdir` (`:1051` -> `kern_rmdir`), `LINUX_SYS_times` (`:1054` -> `kern_times`), `LINUX_SYS_acct` (`:1062` -> `kern_acct`), `LINUX_SYS_sigaction` (`:1072` -> `kern_sigaction`), `LINUX_SYS_sigsuspend` (`:1075` -> `kern_sigsuspend`), `LINUX_SYS_sigpending` (`:1076` -> `kern_sigpending`), `LINUX_SYS_sethostname` (`:1077` -> `kern_hostname`, which writes the hostname into `buf`), `LINUX_SYS_gettimeofday` (`:1081` -> `kern_gettimeofday`), `LINUX_SYS_symlink` (`:1084` -> `kern_symlink`), `LINUX_SYS_readlink` (`:1086` -> `kern_readlink`), `LINUX_SYS_setitimer` (`:1099` -> `kern_setitimer`), `LINUX_SYS_getitimer` (`:1100` -> `kern_getitimer`), `LINUX_SYS_wait4` (`:1104` -> `kern_wait4`), `LINUX_SYS_sigprocmask` (`:1112` -> `kern_sigprocmask`), `LINUX_SYS_getdents` (`:1119` -> `kern_getdents`), `LINUX_SYS_rt_sigpending` (`:1168` -> `kern_sigpending`), `LINUX_SYS_rt_sigtimedwait` (`:1169` -> `kern_sigtimedwait`), and `LINUX_SYS_rt_sigsuspend` (`:1170` -> `kern_sigsuspend`).
  Basis: traced.

- [x] **SYS-EXEC-002** *(Critical → High; confirmed)* If an executable load fails in `aout_load`, `coff_load`, `elks_load`, `xout_load`, or `x286_load`, then the loader shall restore the calling process's original `pmap` and `vm_map` and destroy the partially constructed address space.
  Evidence: `sys/exec/formats/aout.c:501-586`, `sys/exec/formats/coff.c:417-508`, `sys/exec/formats/elks_aout.c:455-600`, and `sys/exec/formats/xout.c:341-565, 1085-1283` overwrite `current_process->pmap = pmap`, call `pmap_activate(pmap)`, and destroy `current_process->vm_map` before fallible steps (`coff_load_shlib`, `aout_build_stack`, `x286_build_stack`, short file reads, LDT allocation) complete.
  Basis: traced.

- [x] **SYS-EXEC-003** *(Critical → High; partly confirmed)* When constructing the initial user stack in `aout_build_stack`, `xout_build_stack`, and `exec_setup_stack`, the loader shall verify that the stack pointer remains within the mapped stack region before every write, register the stack region in `current_process->vm_map`, and free allocated stack pages on OOM.
  Evidence: `sys/exec/formats/aout.c:322-427` (`aout_build_stack`) maps 128 KiB (`[0xBFFE0000, 0xC0000000)`) and copies up to 32 MiB of `argv`/`envp` strings via `sp -= len; memcpy((void *)(uintptr_t)sp, ...)` with no check that `sp >= 0xBFFE0000` (and leaks mapped pages at `oom:`). `xout_load` (`sys/exec/formats/xout.c:186-237, 478-486`) maps 64 KiB while `xout_build_stack` can write up to 1008 KiB. In `sys/exec/formats/elf.c:1304-1388` (`exec_setup_stack`), overflowing the 128 KiB stack causes `stack_write` to drop writes while `exec_setup_stack` still returns `0`, and neither `elf.c` nor `aout.c` inserts a stack `vm_map_entry` into `current_process->vm_map`.
  Basis: traced.

- [x] **SYS-EXEC-004** *(Critical → High; partly confirmed)* When restoring signal context in `linux_sys_sigreturn`, `linux_sys_rt_sigreturn`, `freebsd_sys_sigreturn`, `netbsd_sys_sigreturn`, and `openbsd_sys_sigreturn`, the personality handler shall validate segment selectors, clear `sigmask(SIGKILL) | sigmask(SIGSTOP)` from `sig_mask`, set `current_thread->frame_replaced = 1`, and translate foreign signal masks.
  Evidence: `sys/exec/perso/linux/linux_sig.c:186-193, 216-223`, `sys/exec/perso/freebsd/freebsd_sig.c:457-465`, `sys/exec/perso/netbsd/netbsd_sig.c:241-249`, and `sys/exec/perso/openbsd/openbsd_sig.c:220-228` assign `regs->cs = sc.cs | 3`, `regs->ds = sc.ds | 3`, etc. without validating descriptor bounds/types, do not strip `SIGKILL`/`SIGSTOP` from `sig_mask`, and `linux_sig.c` / `openbsd_sig.c` omit `current_thread->frame_replaced = 1` and Linux signal bitmask translation.
  Basis: traced.

- [x] **SYS-EXEC-005** *(High; partly confirmed)* When copying multi-byte buffers between the kernel and a 16-bit ELKS process in `sys/exec/perso/perso_elks.c`, the personality layer shall validate the entire buffer range with `elks_ds_span` rather than single-byte `elks_ds_pointer`.
  Evidence: `sys/exec/perso/perso_elks.c:1159, 1171, 1518, 1540, 1562, 1584, 2351, 2381, 2397` (`elks_sys_read`, `elks_sys_write`, `elks_sys_waitpid`, `elks_sys_getpid`, `elks_sys_getuid`, `elks_sys_getgid`, `elks_sys_ioctl`) call `elks_ds_pointer(off, &linear)` (which checks only `off <= limit`) before multi-byte `copyin`/`copyout`/`memcpy`.
  Basis: traced.

- [x] **SYS-EXEC-006** *(High → Low; partly confirmed)* When handling `MAP_ALIGNED(n)` in `freebsd_sys_mmap` (`sys/exec/perso/compat.c`), the kernel shall reject alignment shifts `>= 28` (or `>= 31`), page-align `len`, and check `len + align` for overflow before mapping and unmapping trim regions.
  Evidence: `sys/exec/perso/compat.c:169-183` checks only `align_shift > 12`. Passing `align_shift >= 31` causes shift UB in `(size_t)1 << align_shift`, integer overflow in `over = len + align`, and integer underflow in `tail = over - head - len`, passing a huge `tail` to `sys_munmap`.
  Basis: traced.

- [x] **SYS-EXEC-007** *(High → not a defect; refuted — closed, no change)* When loading `a.out` or COFF binaries in `aout_load` and `coff_map_sections`, the loader shall reject segments whose `bss_end` or `sec_end` exceeds `USER32_VA_END` (`0xC0000000`).
  Evidence: `sys/exec/formats/aout.c:468-478` checks `bss_end < data_end` but never `bss_end > aout_User_End` (`0xC0000000`), and `sys/exec/formats/coff.c:260-275` checks `sec_end < sec_start` but never `sec_end > 0xC0000000`.
  Basis: traced.

- [x] **SYS-EXEC-008** *(Medium → not a defect; refuted — closed, no change)* When `script_load` invokes `kern_execve` with a kernel-stack `new_argv` vector for an `a.out` or COFF interpreter, `aout_capture_vector` shall accept kernel-space vector pointers (`>= KERN_BASE`), matching `elf.c` and `xout.c`.
  Evidence: `sys/exec/formats/script.c:96-117` builds `new_argv[4]` on the kernel stack, and `aout_capture_vector` (`sys/exec/formats/aout.c:171-225`) unconditionally passes `uvec` to `copyin`/`copyinstr`, failing with `-EFAULT`.
  Basis: traced.

- [x] **SYS-EXEC-009** *(Medium → Low; partly confirmed)* When loading an ELF `PT_LOAD` segment in `elf_load`, the loader shall not underflow `uint32_t file_off = phdr[i].p_offset + (va - phdr[i].p_vaddr)` when `va < phdr[i].p_vaddr` and `phdr[i].p_offset < (phdr[i].p_vaddr - va)`.
  Evidence: `sys/exec/formats/elf.c:778-799` computes `uint32_t file_off = phdr[i].p_offset + (va - phdr[i].p_vaddr);` at `va = start` (`p_vaddr & ~0xFFF`), which wraps around `0` if `p_offset < (p_vaddr & 0xFFF)`.
  Basis: traced.

- [x] **SYS-EXEC-010** *(Medium; partly confirmed)* When a personality `execve` wrapper allocates temporary path or argument buffers, those buffers shall be registered with `exec_cleanup_push`, and all executable loaders (`aout`, `coff`, `xout`, `x286`) shall call `exec_cleanup_drain` on success.
  Evidence: `sys/exec/perso/perso_sysv386.c:867-887`, `perso_xenix.c:1467-1499`, `perso_linux.c:559-564`, `freebsd/freebsd_user.c:309-313`, and `sunos/sunos_user.c:412-426` allocate buffers freed only after `kern_execve` returns, and `aout.c`, `coff.c`, and `xout.c` never call `exec_cleanup_drain()`.
  Basis: traced.

- [x] **SYS-EXEC-011** *(Medium; partly confirmed)* In the SVR4, FreeBSD, NetBSD, and IPC personality wrappers, `svr4_sys_lchown` shall call `kern_lchown` rather than passing a kernel stack pointer to `sys_lchown`, `sys_utime(path, NULL)` shall verify owner or write permission, `pread`/`pwrite` wrappers shall not mutate shared `f->f_offset` via `kern_lseek`, and `perso_ipc_shmat_PageAddr` shall detach the segment if `copyout` fails.
  Evidence: `sys/exec/perso/svr4/svr4_calls.c:438-447` passes kernel buffer `kpath` to `sys_lchown` (which fails in `copyinstr`); `sys/exec/perso/compat.c:473-478` omits permission checks when `times == NULL`; `sys/exec/perso/freebsd/freebsd_user.c:682-702, 1075-1167` implements `pread`/`pwrite` with `kern_lseek` + `sys_read`/`sys_write` + `kern_lseek`; and `sys/exec/perso/perso_ipc_shm.c:128-138` leaves `shmat` attached when `copyout` returns an error.
  Basis: traced.
  Resolution: SVR4 lchown calls the new `kern_lchown`.  `kern_pread`/`kern_pwrite` (and `sys_pread_at`, `sys_preadv_at`, ...) do positioned I/O; the FreeBSD and NetBSD pread/pwrite/preadv/pwritev and FreeBSD `copy_file_range` use them.  `sys_utime` (no caller) checks write permission for `times == NULL`.  Linux `SHMAT` is left as it was: Linux itself keeps the segment attached when the result cannot be written back (checked on a Linux host, `shm_nattch` 1).

- [x] **SYS-NET-001** *(High → Medium; confirmed)* When `sendmsg_kiov` on an `AF_UNIX` socket processes `SCM_RIGHTS` descriptors (`sys/net/af_unix.c`), the socket layer shall release rolled-back descriptors via `file_close_ptr` rather than raw `__sync_fetch_and_sub(&qf->f_count, 1)`, and shall not leave descriptors queued in `peer->rx_fdq` when the payload send fails.
  Evidence: `sys/net/af_unix.c:2388-2392` (`fd_fail:`) decrements `qf->f_count` directly without calling `file_close_ptr(qf)` if a concurrent `close()` dropped the last user reference, and lines 2372–2437 enqueue descriptors into `peer->rx_fdq` before `sock_dgram_sendv` / `sys_send` can fail.
  Basis: traced.

- [x] **SYS-NET-002** *(High → Medium; confirmed)* When rebinding an `AF_PACKET` socket in `afpacket_bind`, the socket layer shall synchronize `s->sub` teardown against concurrent readers in `afpkt_node_read` and `afpacket_recvfrom`.
  Evidence: `sys/net/af_packet.c:199-211` calls `netdev_unsubscribe(s->sub)` (which `kfree`s `sub->ring` and `sub` in `sys/net/netdev.c:423-425`) while concurrent threads in `afpkt_node_read` (`:67-106`) or `afpacket_recvfrom` (`:279-343`) read `s->sub` and sleep on `netdev_sub_wait_chan(s->sub)` without a lock.
  Basis: traced.

- [x] **SYS-NET-003** *(High → Medium; confirmed)* When replying to an ICMPv6 Echo Request in `icmp6_handle_echo`, the network stack shall not allocate a 1500-byte buffer on the hard-IRQ stack and shall compute the ICMPv6 pseudo-header checksum using the unicast source address (`dev->ip6_addr`) when the request destination `daddr` is multicast.
  Evidence: `sys/net/icmp.c:445` allocates `uint8_t reply[1500];` on the stack in hard-IRQ context (`netdev_rx -> ip6_input -> icmp6_input`), and line 456 passes multicast `daddr` as the source address to `inet_csum_pseudo6` even though `ip6_output` (`sys/net/inet6.c:427`) substitutes `dev->ip6_addr`.
  Basis: traced.

- [x] **SYS-NET-004** *(Medium → Low; partly confirmed)* In `ip6_input` (`sys/net/inet6.c`), packets exceeding the 8-header extension limit shall be dropped, and raw socket delivery via `afinet_deliver_v6` shall receive the upper-layer protocol `nh` and payload `(p, plen)`.
  Evidence: `sys/net/inet6.c:364-386` exits the 8-iteration extension header loop without dropping the packet when `nh` is still an extension header, and line 401 passes `h->next_header, data, len` instead of `nh, p, plen` to `afinet_deliver_v6`.
  Basis: traced.

- [x] **SYS-NET-005** *(Medium → Low; partly confirmed)* In `sys/net/icmp.c`, `sys/net/netdev.c`, and `sys/net/af_inet.c`, ICMP reply transmission shall not call `ip4_output_opts` / `ip6_output` with interrupts disabled around static buffers (which blocks ARP/ND6 resolution), and `g_netdevs` / `dev->ip4_alias[]` accesses shall be synchronized against concurrent readers.
  Evidence: `sys/net/icmp.c:223-249, 354-365` holds `intr_disable()` across `ip4_output_opts`/`ip6_output` (which return `-EHOSTUNREACH` whenever `!intr_enabled()` and ARP/ND6 is uncached); `sys/net/netdev.c:124-140` walks `g_netdevs` without `g_netdev_lock`; and `sys/net/af_inet.c:416-420` mutates `dev->ip4_alias[]` without IRQ synchronization.
  Basis: traced.
  Resolution: adding and removing an address (`SIOCAIFADDR`, `SIOCDIFADDR`) now runs with interrupts disabled, so the receive path, which reads the table from the NIC interrupt, never sees it half changed.  Not changed: the ICMP replies already run in interrupt context, so the `intr_disable` there changes nothing, and dropping a reply to an unresolved next hop after sending the ARP/ND request is by design (it cannot wait in an interrupt).  The netdev list only grows, so lockless readers are safe.

---

## 7. Device Drivers & Kernel Coding Rules (`sys/drivers/`, style rules)

- [x] **SYS-DRV-001** *(Critical → Low; partly confirmed)* In `/dev/mem`, `/dev/kmem`, `/dev/fb0`, and AC'97 device drivers, `mem_read` and `mem_write` shall reject negative offsets, `kmem_read` and `kmem_write` shall verify `pmap_extract(pmap_kernel(), va)` per page before `memcpy`, and `MAP_FIXED` device `mmap` handlers shall validate that `[addr, addr + length)` lies within user virtual address bounds.
  Evidence: `sys/drivers/devices/mem.c:124, 166` compares signed `off_t offset >= MEM_DIRECT_MAP_LIMIT` without checking `offset < 0`; `sys/drivers/devices/kmem.c:100-158` copies from `kva_start >= 0xC0000000` without verifying `pmap_extract`; and `mem_mmap` (`mem.c:83-85`), `fb_fs_mmap` (`sys/drivers/video/fb.c:984-987`), and `ac97_mmap` (`sys/drivers/audio/ac97.c:737-744`) pass unvalidated `MAP_FIXED` addresses to `vm_map_remove` and `vm_map_insert`.
  Basis: traced.

- [x] **SYS-DRV-002** *(Critical → not a defect; refuted — closed, no change)* When `/dev/ntsync` or `/dev/ptmx` is opened via `open(2)`, the driver shall allocate a private per-open `fs_node_t` rather than overwriting and freeing the global devfs singleton `fs_node_t.impl`.
  Evidence: `sys/drivers/devices/ntsync.c:1263-1294` (`ntsync_open_callback`, `ntsync_close`) stores the per-open `struct ntsync_instance *` in global `ntsync_device.impl`, overwriting and leaking prior instances on every `open("/dev/ntsync")` and freeing `ntsync_device.impl` on the first `close()`, causing a use-after-free for all other open descriptors. `sys/drivers/console/pty.c:868-915` (`ptmx_open`) similarly overwrites global `ptmx_node.impl`.
  Basis: traced.

- [x] **SYS-DRV-003** *(High → Critical; partly confirmed)* When handling terminal ioctls in `tty_ioctl` (`sys/drivers/console/tty.c`) and personality ioctl entry points (`freebsd_sys_ioctl`, `elks_sys_ioctl`), the kernel shall not bypass `copyin`/`copyout` merely because a user-supplied `arg` is `>= KERN_BASE`.
  Evidence: `sys/drivers/console/tty.c:1369-1400` sets `int kernel_arg = ((uintptr_t)arg >= KERN_BASE);` and uses `memcpy` when true, while `freebsd_sys_ioctl` (`sys/exec/perso/compat.c`) passes user `arg` directly to `kern_ioctl` without checking `arg < KERN_BASE`.
  Basis: traced.

- [x] **SYS-DRV-004** *(High → Medium; partly confirmed)* When recording an input event in `input_report_event` and reading events in `input_read` (`sys/drivers/input/input_subsys.c`), the input subsystem shall obtain timestamps via `kern_gettimeofday` rather than `sys_gettimeofday` and shall not copy directly to user space while holding an IRQ-disabled spinlock.
  Evidence: `sys/drivers/input/input_subsys.c:170-176` calls `sys_gettimeofday(&tv, NULL)` with a kernel stack pointer `&tv` (which fails `copyout` with `-EFAULT`, leaving `tv` uninitialized and leaking 8 bytes of kernel stack per event), and `input_read` (`:451-479`) copies to user `buffer` while holding `input_lock` with interrupts disabled.
  Basis: traced.

- [x] **SYS-DRV-005** *(High → Medium; partly confirmed)* When waiting for an event in `ntsync_wait_any` and `ntsync_wait_all` (`sys/drivers/devices/ntsync.c`), the driver shall not drop `inst->lock` before entering the sleep queue and shall abort the wait when a signal is pending.
  Evidence: `sys/drivers/devices/ntsync.c:811-841, 1172-1194` releases `inst->lock` before calling `sched_sleep(&q.triggered)` (losing wakeups that arrive in the window) and never checks for pending signals when `timeout == UINT64_MAX`.
  Basis: traced.

- [x] **SYS-DRV-006** *(High; partly confirmed)* In `sys/drivers/video/fb.c`, `fb_init` shall register `bga_install()` only once, and `fb_client.pmap` shall be cleared on `execve` and synchronized before `pmap_remove_range`.
  Evidence: `sys/drivers/video/fb.c:1223-1226` calls `bga_install()` inside `#ifdef ENABLE_BGA` and immediately calls `bga_install()` a second time, linking `bga_driver.next = &bga_driver` in an infinite loop. `fb_set_offscreen` (`:786-839`) drops `fb_shadow_lock` before calling `pmap_remove_range(pmap, ...)`, and `execve` does not clear `fb_client`.
  Basis: traced.

- [x] **SYS-DRV-007** *(High → Low; refuted as written)* When parsing and freeing PCF and BDF fonts in `sys/drivers/video/bdf_pcf.c`, the parser shall validate `toc_count` and `glyph_offsets` against buffer bounds, ensure BDF input is NUL-terminated, and pass `sizeof(uint8_t)` to `kfree` for `font->glyph_widths`.
  Evidence: `sys/drivers/video/bdf_pcf.c:77-78, 194-203` reads `toc_count` and `glyph_offsets[glyph_index]` without bounds checks; `bdf_parse` (`:225`) runs `strstr` on unverified `data`; and `bdf_free` (`:375`) calls `kfree(font->glyph_widths, font->glyph_count * sizeof(uint16_t))` for an array allocated with `sizeof(uint8_t)` (`:288`).
  Basis: traced.

- [x] **SYS-DRV-008** *(High → Medium; confirmed)* When probing IDE controllers in `ide_scan_controller` (`sys/drivers/storage/ide/ide_probe.c`), the driver shall not invoke `geom_scan_partitions` a second time after `ide_register_blkdev` (`blkdev_register`) has already scanned the disk.
  Evidence: `sys/drivers/storage/ide/ide_probe.c:796, 809-811, 831-833` calls `ide_register_blkdev` (which calls `blkdev_register` -> `geom_scan_partitions`) and then calls `geom_scan_partitions(&ide_blkdevs[idx])` again, leaking the first `geom_disk_t` and registering duplicate partition devices.
  Basis: traced.

- [x] **SYS-DRV-009** *(High → Medium; partly confirmed)* When executing non-data (`data_len == 0`) ATAPI/SATAPI SCSI commands in `ahci_scsi_execute` (`sys/drivers/storage/ahci/ahci.c`) and `ide_atapi_packet` (`sys/drivers/storage/ide/ide_atapi.c`), the drivers shall issue the command with zero data phase rather than failing or waiting for `ATA_SR_DRQ`.
  Evidence: `sys/drivers/storage/ahci/ahci.c:1418` rejects `cmd->data_len == 0 || cmd->data == NULL` with `-1`, and `sys/drivers/storage/ide/ide_atapi.c:57-62, 95` waits for `ATA_SR_DRQ` during the data phase even when `len == 0`.
  Basis: traced.

- [x] **SYS-DRV-010** *(High → Low; partly confirmed)* When submitting commands and polling completion queues in `nvme_admin_submit_sync` and `nvme_io_submit_sync` (`sys/drivers/storage/nvme/nvme.c`), the NVMe driver shall serialize queue access with a lock and read completion queue entries through `volatile` / memory barriers.
  Evidence: `sys/drivers/storage/nvme/nvme.c:166-237, 635-695` mutates `sq_tail`, `cq_head`, and `next_cid` without any lock and polls `cqe->status` via a non-`volatile` pointer.
  Basis: traced.

- [x] **SYS-DRV-011** *(Medium; partly confirmed)* In the audio subsystem (`sys/drivers/audio/`), `sb16_write` shall bound its completion wait and avoid `(copy_len / 2) - 1` underflow when `copy_len == 1`, `ac97_flush` shall hold `d->feed_lock`, `ac97_attach` shall free `d->fifo_buf` on registration failure, and `audio_node_close` shall decrement `dev->open_count` rather than forcing it to `0`.
  Evidence: `sys/drivers/audio/sb16.c:307-324` spins forever if the IRQ is masked and underflows `samples = (uint16_t)((copy_len / 2) - 1)` to `0xFFFF` when `copy_len == 1`; `sys/drivers/audio/ac97.c:640-653` resets ring indices without `d->feed_lock` and leaks 256 KiB `d->fifo_buf` at `:971`; and `sys/drivers/audio/audio.c:1139-1142` sets `dev->open_count = 0` unconditionally.
  Basis: traced.

- [x] **SYS-DRV-012** *(Medium → Low; partly confirmed)* In the VirtIO drivers (`sys/drivers/virtio/`), `virtio_gpu` shall not place DMA command/response buffers on the kernel stack without queue locking, `virtio_9p` / `virtio_scsi` / `virtio_gpu` shall enable `PCI_COMMAND_IO | PCI_COMMAND_MASTER`, `virtio_scsi_isr` shall acknowledge `VIRTIO_REG_ISR_STATUS`, and `virtio_scsi` shall free already-allocated DMA pages on partial allocation failure.
  Evidence: `sys/drivers/virtio/virtio_gpu.c:203-306` passes `V2P(&cmd)` for stack variables to the DMA ring without locking `vq_ctrl`; `virtio_9p.c:39-144`, `virtio_scsi.c:697-800`, and `virtio_gpu.c:308-340` never set `PCI_COMMAND_MASTER`; `virtio_scsi.c:689-695` never reads `VIRTIO_REG_ISR_STATUS`; and `virtio_scsi.c:769-792` leaks `dev->ctrl_buf`, `dev->req_buf`, and `dev->extra_req_bufs[]` if a later `pmm_alloc_block()` fails.
  Basis: traced.

- [x] **SYS-DRV-013** *(Medium → Low; partly confirmed)* In the USB drivers (`sys/drivers/usb/uac.c`, `usb_msc.c`, `uas.c`), attach error paths shall stop any spawned kernel thread (`uac-feed`), free all allocated DMA and FIFO buffers, and terminate `uac_feeder` via `kthread_exit()`.
  Evidence: `sys/drivers/usb/uac.c:738-744` sets `d->in_use = 0` on `audio_register_device` failure while `uac-feed` is still running and leaks `d->fifo_buf` (64 KiB) and `d->ring`; `uac_feeder` (`:279`) returns instead of calling `kthread_exit()`; `sys/drivers/usb/usb_msc.c:565-569` and `sys/drivers/usb/uas.c:317-321` leak DMA buffers when `scsi_register_link` fails.
  Basis: traced.

- [x] **SYS-RULE-001** *(Low; partly confirmed)* Kernel C source files under `sys/` shall not contain `extern` declarations, declaring shared functions and variables in headers instead (Kernel Code Style Rule 2).
  Evidence: 27 non-generated `extern` declarations appear across `.c` files in `sys/`, including `sys/arch/i386/efi_boot.c:439`, `sys/arch/x86-common/trap.c:84`, `sys/arch/x86-common/pmm.c:217-218, 608`, `sys/arch/x86-common/lapic.c:20-21, 181-182`, `sys/arch/x86-common/pci.c:31-34`, `sys/boot/stage2.c:19-20`, `sys/kern/efi_runtime.c:65`, `sys/kern/panic.c:60`, `sys/exec/formats/elf.c:41`, `sys/exec/perso/perso_freebsd.c:56`, `sys/exec/perso/svr4/svr4_streams.c:13-14`, `sys/drivers/console/console.c:16`, `sys/drivers/devices/cpuid.c:19`, `sys/drivers/devices/pseudo.c:20`, `sys/drivers/storage/ide/ide.c:112`, `sys/drivers/storage/nvme/nvme.c:46-49`, `sys/drivers/usb/ehci.c:45-46`, `sys/drivers/usb/uhci.c:44-46`, `sys/drivers/video/bga.c:35`, `sys/drivers/video/fb.c:46, 52`, `sys/drivers/video/fb_console.c:16-17, 134`, and `sys/drivers/video/vga.c:775, 1110`.
  Basis: reproduced.

- [x] **SYS-RULE-002** *(Low; partly confirmed)* Kernel source files under `sys/` shall not use relative or quoted `#include "..."` directives, specifying all header paths relative to `sys/` or `sys/include/` with `<...>` (Kernel Code Style Rule 3).
  Evidence: Quoted/relative includes appear in `sys/kern/subr_copy.c:14` (`#include "sched.h"`), `sys/arch/i386/boot.S:2`, `sys/arch/i386/multiboot_hdr.S:2`, `sys/exec/formats/aout.c:23` (`#include "../perso/freebsd/freebsd_user.h"`), `sys/exec/formats/coff.c:1`, `sys/fs/ext2/ext2_xattr.c:39` (`<ext2/ext2.h>` instead of `<fs/ext2/ext2.h>`), `sys/fs/ext2/ext2_hash.c:21`, and across `sys/exec/perso/*/*.c`, `sys/drivers/storage/ide/*.c`, `sys/drivers/storage/scsi/*.c`, `sys/drivers/video/*.c`, and `sys/drivers/virtio/*.c`.
  Basis: reproduced.

- [x] **SYS-RULE-003** *(Low; confirmed)* Kernel C source files under `sys/` shall not duplicate or define shared interface macros, constants, or structures that belong in header files (Kernel Code Style Rule 4).
  Evidence: `sys/vm/vm_syscalls.c:24-36, 710-712` redefines `PROT_*`, `MAP_*`, `MCL_*`, and `MS_*` instead of including `<sys/mman.h>`; `sys/fs/pipe.c:562-576` redefines `O_RDONLY`, `O_WRONLY`, `O_RDWR`, and `O_NONBLOCK` instead of including `<sys/fcntl.h>`; `sys/fs/minix/minix.c:1299-1302, 1404-1407` redefines `struct minix_dirent_v1` locally inside functions despite `<fs/minix/minix.h>`; and `sys/arch/i386/sched.c:97-104, 206-213` defines `fork_regs_t` locally instead of using `registers_t` from `<arch/i386/idt.h>`.
  Basis: reproduced.

---

## 8. Verification results

Each finding was checked against `sys/` at `0f4036d53` by three reviews (trace
the path; try to refute; establish who can reach it and what follows) and a
judge.  For each: the verdict, the severity as verified, what does not hold or
limits the finding, and corrected line numbers.  *Confirmed* means the finding
holds as written (wording slips aside); *partly confirmed* means the defect is
real but part of the evidence, mechanism or impact is wrong; *refuted* means
the defect described does not exist.

### 8.1 Virtual Memory

**SYS-VM-001** — confirmed, Critical.  The ring-0 write needs the target page
already present and read-only (it is, after fork, or after one touch).  A
loader that set `CR0.WP` would hide the bug, but the kernel never sets it.
Lines: `mmap_get_shared_backing_object` is `vm_syscalls.c:138-213`; the fork
write-clear is `pmap.c:782/785`.

**SYS-VM-002** — partly confirmed, Critical.  The COW half holds: a ptrace
POKE into a child's text rewrites the shared vnode page that every process
running that binary executes, root's included (`elf.c:787-797` maps text
straight from the inode-keyed object).  The i386 half is wrong:
`pmap.c:1571` rejects `uva >= KERN_BASE`, so neither the trampoline nor kernel
VAs are writable, and the missing `PTE_U` test has no effect below
`KERN_BASE`.  The `0x40000000` pa bound is latent.  The x86_64 limits only
cause `EFAULT`.  Lines: i386 `pmap_copyout_other` is `pmap.c:1558-1611`
(guard :1571, memcpy :1607); x86_64 `pmap_copy_other` is `:755-783` (checks at
:759, :766); ptrace POKE `ptrace.c:327-347`, `ptrace_fault_in` `:207-213`.

**SYS-VM-003** — confirmed, Critical.  There is no `PG_VOTED`; the gate is
`PG_INACTIVE`, `PG_DIRTY`, `PG_BUSY` and `wire_count`.  The page dropped from
the inactive queue is a page leak only.  Lines: `pv_remove_all` is
`vm_page.c:499-518`; `vm_page_try_to_free` `:861-876`;
`vm_page_reclaim_inactive_clean` `:878-907`.

**SYS-VM-004** — partly confirmed, Medium.  Most of the cited code never runs:
`vm_page_set_policy` has no caller, so the LRU scan and age scan are dead;
`vm_page_estimate_working_set` and `vm_page_activate` have no callers.  A
stale `next` in reclaim/scan_clock is gated by a flag check a freed page
fails.  Real list corruption needs two concurrent `vm_pageout` runs plus a
failed launder of exactly the cached next.

**SYS-VM-005** — confirmed, High.  The "memtrack leak" is accounting skew and
a wasted copy; the frame is freed.  That half is i386-only.  Lines:
`vm_fault()` starts at `vm_fault.c:194` (55-193 is helper code).

**SYS-VM-006** — confirmed, raised to Critical.  The double drop is a
use-after-free of a physical frame, reachable unprivileged: write a heap page,
fork C, shrink the parent's heap, C forks G, C shrinks its heap — the frame is
freed while G still maps it, and a write fault in G passes the COW
single-mapper test (`pmap.c:2442-2444`).  Needs the mmap/heap overlap
precondition.  Lines: `brk_unmap_free_pages` is `vm_syscalls.c:93-101`
(pair at :97-98).

**SYS-VM-007** — partly confirmed, Low.  Nothing can run between `trap.c:130`
and `:134`: #PF is an interrupt gate (IF=0), `pmap_enter` never sleeps, and
the APs never run threads.  Latent until the fault path is preemptible or APs
schedule.

**SYS-VM-008** — partly confirmed, Low.  No in-tree zone triggers it: the
smallest is 16 bytes (ipers 239 on i386).  The threshold is also off by one:
collision needs ipers ≥ 255, not ≥ 254.  A latent trap for a future small
zone.

**SYS-VM-009** — confirmed, Low.  `uma_zdestroy` has no in-kernel caller and
every zone passes NULL init/fini without `UMA_ZONE_REDZONE`, so neither
defect can trigger today.

**SYS-VM-010** — partly confirmed, Low.  The counter cannot underflow (the
`> 0` guard at :674 stops it).  No caller can pass a high page: all four pass
`uint32_t` and the high zone starts at 4 GiB.  The integrity check is used
only by host tests.  Lines: `vm_phys_check_integrity` is `phys_mem.c:680-734`.

### 8.2 Architecture

**SYS-ARCH-001** — partly confirmed, High.  NT does not make the ring-0
`iret` task-switch: entry through an interrupt gate clears NT, so `iret` runs
with NT=0 and only loads NT=1 into the user context, which user code can do
with POPF anyway.  The bad-selector panic and the SIGKILL-immune process
remain: an unprivileged panic, no escalation.  Lines: `isr_exit` starts at
`isr.S:256` (iret :270); RT eflags masks `signal.c:857-861`; panic site
`trap.c:535`.

**SYS-ARCH-002** — refuted.  `is_iret_at_ip` does not exist;
`trap.c:25-35` is `#include` lines.  The kernel has no opcode-matching iret
recovery on either architecture; kernel faults recover only through
`current_thread->on_fault` (`trap.c:248, 333-343`).  ptrace and x86_64
sigreturn validate their frames.  Whether another path can fault an `iretq`
is a separate open question.

**SYS-ARCH-003** — partly confirmed, Critical.  An all-zero descriptor raises
#GP(0x30), not #NP — still a ring-0 fault.  On i386 a ring-0 store to a
read-only page succeeds silently (see SYS-VM-001) rather than panicking; the
panic holds on x86_64.  Only truly unmapped addresses panic (resident-able and
stack-growth pages are demand-paged).  The slot-6 fault fires only until some
thread sets `gs_base`; afterwards a new thread silently inherits a stale TLS
descriptor.  Lines: i386 stores `isr.S:439, 441`; x86_64 `isr.S:295, 297,
450`; zeroing `gdt.c:127`; missing validation `syscall.c:1509-1513`
(`kern_thr_new`).

**SYS-ARCH-004** — confirmed, Critical.  `vm86_gpf_handler` is an if-chain:
the catch-all is `vm86.c:403-405` and the user-HLT branch `:398-400` behaves
the same.  The loop is a VM86 ↔ ring-0 #GP bounce.  Lines: `vm86_enter`'s
iret is `vm86_asm.S:113`.

**SYS-ARCH-005** — partly confirmed, Low.  No thread runs off the BSP (APs
halt at `smp_discovery.c:693-696`; x86_64 never starts one).  The
`fpu_forget_process` race is unreachable: `sched_reap_thread` already clears
every `fpu_owner` slot (`wait.c:270` runs before `proc_destroy` at :282).
Disabling interrupts would not fix migration anyway.

**SYS-ARCH-006** — partly confirmed, Low.  The wrong-size free does not
corrupt: `kfree` ignores the size, takes the large path, and bails on the
magic check, leaking 64 KiB.  The retry path needs a narrow fork/exec race.
The stale GDT descriptor has no effect.  Lines: `ldt_activate` is
`ldt.c:302-314`; the x86_64 skip is `ldt.c:127-133` and `gdt.c:250-262`.

**SYS-ARCH-007** — partly confirmed, Low.  `ext2_read_inode` runs twice per
boot (~2.3 KiB); the allocation that can collide with the stack is
`salloc(dir->i_size)` in `ext2_find_entry` (`stage2.c:512`).  The stage1/2
loader is legacy and not built.  Lines: `ext2_read_inode` is
`stage2.c:455-466`; the pointer is `heap_ptr`.

### 8.3 Kernel Core

**SYS-KERN-001** — confirmed, Critical.  The walk is the `allthread` list via
`sched_iterate_threads`, not a `threads[]` array.  Lines: the `id == -1` block
is `syscall.c:1665-1669`; unlocked walk `sched.c:888-892`.

**SYS-KERN-002** — partly confirmed, Medium.  `sysctl_new_kernel` does not
exist (the code is `copyin` at `sysctl.c:351`, same effect).  `sys_sysctl`
allows writes only with `CTLFLAG_WR` and euid 0 (:163-173), so only root can
trigger it, and the single NUL byte at `str-1` is not attacker-chosen.

**SYS-KERN-003** — partly confirmed, Medium.  `tty_revoke` already validates:
`tty_valid()` checks `TTY_MAGIC` (`tty.c:1671`, `:30-32`) before any write.
`revoke("/etc/passwd")` returns `-ENOTTY` (ext2 never sets `node->ptr`).  The
worst case is a 4-byte read.  Lines: `sys_revoke` is `syscall.c:4053-4062`.

**SYS-KERN-004** — confirmed, Critical.  The value written is always 1.  The
trigger needs the Linux personality (any user can exec an OSABI-3 ELF).
Linux writes 0 here, not 1.

**SYS-KERN-005** — confirmed, High.  The stale writes land on the same CPU
straight after the free, and UMA keeps its free-list state outside the item,
so allocator metadata is safe; corrupting a live object needs an IRQ or
preemption reallocation in a few-instruction window.  The `OWNER_DIED` path
repeats the bug (free :1327, writes :1328-1331).

**SYS-KERN-006** — confirmed, High.  Needs a set-ID program that drops with
`setreuid(getuid(), getuid())` and attacker code after the drop.

**SYS-KERN-007** — confirmed, High.  A bare pid check would break POSIX fork
inheritance: fork does not copy mq descriptors, so they must become
per-process (copied at fork) for the fix to be correct.

**SYS-KERN-008** — confirmed, High.  "Open record OR `ksem_perm_ok`" is the
right shape, but NetBSD anonymous semaphores (mode 0) shared across fork need
more than that.  Lines: lookups at `posix_sem.c:352, 410`.

**SYS-KERN-009** — confirmed, High.  The serious harm is the wrap case only.
A later teardown of the stretched entries could clear kernel PTEs — plausible,
not proven.  Lines: `kern_shmat` is `ipc_shm.c:238-330`; alignment check
:281-284; second unvalidated remove at :320.

**SYS-KERN-010** — partly confirmed, Medium.  Kernel threads never read
`THREAD_F_WAKE_PENDING`, so nothing is woken there.  The effect is spurious
wakeups that park/unpark callers must tolerate.

**SYS-KERN-011** — partly confirmed, Medium.  The tail is 0-3 bytes
(getdents) or 0-7 (getdents64), and is often left over from an earlier name
in the same call, not fresh stack.  A small read-only disclosure.

**SYS-KERN-012** — confirmed, High.  The working trigger is a present
read-only page (an unmapped address is rejected earlier by `futex_get_key`).
Impact is an unprivileged livelock, not corruption.

**SYS-KERN-013** — confirmed, Medium.  `kern.securelevel` is never raised and
is read only by `/dev/kmem`; `/dev/mem` uses its own `mem_secure_level`.
Lines: `sysctl_handle_int` starts at `sysctl.c:302`.

**SYS-KERN-014** — partly confirmed, Low.  `struct acct` has no
`ac_exitcode`; only padding is uninitialized.  Turning accounting on needs
euid 0.

**SYS-KERN-015** — confirmed, Medium.  Lines: `sys_proc_cwd` is
`syscall.c:4659`, `sys_proc_exe` 4673, `sys_proc_thr_list` 4565.
`sys_proc_thr_count` (4511) is also unchecked.

**SYS-KERN-016** — partly confirmed, Medium.  `kmalloc` under `pi_lock` is
`M_NOWAIT` and never sleeps or takes `pi_lock`; it only lengthens the hold.
The spinner is preemptible, so the cost is wasted timeslices rather than a
hard deadlock.  Lines: the `kmalloc` is at `futex.c:906` (in
`pi_state_alloc`); lock taken :1151, released :1186.

**SYS-KERN-017** — confirmed, Low.  The bad values only set the caller's own
deadline (early `ETIMEDOUT` or a long interruptible sleep).
`posix_sem.c:458` is an unsigned wrap; the signed overflow is at :450.

**SYS-KERN-018** — partly confirmed, Low.  The loop stops at the first
unmapped word and never passes the 3 GiB ceiling.  No lock is held and the
timer preempts it.  The cost falls on the caller.

**SYS-KERN-019** — confirmed, Medium.  Fails closed: it refuses legitimate
access (owners too, e.g. mode 0400), never grants extra.

**SYS-KERN-020** — confirmed, Medium.  Values are clamped to `[0, SEMVMX]`;
the effect is wrong semaphore values for that set, not memory corruption.

**SYS-KERN-021** — partly confirmed, Low.  Nothing reads `s_login`;
`sys_getlogin` returns `"root"` regardless, and native binaries cannot reach
the syscall.

**SYS-KERN-022** — partly confirmed, Low.  `-INT_MIN % 16 == 0`, so the index
stays in bounds and `sys_kill` already returns `-ESRCH`.  Negative pgids are
filtered at every other caller.  Formal UB only.

**SYS-KERN-023** — partly confirmed, Low.  For `setre*`/`setres*`, -1 means
"unchanged" and values below -1 are legal IDs (Linux accepts them).  Only
`(uid_t)-1` in `setuid`/`setgid`/`seteuid`/`setegid` should be rejected, and
only root can reach it.  Lines: `setreuid_ids` 2081, `setregid_ids` 2105,
`setresuid_ids` 2134, `setresgid_ids` 2149-2161.

**SYS-KERN-024** — partly confirmed, Low.  There is no KASLR, so the PCs are
the same every boot.  The same PCs are world-readable in `/proc/memtrack`
(`procfs.c:161-177`), which a fix must also cover.

**SYS-KERN-025** — confirmed, Low.  Three callers, all once at boot.

**SYS-KERN-026** — confirmed, Low.

**SYS-KERN-027** — refuted.  `mutex_lock` checks `m->owner == me` at entry
(:58) and panics (:71) before the fast path, so a recursive lock never reaches
the slow path; the branch at :110 is dead.  There is no `m->turnstile`.

### 8.4 VFS

**SYS-VFS-001** — partly confirmed, Critical.  Read-only mounts are checked
(`truncate_fs`, `syscall.c:534-536`, and `ext2_truncate`).  Directories are
never truncated; the only fault there is `-EINVAL` instead of `-EISDIR`.  The
missing write-permission check stands.

**SYS-VFS-002** — confirmed, High.  `EROFS` and the in-memory setuid/setgid
clear already hold.  `sys_chown` (`syscall.c:3519`) is a fifth entry point.

**SYS-VFS-003** — partly confirmed, High.  The `namei` half is not reachable:
its only caller `vfs_mount()` has no callers.  Lines: the slash-skip loop is
`vfs_lookup.c:132-133`; the concatenation `:279-280`; `vfs_perso_shadow`'s
root lookup `vfs.c:1012-1013`.

**SYS-VFS-004** — partly confirmed, Medium.  Not live: `vop_access`'s only
caller `vop_open` has no callers, and the vnode layer has no consumers.  The
parameter is `cred`, not `ap->a_cred`.

**SYS-VFS-005** — partly confirmed, Low.  The call is at
`vnode_fsnode.c:154`; the quoted comment does not exist;
`finddir_lstat_fs` does not exist (use `finddir_fs_internal(dir, name, 0, 0)`
via a new wrapper).  `namei` is unreachable today.

**SYS-VFS-006** — confirmed, High.  Severity rests on the NULL dereference;
most filesystems already reject negative offsets.

**SYS-VFS-007** — confirmed, raised to High.  On the `!wf` path the pipe and
its buffer also leak, permanently.  `sys_pipe` (`syscall.c:3346`) leaks both
fds on copyout failure too.  `kern_pipe` never checks the NULL nodes
`pipe_create` returns on OOM — a separate defect.

**SYS-VFS-008** — refuted.  `sleepq_add` + `sched_yield` is a real blocking
sleep (state set `BLOCKED` under `bio_lock`; `sched_yield` never picks it;
wakers dequeue under the lock).  Switching to `sched_sleep` would add a 250 ms
fallback that leaves the thread on the sleepq — `getblk`, which does that
today, is the one site with a latent double-link hazard.  The unlock is
`spinlock_release`, not `mutex_unlock`.

**SYS-VFS-009** — partly confirmed, Medium.  The `vfs_mount.c` and `vflush`
sites are dead code (no callers).  Live unlocked walks of the mount list also
exist at `freebsd_user.c:882`, `netbsd_user.c:932`, `perso_xenix.c:2899` and
`perso_elks.c:114` (reported, not independently checked).

### 8.5 Filesystems

**SYS-FS-001** — confirmed, Critical.  Lines: grow calls are
`shmfs.c:208, 342, 452-453`; `phys_base` captured at :356.

**SYS-FS-002** — partly confirmed, High.  Item 3 (`udf_vnodeops`) and
`udf_remove_fid` are unreachable.  Item 4 yields wrong data, not memory
corruption; `udf_add_fid` bounds the write.  Mounting needs root.  Plausible
but untraced: with two UDF mounts a recycled slot could make one umount free
the other's `udf_fs`.

**SYS-FS-003** — partly confirmed, High.  The `minix_link` cast is overwritten
by `minix_write_inode` before anything reaches disk.  The 32-bit wrap is real
for V2 only.  `minix_free_all_zones` can only mark wrong zones free.  Mounting
needs root.

**SYS-FS-004** — refuted.  `kern_getxattr` (`xattr_permission`,
`syscall.c:2379-2381`) and `kern_listxattr` (:2450-2471) already block
`trusted.*` for non-root.  Remaining gap: the size-only listxattr probe
(:2441-2445) returns the unfiltered list length.

**SYS-FS-005** — partly confirmed, Medium.  Growth writes the 64-bit size
correctly (`ext2.c:5494`); only shrinks across a 4 GiB band corrupt
`i_dir_acl`.

**SYS-FS-006** — confirmed, High.  The write-side cast and the missing
`cluster < 2` check are the real defects; the read-side escalations need
near-4 GiB files or crafted images.  An uncited second `dir_buf` leak sits at
`fat.c:~569-570`.

**SYS-FS-007** — partly confirmed, High.  The negative-offset claim is not a
memory-safety defect: comparisons are in `size_t` and no out-of-bounds read
happens.  The task-tid singleton use-after-free carries the severity.

**SYS-FS-008** — partly confirmed, Medium.  The `g_dirent` race does not
exist (`readdir_fs` holds `readdir_lock`).  A crafted image only reads other
bytes of the same partition; the driver is read-only and mounting needs root.

**SYS-FS-009** — confirmed, Medium.  Also an unlocked `memset(node)` at
`fat.c:690`.  Lines: the match is `fat.c:647-654`; memsets 680-681.

**SYS-FS-010** — partly confirmed, Low.  The `ext2_read_blocks` half cannot
wrap (single caller caps the count).  The immutable half reaches only
timestamps, and `kern_utimensat` has a broader missing owner check.

**SYS-FS-011** — confirmed, Low.  Every reply is bounded; the tag race is
serialized by `v9p_lock`; the fid race is unreachable.

**SYS-FS-012** — confirmed, Medium.  Only the unlocked alias writer can be
preempted; the boot-time calls are single-threaded.

**SYS-FS-013** — confirmed, Low.  The -1 never escapes; callers map failure
to `-EIO`.

**SYS-FS-014** — partly confirmed, Low.  The VFS walker consumes `.` and `..`
before `shmfs_root_finddir`/root `sysfs_finddir` see them.  The real gap is
`sysfs_leaf_finddir` at `sysfs.c:93-96`.

### 8.6 Networking, Executable Loaders & Personalities

**SYS-EXEC-001** — confirmed, Critical.  Per-entry lines are off by 1-11:
the table opens at `perso_linux.c:1006`, first `kern_*` entry :1013
(waitpid 1013, link 1015, unlink 1016, chdir 1018, time 1019, stime 1030,
access 1036, rename 1040, mkdir 1041, rmdir 1042, times 1045, acct 1053,
sigaction 1065, sigsuspend 1068, sigpending 1069, sethostname 1070,
gettimeofday 1074, symlink 1079, readlink 1082, setitimer 1100,
getitimer 1101, wait4 1108, sigprocmask 1120, getdents 1130,
rt_sigpending 1167, rt_sigtimedwait 1168, rt_sigsuspend 1170).

**SYS-EXEC-002** — confirmed, High.  `coff_load_shlib` does not exist (an
inline loop at `coff.c:441-467`).  xout/x286 tolerate short reads by design.
Once the old map is destroyed, the right handling is to kill the process.
The impact is the caller's own process plus a per-exec leak, not corruption.

**SYS-EXEC-003** — partly confirmed, High.  The xout claim is wrong (the
whole 1 MiB stack region is registered and demand-filled).  The ELF claim is
unreachable (`ARG_MAX_BYTES` and the 8192-entry cap keep the image under the
128 KiB eager stack).  The a.out overflow stays within the new process's own
user space: a local panic, not corruption.

**SYS-EXEC-004** — partly confirmed, High.  The missing `frame_replaced` in
`linux_sig.c`/`openbsd_sig.c` is harmless (the fast path writes back the
restored `eax` and skips the EDX/CF write-backs).  Native and Linux signal
numbers agree except 16, 23, 29, 30.  RPL forcing and EFLAGS masks rule out
ring 0; what remains is an unprivileged panic and an unkillable process.
Native `sys_sigreturn`/`sys_rt_sigreturn` have the same SIGKILL/SIGSTOP gap.

**SYS-EXEC-005** — partly confirmed, High.  read/write go through
`copyin`/`copyout` and cannot leave the process.  The getpid-family and
GETUSAGE stores overrun by at most a byte into the process's own page.  The
ioctl `memcpy` sites (`perso_elks.c:2381, 2397`, default :2401) are the
dangerous ones.  Checks: read 1116, write 1168, waitpid 1508, getpid 1537,
getuid 1559, getgid 1581, ioctl 2301.

**SYS-EXEC-006** — partly confirmed, Low.  `sys_munmap` bounds the range to
user space, so the worst outcome is a process unmapping its own memory.  The
shift is UB from 32, not 31; the fix must check `len + align` overflow, not
just cap the shift.

**SYS-EXEC-007** — refuted.  The quoted checks and variables do not exist.
COFF rejects out-of-range sections (`coff.c:244-246`); a.out caps and
wrap-checks sizes (`aout.c:105-117`) and every region goes through
`vm_map_insert` on a map bounded to `[0, 0xC0000000)`.

**SYS-EXEC-008** — refuted.  The function is `aout_dup_vector`
(`aout.c:193`), which handles kernel-resident vectors through
`exec_vec_ptr` and per-string `aout_is_user_ptr`, as `elf.c` and `xout.c` do.

**SYS-EXEC-009** — partly confirmed, Low.  The quoted expression does not
exist; the real wrap is at `elf.c:783-784`.  Filesystems reject the negative
offset and the pager zero-fills: only the exec'd image is wrong.  9p, fuse
and udf were not checked.

**SYS-EXEC-010** — partly confirmed, Medium.  Linux, FreeBSD and SunOS map
execve to native `sys_execve` and do not leak; drop those citations.  Valid:
`perso_sysv386.c:867-888`, `perso_xenix.c:1467-1500`; loader jumps that never
drain: `aout.c:608`, `coff.c:525`, `xout.c:563, 1301`, and `elf.c:2098`.
Related: `perso_elks.c:1753-1775`.

**SYS-EXEC-011** — partly confirmed, Medium.  `sys_utime` is unreferenced.
`kern_lchown` and `perso_ipc_shmat_PageAddr` do not exist (the code is
`linux_sys_ipc_shm`, `LINUX_SHMAT`).  FreeBSD pread/pwrite is
`freebsd_user.c:682-702`; NetBSD has the same pattern at
`netbsd_user.c:~1612-1660`.

**SYS-NET-001** — confirmed, Medium.  The rollback race leaks, it does not
use-after-free; descriptors left queued are released when the receiving
socket is freed.  Lines: `fd_fail:` at `af_unix.c:2386`, decrement :2390.

**SYS-NET-002** — confirmed, Medium.  AF_PACKET needs euid 0 and the window
is a preemption window.  Lines: the `kfree`s are `netdev.c:228-229` (the file
has 288 lines).

**SYS-NET-003** — confirmed, Medium.  `len > sizeof(reply)` is checked
(`icmp.c:449`); 1.5 KiB on a 16 KiB stack is pressure, not overflow.  The
checksum bug loses multicast-ping replies only.  Lines: buffer `icmp.c:448`,
pseudo-checksum :459-460; source substitution `inet6.c:265`.

**SYS-NET-004** — partly confirmed, Low.  No AF_INET6 socket can exist
(`afinet_socket` rejects it), so neither defect is observable today; header
reads are bounds-checked.  The arguments should be `nh, l4, l4_len`.

**SYS-NET-005** — partly confirmed, Low.  NICs already call `netdev_rx` with
IF=0, so the `intr_disable()` changes nothing.  The netdev list
(`g_netdev_head`) only grows, so lockless reads are safe.  The alias race
needs root on one CPU.

### 8.7 Drivers & Coding Rules

**SYS-DRV-001** — partly confirmed, Low.  `sys_mmap` validates `MAP_FIXED`
ranges (`vm_syscalls.c:80-91, 386-387`) before any device handler.  Every
path needs root, who already has these devices.  Lines: `mem_mmap`'s
`MAP_FIXED` handling is `mem.c:211-216`.

**SYS-DRV-002** — refuted.  `vfs_prepare_open_node` (`syscall.c:410-429`)
clones a private `fs_node_t` for every char device with an `->open`, and the
last close frees only that clone (`:76-85`).  The per-open node the finding
asks for already exists.

**SYS-DRV-003** — partly confirmed, raised to Critical.  Arbitrary kernel
read and write through `tty_ioctl`'s `kernel_arg` bypass by an unprivileged
user.  `elks_sys_ioctl` is not affected (its arg is LDT-translated).  The
dereference is struct assignment in `tty_ioctl_kern` (`tty.c:1213-1235`).
Lines: `kernel_arg` `tty.c:1369`, bypass 1379-1409; BSD forwarding
`compat.c:1994-1997`.

**SYS-DRV-004** — partly confirmed, Medium.  Native, Linux and FreeBSD reads
use a kernel bounce buffer.  Only the SysV386, SVR4 and Xenix `kern_read`
paths pass a raw user pointer — a general flaw of those personalities.  The
node is root-only in practice.

**SYS-DRV-005** — partly confirmed, Medium.  The mechanism is wrong: no
`inst->lock` is taken and there is no `sched_sleep(&q.triggered)`.  Wakeups
are rechecked each pass and bounded by the 250 ms fallback.  What remains:
untimed waits ignore signals (an unkillable thread).

**SYS-DRV-006** — partly confirmed, High.  The BGA half is never compiled
(nothing defines `ENABLE_BGA`).  The exec use-after-free carries the
severity.  Lines: `fb_set_offscreen` is `fb.c:768-839` (unlock :824,
`pmap_remove_range` :838).

**SYS-DRV-007** — refuted as written, Low.  Most named identifiers do not
exist; `:77` is itself a bounds check; the TOC is per-entry guarded (:51);
the glyph offsets are never read; `bdf_free` frees `font->glyphs`.  A small
real issue remains: BDF parsing relies on NUL termination (:225-294).

**SYS-DRV-008** — confirmed, Medium.  The functions are
`blkdev_register_disk` → `blkdev_scan_partitions`; the leak is small and once
per boot.

**SYS-DRV-009** — partly confirmed, Medium.  The AHCI condition is
`req->data_len == 0 || req->data_len > 65534u`.  IDE returns -1 at
`ide_atapi.c:61-62` before PACKET; no DRQ wait.  Commands fail cleanly, and
probe tolerates the failure.

**SYS-DRV-010** — partly confirmed, Low.  Nothing calls `nvme_init`.  Harden
before wiring the driver up.

**SYS-DRV-011** — partly confirmed, Medium.  The `audio_node_close` claim is
false (`open_refs` is decremented correctly).  Add `sb16.c:224` (same
underflow) and `:427` (ignored `request_irq`); the ac97 failure block is
`ac97.c:970-977` and also leaves the IRQ registered.

**SYS-DRV-012** — partly confirmed, Low.  There is no `virtio_scsi_isr` (the
driver polls).  Kernel stacks are physically contiguous and DMA is
busy-waited.  The GPU command API has no callers.

**SYS-DRV-013** — partly confirmed, Low.  A returning kthread already exits
through `thread_exit_wrapper`.  The uas buffers are ~96 bytes of `kmalloc`,
not DMA.  Every path needs a hotplug into a full table.

**SYS-RULE-001** — partly confirmed, Low.  The rule is AGENTS.md Directive 12
"No Manual Externs".  Ten cited files have no extern (`elf.c`,
`perso_freebsd.c`, `svr4_streams.c`, `console.c`, `cpuid.c`, `pseudo.c`,
`ide.c`, `fb.c`, `fb_console.c`, `vga.c`); `vga.c` has 601 lines.  Corrected:
`nvme.c:60-62, 632`, `ehci.c:91-92`, `uhci.c:128-130`, `bga.c:121`.  20 of
the 27 are host-test-only; of the rest, 6 name linker or assembly symbols,
leaving `efi_runtime.c:65`.

**SYS-RULE-002** — partly confirmed, Low.  `aout.c:23` and `coff.c:1` are
compliant, and the directory-wide claims are false.  The correct citations
are `subr_copy.c:14`, `boot.S:2`, `multiboot_hdr.S:2`, `ext2_hash.c:21` and
`ext2_xattr.c:39` (a `<...>` include with the wrong path), plus
`sys/host_test_mutex.c:38`, a `../` include in a host test.

**SYS-RULE-003** — confirmed, Low.  `pipe.c` also defines `O_ACCMODE`.
`<sys/mman.h>` resolves to the userland header; there is no
`sys/include/sys/mman.h`.

## 9. Found while fixing the above

Defects met while fixing this audit's findings (October 2026), outside any
finding above.  Same form as sections 1–7.  Each was then checked by an
independent read-only verifier and a completeness critic (2026-10-19); the
verdicts, corrections and splits are recorded with each finding, and the
critic's suggested fix order is 007a, 003, 001a, 004, 006a, 002, 001b, 005,
006b, 007b.

- [x] **SYS-NEW-001a** *(High; confirmed)* No ext2 vnode op shall dereference `ctx->fs` of a node whose inode has been deleted.
  Evidence: `ext2_node_close`'s deferred delete (`sys/fs/ext2/ext2.c:115-137`, reached from rmdir at :6780-6784 and unlink at :6616-6624) sets `ctx->fs = NULL` but leaves `node->finddir` set; a path walk sitting in a directory that is rmdir'ed calls `ext2_finddir`, which dereferences `fs->mp` at :3539-3542 — a kernel NULL dereference in the root filesystem.  Other ext2 ops reading `ctx->fs` unchecked need the same guard.  Interim mitigation for SYS-NEW-001b.
  Basis: traced (verifier + critic).

- [x] **SYS-NEW-001b** *(High; confirmed)* While a path walk holds an `fs_node_t` it got from `finddir`, no filesystem shall free, recycle or repopulate that node.
  Evidence: the live walk (`vfs_lookup`/`vfs_lookup_lstat`/`finddir_fs_internal`, `sys/vfs/vfs.c:1118, 1277, 851`) carries bare node pointers between `finddir` calls; the only pin is `open_fs`, taken after the walk (`vfs_lookup_ref` :1254, `sys_open` `syscall.c:904`) or never (readlink :3245, truncate :1225, statfs, the parent lookups of unlink/mkdir/link).  Lookups sleep on I/O and the kernel is preemptible.  Exposed: ext2 (`ext2_finddir` returns children unpinned, `ext2.c:2861-2866`; `ext2_alloc_node` recycles any `pin_count==0` slot, :2286, silently retargeting a walk); FAT (`fat.c:663-685`, which also memsets a slot on a cache hit), exFAT (`exfat.c:716-737`, likewise), UDF (`udf.c:556-570`); Minix (`minix_ncache_forget`, `minix.c:695-703`, unlink during a walk — eviction itself was removed in 7d81065bb); procfs (`procfs_release_pid_nodes` frees the block when unopened, `procfs.c:1059-1087`); shmfs (`shmfs.c:285-289` via unlink :638, and finddir drops the lock before returning, :557-563); devfs alias nodes (`devfs.c:556` via `device.c:329`); pty slave nodes (`drivers/console/pty.c:316-321` via `pty_destroy` :507).  Not exposed: sysv (leaks unopened nodes instead), sysfs, 9p, fuse.  `open_fs` is also the wrong primitive for a reference: `stat()` through `vfs_lookup_ref` runs a pty slave's real open hook (`pty.c:522`).
  Fix: side-effect-free `hold`/`rele` ops on `fs_node_t` (NULL for static nodes) mapped to each driver's pin; `finddir` returns its child held; the walk holds `root` and releases each step's predecessor (also for `..`, mount crossing, symlinks, perso shadowing); every `vfs_lookup*`/`finddir_fs` caller (about 57 sites) releases; `sys_open` turns the walk's hold into the open pin; `vfs_lookup_ref` uses hold/rele instead of open/close.
  Basis: traced (verifier + critic).
  Progress: the VFS side and ext2 are done — hold/rele ops, one held walk (`vfs_walk`) behind `vfs_lookup`/`vfs_lookup_lstat`, every caller converted (the `_ref` variants are gone, so `stat()` no longer runs a pty's open hook), and a per-thread hold count checked at syscall return (`vfs_holds_check`).  `tests/kern/walkrecycle.c` (walks while other lookups cycle ext2's node cache) failed on the pre-remediation kernel (2 wrong inodes, 19 failed lookups) and passes now.  Converting the walk also found chroot taking no pin on its new root (which exit then dropped), fchroot never releasing the old one, `chdir(".")` leaking a cwd pin, and acct leaking the previous file's.  FAT done too: `fat_alloc_node` pins under its lock, never rebuilds a pinned slot (a held slot whose first cluster differs is a since-deleted file and the new one gets its own slot), and builds a slot under the lock; walkrecycle on a FAT32 mount failed on the old kernel (2 wrong, 10 failed) and passes.  exFAT likewise (and an orphaned slot is never taken for a new file in the same entry); walkrecycle on exFAT failed ~130 000 lookups per walker on the old kernel and passes.  UDF: a lookup's slot is claimed pinned under the lock that chose it (it was chosen, released, then filled); compile-verified only — no UDF image here that either kernel mounts (`mkisofs -udf` output fails "AVDP not found" on old and new alike; no mkudffs).  Minix: its lookup cache returns nodes held (`opens`, taken under the cache lock), so `minix_ncache_forget` frees a node a walk stands in at the walk's release; smoke-tested (no `mkdir`, so no walkrecycle).  procfs: `procfs_get_pid_nodes` returns its block held, every finddir returns its node held (the per-fd and per-thread entries pinned inside the lock that sets them up), so a reaped process's block waits for the walk's release; procnodes passes.  shmfs: finddir takes the inode's refcount under `shmfs_lock` (hold/rele are the open/close count), so an unlink frees it at the walk's release.  devfs and ptys, last: an alias node devfs owns counts its references on its entry, and one removed (or replaced) while held is freed at its last release; a Unix98 slave node lives in a refcounted box and, held when its pair is destroyed, stays as an inert orphan until released; `devfs_dir_finddir` takes every reference under `devfs_lock`, so an unregister cannot free a node between its being found and held.  `tests/kern/ptsrace.c` (stat /dev/pts/N while ptys churn) passes, though the old kernel survives it too.  sysv, sysfs, 9p and fuse were not exposed and have no hold ops.

- [x] **SYS-NEW-002** *(Medium; confirmed)* `link(2)` shall link a symlink in the old path itself, not its target; a BSD `linkat` shall follow only with `AT_SYMLINK_FOLLOW`.
  Evidence: every link entry point ends in `kern_link`, which resolves the old path with `vfs_lookup` (`sys/kern/syscall.c:3088`) and so follows its final symlink: `ln -P dangling new` fails `ENOENT`, `ln -P good new` links the target.  Not a userland cause (`ln -P` lstat()s and calls `link()`), and not ext2/Minix-specific.  There is no native `linkat`/`AT_SYMLINK_FOLLOW`; FreeBSD and NetBSD `linkat` discard the flag (`freebsd_user.c:373`, `netbsd_user.c:775`, whose comments call following intended); Linux `linkat` (303) is named but not dispatched (ENOSYS).
  Fix: give `kern_link` a follow flag choosing `vfs_lookup_lstat` for the old path; pass `AT_SYMLINK_FOLLOW` through from the BSD `linkat`s; dispatch Linux `linkat`.  Check first that every filesystem's link op still rejects a directory source.
  Done: `kern_linkat` (dirfds + follow flag); FreeBSD/OpenBSD/SVR4/SunOS `link` keep following, as those systems do.  The check found more: Minix's link op accepted a directory, and neither ext2's nor Minix's checked that the source was on the same mount, so `link("/dev/null", "/tmp/x")` handed a devfs node to ext2 as its own; `link_fs` now refuses both (`EPERM`, `EXDEV`).  Test: `tests/kern/linkfollow.c` (Linux build, and a FreeBSD build under `/perso/freebsd`).
  Basis: traced (verifier + critic).

- [x] **SYS-NEW-003** *(High; confirmed — raised from Medium)* The pv (reverse-map) code shall accept every valid kernel pointer on x86_64.
  Evidence: `pv_entry_ptr_sane` (`sys/vm/vm_page.c:360-380`) rejects any pointer `>= 0xFF000000U`, which on x86_64 is every kernel pointer.  So `pv_insert` drops a page's existing list and `pv_remove` drops it without freeing (:460, 479, 486, 503, 510): every pv entry leaks (the static pool, then kmem-32), and x86_64 has effectively no reverse map — `pmap_page_remove_all` and the `pmap_protect` multi-mapper COW guard (x86_64 `pmap.c:739, 897`) see at most one mapping.  This is the ~40 KB per fork+exit seen from the shell (a slab leak, not a cache; MemFree counts only buddy-free pages), and any frame-reclaim path relying on the pv list rather than `ref_count` is a use-after-free candidate on x86_64.  The silent "insane" recovery is what hid it.  i386's ~1 MB drift over 200 cycles is unexplained (likely cache or noise).
  Fix: a per-arch upper bound (or none on x86_64), rate-limited logging in the "insane" branches, and a boot-time self-check.
  Basis: traced (verifier + critic).

- [x] **SYS-NEW-004** *(Medium; confirmed)* A Linux thread's `exit` (`SYS_exit`, not `exit_group`) shall end only that thread, and its `CLONE_CHILD_CLEARTID` word shall be cleared to 0.
  Evidence: Linux `exit` (1) and `exit_group` (252) both map to `sys_exit` → `proc_exit`, which kills every thread (`sys/exec/perso/perso_linux.c`); glibc threads end with `SYS_exit`, so the first thread exit ends the process.  Companion bug: `futex_wake_exited_thread` writes 1, not 0, to the clear-tid word (`sys/kern/futex.c:358`, since ef53a85f2; the comment at :372 says "zeroes"), which would hang glibc `pthread_join` once threads stop killing the process.  No Linux x86_64 personality exists; the i386 table is built into both kernels.  Also: `sys_thr_exit` (`syscall.c:1580`) has no last-thread check, so a process whose last thread leaves through `thr_exit`/`_lwp_exit` never reaches `proc_exit` — the only caller of the mq/ksem/shm exit cleanup.
  Fix: `linux_sys_exit`: the last live thread calls `proc_exit`; otherwise run the robust-list cleanup, mark the thread detached and exit it alone; write 0 to the clear-tid word.  The same last-thread rule in `sys_thr_exit`.
  Basis: traced (verifier + critic).

- [x] **SYS-NEW-005** *(Medium → Low; confirmed, i386 only)* A statically linked i386 program shall get thread-local storage, for its main thread and its threads.
  Evidence: `lib/c/src/tls_static.c` (static TLS from PT_TLS) is under `#if defined(__x86_64__)` ("every i386 program is dynamically linked" — not so); i386 `crt0.S` has no TLS step, so a static i386 program's main thread has no TLS image and threads get `tls_base == NULL` (gs base 0).  libpthread's own TLS (cancel state, cleanup stack, keys) then faults — a thread at the latest on exit, the main thread on any such call — and the program's `__thread` variables are never initialised.  amd64 static programs are fine.  Narrower than first thought: `-lpthread` now resolves to the shared library (`install-specs.sh`), so only explicit `-static` links are hit.
  Fix: port `tls_static.c` to i386 (Elf32 phdrs from auxv, variant II as ld.so builds, `SYS_SET_GSBASE`) and call it from i386 `crt0.S`; add a static i386 TLS test.
  Basis: traced (verifier + critic).

- [x] **SYS-NEW-006a** *(Medium; confirmed)* `exec` shall detach the process's SysV shared-memory segments.
  Evidence: `shm_proc_cleanup` (`sys/kern/ipc_shm.c:479`) runs only at exit (`process.c:1978`); attach records and `nattch` survive exec, and a later `shmdt` in the new image (`kern_shmdt` :373 → `shm_detach_one` :344) can `vm_map_remove` an unrelated range of it.
  Basis: traced (verifier + critic).

- [x] **SYS-NEW-006b** *(Low; confirmed)* `exec` shall close POSIX message-queue descriptors (dropping `mq_notify` registrations) and named semaphores.
  Evidence: mq handles live in a global table keyed by pid (`posix_mqueue.c:108-118`) and are released only by `mq_close` and `mq_proc_cleanup` at exit (`process.c:1988`); no loader calls into it.  All descriptors survive exec, not only inherited ones, and an old `mq_notify` registration still signals the new image, whose handlers exec reset to default (e.g. SIGUSR1 kills it).  `ksem_proc_cleanup` (`posix_sem.c:577`) is likewise exit/fork-rollback only.  POSIX timers are cleared correctly.
  Fix: one post-commit exec hook beside `proc_close_cloexec` in every loader: `mq_proc_cleanup`, `ksem_proc_cleanup`, and an shm detach that drops the records without `vm_map_remove` (the old map is gone).  Stub it in `tests/Makefile` in the same commit.
  Basis: traced (verifier + critic).

- [x] **SYS-NEW-007a** *(Medium; confirmed — regression)* `make -C tests` (CI's host-testsuite job) shall build.
  Evidence: 7 targets (`test_runner`, `host_test_proc_tree`, `host_test_proc_exit`, `host_test_proc_lifecycle`, `host_test_proc_signal_state`, `host_test_fork_exec_wait`, `host_test_linux_signal`) fail to link on `mq_proc_fork`, `ksem_proc_fork`, `casuword32` and `sys_thr_self`, introduced by this remediation (3cd121605, c069a41ee, the KERN-012 futex change, a2ce8f109).  CI has failed on every run since 3cd121605.  Also `sys/exec/perso/personality.h` uses `size_t` with only `<stdint.h>`.
  Basis: observed (verifier + critic; `gh run list`).

- [x] **SYS-NEW-007b** *(Low; confirmed)* The tests under `tests/sys` shall build or be removed.
  Evidence: 53 of the 136 `HOST_TESTS` fail to build, most for months (e.g. `RTSIG_QUEUE_MAX`, `fpu_switch`, `poll_notify`, `xout_init_handler` drift).  `host_test_perso_elks` does exercise the syscall table but no longer compiles (stale `stub_sys_stat`/`readdir_fs` prototypes).  The three FAT tests are in no list and need source updates as well as rules.  28 of 31 `tests/sys/torture_*.c`, `torture_fat_persist.c` among them, are built by nothing.  `tests/kern` and `tests/elks` build (except `cat_elks`, which needs ia16-elf-gcc).
  Basis: observed (verifier + critic).

- [x] **SYS-NEW-008** *(Medium)* FreeBSD `libthr` mutex contention shall not lose a wake-up.
  Evidence: `torture-cross/freebsd32/torture_pthread` (built on FreeBSD 14 i386, run under `/perso/freebsd`) stalls in its first test, 32 threads contending on one mutex: most of them asleep on the mutex word (one user address in every thread's WaitChan) while the holders have exited.  On the kernel from before the October 2026 remediation it hangs in 2 runs of 3 and passes in the third; a build at `b61a15847` once crashed the test with SIGSEGV instead.  So it is an old, nondeterministic race in the FreeBSD umtx emulation (`sys/exec/perso/compat.c`, `kern_umtx_*`), not a regression.  A simple FreeBSD threads test (join, `pthread_exit`, exit status, last-thread exit; `tests/kern/linuxthrexit.c` built on FreeBSD) passes.
  Basis: observed.
  Cause and fix: libthr's unlock stores `UMUTEX_UNOWNED`, clearing `UMUTEX_CONTESTED`, then calls `UMTX_OP_MUTEX_WAKE2`, which only woke one waiter.  FreeBSD's `do_wake2_umutex` sets the contested bit again while waiters remain; without it the waiter that took the lock next held an "uncontested" word, its unlock woke nobody, and the rest slept for ever.  `umtx_mutex_wake2` now does the same.  torture_pthread passed 3 runs of 3 on i386 and 1 of 1 on x86_64.

- [x] **SYS-NEW-009** *(Medium; confirmed — raised from Low)* A System V segment attached before `fork()` shall stay shared with the parent.
  Evidence: `tests/lib/ipc/torture_shm` reports test 20, `shared_pingpong`, as a hang (killed after 8 s) on i386 and x86_64, and identically on the kernel from before the October 2026 remediation.  Not a timing problem: the child, attaching the segment itself, never saw the parent's writes.  `shmat` maps with `VM_INHERIT_NONE`; `vm_map_fork` tears the cloned PTEs out of the child, but `pmap_fork` had also cleared `PTE_W` on the parent's side to arm COW, so the parent's next write faulted and split the device page into a private copy.
  Fix: for an inherit-none entry backed by a device or physical object, restore the parent's write access as the inherit-share path does (`vm_map.c`).  torture_shm: 37 of 37 on i386 and x86_64.
  Basis: observed.

- [x] **SYS-NEW-010** *(Medium; confirmed)* The sysv driver shall recognise the on-disk Xenix and System V superblocks it claims to.
  Evidence: `SYSV_MAGIC_XENIX` (`sys/fs/sysv/sysv.h`) is 0x012FF7B5, which is Linux's statfs `f_type` for System V 4, not a value ever written to a disk; a real Xenix filesystem has magic 0x2B5544 at offset 0x3F8 of a superblock at byte 1024, which the probe (1 KiB read at byte 512, magic at +0x1F8) never looks at.  So no real Xenix volume mounts.  Of the disks with magic 0xFD187E20, the driver reads `s_fsize` at offset 2 -- right for the System V Release 2 layout, wrong for Release 4, whose `s_fsize` follows a 2-byte pad at offset 4.  None of the images in the tree carries such a filesystem (their personality trees are ext2), so this was found by reading, and the driver had been exercised only by synthetic images.  Found while fixing SYS-FS-008.
  Basis: traced (against Linux `fs/sysv`).

- [x] **SYS-NEW-011** *(Low; confirmed)* Minix shall support `mkdir(2)`.
  Evidence: `minix_init_node` (`sys/fs/minix/minix.c`) gives a directory node symlink, link, unlink, rmdir and mknod but never `mkdir`, so `vfs_mkdir_in` and `kern_mkdirat` answer EOPNOTSUPP on every Minix volume (V1 and V2), though `minix_mknod` can already make a directory from an `S_IFDIR` mode.  Found when the `tests/sys` torture tests were wired up: `torture_minix` fails there, its other 13 checks passing.
  Basis: observed.

- [x] **SYS-NEW-012** *(Low; observed)* The AC97 driver shall honour `O_NONBLOCK`.
  Evidence: on AC97, `torture_audio_nonblock` never sees EAGAIN -- writes on a non-blocking descriptor go on accepting (blocking) for the whole fill window and the run stalls -- and `torture_audio_share` gets EBUSY from a write.  Only `hda.c` checks the flag (`hda_nonblock`, added by f2ac3e2d5).  Not root-caused.
  Basis: observed.
