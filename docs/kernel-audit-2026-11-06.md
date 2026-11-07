# Substrate Kernel (`sys/`) Audit & Actionable Task List (INCOSE / EARS Format)

**Date:** 2026-11-06  
**Scope:** `sys/arch/`, `sys/boot/`, `sys/vm/`, `sys/pm/`, `sys/kern/`, `sys/vfs/`, `sys/fs/`, `sys/exec/`, `sys/net/`, `sys/drivers/`  
**Format:** INCOSE / EARS (Easy Approach to Requirements Syntax)

---

## EARS Pattern Key

- **Ubiquitous:** The `<system>` **shall** `<response>`.
- **Event-Driven:** **When** `<trigger>`, the `<system>` **shall** `<response>`.
- **State-Driven:** **While** `<state>`, the `<system>` **shall** `<response>`.
- **Unwanted Behavior:** **If** `<condition>`, **then** the `<system>` **shall** `<response>`.

---

## 1. Architecture & Low-Level (`sys/arch/` & `sys/boot/`)

- [x] **[REQ-SYS-ARCH-001]** *(Critical — Unprivileged Ring-3 Interrupt Disable & Broken Kernel Helper in `sys/arch/i386/vm86.c`)*
  - **Resolution (2026-11-06):** the guest's IF is VIF, the CPU's stays set; `vm86_bios_call()` enters by `kern_vm86()`.  Running the BIOS then showed what the dead path hid, fixed with it: `vm86_enter` now points the TSS's `esp0` at its own frame (and the return puts it back), the call runs with preemption off, kernel BIOS calls do real word and dword port I/O, and any fault in one ends the call -- as any VM86 trap but #GP ends a user guest by a signal, where it panicked the kernel.  With `vga=ask` the BIOS video menu now draws; reading a key through `int 16h` still gets none (the PS/2 driver owns the keyboard).
  - **Location:** `sys/arch/i386/vm86.c:159-203` (`sys_vm86`), `sys/arch/i386/vm86.c:205-469` (`vm86_gpf_handler`), `sys/arch/i386/vm86.c:471-520` (`vm86_bios_call`)
  - **Finding:**
    1. In `vm86_gpf_handler()`, the `CLI` (`0xFA`), `STI` (`0xFB`), `PUSHF`/`POPF`, `INT n`, and `IRET` opcode emulation paths directly clear/restore the hardware `EFLAGS_IF` bit (`0x00000200`) in `regs->eflags` instead of virtualizing via `EFLAGS_VIF` (`0x00080000`), allowing an unprivileged VM86 task to disable hardware interrupts on the CPU.
    2. `vm86_bios_call()` allocates `struct vm86_struct info` on the **kernel stack** and passes `&info` to `sys_vm86()`, which immediately calls `copyin(info, &k_info, sizeof(...))`. Because `copyin` rejects kernel addresses (`>= KERNEL_BASE`), `vm86_bios_call()` always fails with `-EFAULT` after identity-mapping the low 1 MiB into `pmap_kernel()` without unmapping it.
  - **Requirement (Event-Driven / Unwanted Behavior):**
    - **When** a VM86 guest executes interrupt-flag instructions (`CLI`, `STI`, `POPF`, `IRET`, `INT`), the **VM86 Monitor** **shall** manipulate only `EFLAGS_VIF` and keep hardware `EFLAGS_IF` set.
    - **When** `vm86_bios_call()` invokes VM86 execution from kernel context, the **VM86 Subsystem** **shall** call an internal kernel entry point (`kern_vm86()`) rather than passing a kernel stack pointer to `copyin()`.

- [x] **[REQ-SYS-ARCH-002]** *(High — AP Kernel Stack Size Mismatch in `sys/arch/x86-common/smp_discovery.c`)*
  - **Location:** `sys/arch/x86-common/smp_discovery.c`, `sys/pm/sched.c`
  - **Finding:** While `sys/pm/sched.c` allocates 16 KiB (`4 * 4096`) kernel stacks because deep VFS/ext2/networking call chains overflow 4 KiB stacks, the SMP AP bring-up path allocates a single 4 KiB page (`pmm_alloc_block()`) per Application Processor initial/idle stack.
  - **Resolution (2026-11-06):** the stacks were static 4 KiB arrays in `sys/arch/i386/smp_discovery.c`; each AP now gets a `pmm_alloc_contiguous(THREAD_KSTACK_PAGES)` stack when first booted (`THREAD_KSTACK_SIZE`, `sys/include/sys/proc.h`).  Nothing calls `smp_boot_all_aps()` yet, so the path is checked by the build only.
  - **Requirement (Ubiquitous):**
    - The **SMP Bootstrap Subsystem** **shall** allocate 16 KiB (`pmm_alloc_contiguous(4)`) kernel stacks for each Application Processor to match the scheduler's stack-size invariant.

---

## 2. Virtual Memory (`sys/vm/`)

- [ ] **[REQ-SYS-VM-001]** *(Critical — Premature `cow_src` Free / Double-Free in `vm_fault()` and `vm_object_collapse()`)*
  - **Location:** `sys/vm/vm_fault.c:470-530` (`vm_fault`), `sys/vm/vm_object.c:455-526` (`vm_object_collapse`), `sys/vm/vm_object.c:203-270` (`vm_object_deallocate`)
  - **Finding:**
    1. In `vm_fault()`, when copying a COW source page (`cow_src`), the check on `cow_src->pv_list == NULL` and `cow_src->ref_count` is performed **before** `pmap_enter()` (line 523) overwrites the faulting process's read-only PTE and removes its PV entry. Furthermore, if `cow_src` resides in a backing `vm_object` whose `ref_count == 1` after a sibling exit while the page is still mapped read-only in a parent/child PTE, freeing `cow_src` without `pmap_page_protect(cow_src, VM_PROT_NONE)` leaves live hardware PTEs pointing to a freed physical frame.
    2. Similarly, `vm_object_collapse()` frees overshadowed pages in `backing` via `vm_page_free(p)` without first revoking live hardware PTE mappings via `pv_list`.
    3. `vm_fault_object_lock` is `static` to `sys/vm/vm_fault.c`, leaving concurrent `vm_object` mutations in `vm_map_fork()`, `vm_object_collapse()`, `vm_object_deallocate()`, and `sys_msync()` unsynchronized against active page faults.
  - **Requirement (Event-Driven / Ubiquitous):**
    - **When** replacing or collapsing a COW page in `vm_fault()` or `vm_object_collapse()`, the **VM Subsystem** **shall** update hardware mappings (`pmap_enter` / `pmap_page_protect`) before evaluating `cow_src->pv_list` and `cow_src->ref_count` for page reclamation.
    - The **VM Subsystem** **shall** synchronize all `vm_object` page-list and shadow-chain mutations across `vm_fault`, `vm_map_fork`, `vm_object_collapse`, and `vm_object_deallocate` using a shared or per-object lock.

- [ ] **[REQ-SYS-VM-002]** *(Critical — Splay-Tree Corruption Under Read Lock in `vm_map_lookup()`)*
  - **Location:** `sys/vm/vm_map.c:841-900` (`vm_map_lookup_entry`), `sys/vm/vm_map.c:1144-1195` (`vm_map_lookup`, `vm_map_cow_adopt`, `vm_map_shared_key`)
  - **Finding:** `vm_map_lookup()` calls `vm_map_lookup_entry()`, which splays (rotates and mutates) `map->root` and writes `map->hint = entry`. However, `vm_map_cow_adopt()` and `vm_map_shared_key()` call `vm_map_lookup()` while holding only `vm_map_lock_read(map)`. Concurrent page faults across threads of the same process corrupt the `vm_map` splay tree.
  - **Requirement (Ubiquitous):**
    - The **VM Map Manager** **shall** either perform non-splaying tree walks when holding a shared read lock (`vm_map_lock_read`) or acquire an exclusive write lock before splaying `map->root` and updating `map->hint`.

- [ ] **[REQ-SYS-VM-003]** *(Critical — Multi-Page `UMA_ZONE_OFFPAGE` Slab Hash Lookup Failure in `uma_find_slab()`)*
  - **Location:** `sys/vm/uma_core.c:968-1060` (`uma_find_slab`, `uma_item_size`), `sys/vm/uma_core.c:600-960` (`uma_zalloc`, `uma_zfree`)
  - **Finding:**
    1. For `UMA_ZONE_OFFPAGE` multi-page slabs, `uma_hash_insert()` hashes only the base virtual address `slab->us_data`. When freeing or querying an item that lies on page `+1..+(N-1)` of the contiguous allocation, `uma_find_slab()` and `uma_item_size()` hash `(uintptr_t)item & ~0xFFF`, missing the hash bucket and leaking the allocation or miscomputing the item offset relative to `page_base` instead of `slab->us_data`.
    2. Zone statistics counters (`uz_allocs`, `uz_frees`, `uz_count`, `uz_max`) and the global `uma_zones` list are modified without inter-CPU synchronization or atomic operations.
  - **Requirement (Event-Driven / Ubiquitous):**
    - **When** an off-page UMA slab spans multiple pages, the **UMA Allocator** **shall** register all constituent pages in `uma_page_hash` and compute item offsets relative to `slab->us_data`.
    - The **UMA Allocator** **shall** update zone counters using atomic operations and serialize `uma_zones` list modifications under a lock.

- [ ] **[REQ-SYS-VM-004]** *(High — Integer Overflows & Missing Range Checks in `sys_msync()`, `sys_brk_locked()`, and `device_haspage()`)*
  - **Location:** `sys/vm/vm_syscalls.c:539-630` (`sys_brk_locked`), `sys/vm/vm_syscalls.c:708-760` (`sys_msync`), `sys/vm/vm_pager.c:158-175` (`device_haspage`), `sys/vm/vm_pager.c:270-370` (`vnode_pager_getpages`, `vnode_haspage`)
  - **Finding:**
    1. `sys_msync()` computes `(uintptr_t)addr + length + 0xFFF` without overflow or user-range validation.
    2. `sys_brk_locked()` maps/unmaps pages directly via `pmap_enter`/`pmap_remove` without inserting or checking `vm_map` entries, allowing `brk` to overlap existing `mmap` regions, and truncates physical addresses on 64-bit via `uint32_t pa = pmap_extract(...) & ~0xFFFU`.
    3. `device_haspage()` computes `(uintptr_t)pindex * 4096` in 32 bits, wrapping to 0 for `pindex >= 0x100000`.
    4. `vnode_pager_getpages()` marks pages valid with uninitialized contents when `read_fs()` returns a negative errno cast to `size_t`, and `vnode_haspage()` uses a static `vpager->page_limit` that ignores `vpager->base_offset` and subsequent file growth/truncation.
  - **Requirement (Unwanted Behavior):**
    - **If** `addr + length` overflows, crosses `KERNEL_VA_START`, or collides with existing `vm_map` entries in `sys_msync()` or `sys_brk()`, **then** the **VM Syscall Layer** **shall** reject the request with `-EINVAL` or `-ENOMEM`.
    - **If** `read_fs()` returns an error in `vnode_pager_getpages()`, **then** the **Vnode Pager** **shall** zero the page and return an error rather than marking uninitialized memory valid.

- [ ] **[REQ-SYS-VM-005]** *(High — Wired Queue Corruption in `vm_page_free()` and `VM_INHERIT_ZERO` Handling in `vm_map_fork()`)*
  - **Location:** `sys/vm/vm_page.c:618-670` (`vm_page_free`), `sys/vm/vm_map.c:1496-1610` (`vm_map_fork`), `sys/vm/phys_mem.c:473-540` (`vm_phys_alloc_contiguous`, `vm_phys_free_contiguous`)
  - **Finding:**
    1. `vm_page_free()` removes pages from `active_queue` (`PG_ACTIVE`) and `inactive_queue` (`PG_INACTIVE`), but never checks or removes pages from `wired_queue` (`m->wire_count > 0`) or `laundry_queue`, corrupting global page doubly-linked lists if a wired page is freed on process teardown.
    2. In `vm_map_fork()`, entries with `VM_INHERIT_ZERO` do not clear the cloned child PTEs via `pmap_fork_clear_range()` and set `new_obj = NULL`, causing child writes to fault with `SIGSEGV` while child reads still see parent memory via stale PTEs.
    3. `vm_phys_alloc_contiguous()` loops `while ((1UL << order) < count) order++;` without bounding `order < PMM_MAX_ORDER`, risking undefined shift behavior on large `count`.
  - **Requirement (Event-Driven):**
    - **When** `vm_page_free()` frees a page that is still on `wired_queue` or `laundry_queue`, the **VM Page Manager** **shall** unlink it from its queue and decrement the corresponding queue counter before returning the frame to the buddy allocator.
    - **When** `vm_map_fork()` processes a `VM_INHERIT_ZERO` entry, the **VM Map Manager** **shall** clear the child's hardware PTE range and allocate a fresh anonymous `vm_object`.

---

## 3. Process Management, Scheduler & Core Kernel (`sys/pm/` & `sys/kern/`)

- [ ] **[REQ-SYS-PM-001]** *(Critical — Premature Kernel Stack Free During Context Switch & Autoreap in `sched_context_switch()` and `proc_exit()`)*
  - **Location:** `sys/pm/sched.c:341-410` (`sched_context_switch`), `sys/pm/sched.c:924-985` (`sched_reap_process_threads`, `sched_reap_thread`), `sys/pm/process.c:1913-2260` (`proc_reap_autoreap_zombies`, `proc_exit`)
  - **Finding:**
    1. In `sched_context_switch()`, `THIS_CPU()->current = next` is written **before** `futex_wake_exited_thread(prev)`, `pmap_activate()`, and `arch_switch_to(prev, next)`. On SMP, once `THIS_CPU()->current` changes, `sched_thread_running_remote(prev)` returns `false`, allowing another CPU in `sched_reap_thread()` to free `prev->kstack_Owned` while the exiting CPU is still executing on that stack.
    2. In `proc_exit()`, `current_thread->state = THREAD_ZOMBIE` and `P_AUTOREAP` are set before calling `proc_remove_child()`, which acquires a sleepable `mutex_lock(&proctree_lock)`. If `mutex_lock` blocks and yields, another CPU's idle loop (`proc_reap_autoreap_zombies()`) reaps and frees the process and its kernel stack before `proc_exit()` finishes.
    3. `sched_reap_process_threads()` walks `allthread` unlocked while calling `sched_reap_thread()`, which unlinks threads from `allthread`, risking use-after-free of the cached `next` pointer.
  - **Requirement (State-Driven):**
    - **While** an exiting thread is still executing on its kernel stack prior to the final `arch_switch_to()` handoff, the **Scheduler and Process Manager** **shall** keep the thread marked on-CPU (and defer `THREAD_ZOMBIE` / `autoreap_pending` publication until after all sleeping locks complete) so no remote CPU can reap its stack.

- [ ] **[REQ-SYS-PM-002]** *(High — Process Group / Session Leaks & Missing Locking in `sys/pm/pgrp.c` and `sys/pm/wait.c`)*
  - **Location:** `sys/pm/pgrp.c:67-265` (`session_free`, `__pgrp_remove_proc`), `sys/pm/pgrp.c:359-430` (`sys_setpgid`), `sys/pm/wait.c:66-150` (`find_waitable_child`)
  - **Finding:**
    1. `__pgrp_remove_proc()` never unlinks or frees an empty `struct pgrp` when its last member moves to another process group via `sys_setpgid()`, permanently leaking both the `pgrp` and its `session`.
    2. `sys_setpgid()` does not reject attempts by a session leader (`target->p_pgrp->pg_session->s_leader == target`) to change its PGID (`-EPERM` per POSIX).
    3. `find_waitable_child()` computes `-pid` when `pid < -1`, triggering signed integer overflow UB when `pid == INT_MIN`.
  - **Requirement (Event-Driven / Unwanted Behavior):**
    - **When** the last process leaves a process group in `__pgrp_remove_proc()`, the **Process Group Manager** **shall** unlink and free the empty `pgrp` and decrement the owning session reference count.
    - **If** `sys_setpgid()` targets a session leader or `wait4()` receives `pid == INT_MIN`, **then** the **Process Manager** **shall** return `-EPERM` and `-ESRCH` respectively.

- [ ] **[REQ-SYS-KERN-001]** *(Critical — `thread->next` List Corruption Between `turnstile_block()` and `sleepq_add()`)*
  - **Location:** `sys/kern/turnstile.c:130-195` (`turnstile_block`), `sys/kern/sleepq.c:238-295` (`sleepq_add_internal`), `sys/kern/sleepq.c:575-690` (`sleepq_requeue_internal`)
  - **Finding:**
    1. `turnstile_block()` chains blocked threads onto `ts->ts_waiters` using `current_thread->next`, and `lockmgr()` immediately follows `turnstile_block()` with `sleepq_add()`, which overwrites `current_thread->next` to link the thread into the sleepqueue bucket.
    2. `sleepq_add_internal()` calls `sleepq_alloc()` -> `kmalloc()` while holding a spinlock with interrupts disabled.
  - **Requirement (Ubiquitous):**
    - The **Turnstile and Sleepqueue Subsystems** **shall** use distinct intrusive list linkage fields on `thread_t` (e.g., `t_ts_next` vs `t_sleepq_next`) and **shall not** invoke sleeping allocators while holding an interrupt-disabled spinlock.

- [ ] **[REQ-SYS-KERN-002]** *(High — Arbitrary Kernel Memory Read/Write via Unchecked `addr` in `sys_ptrace()`)*
  - **Location:** `sys/kern/ptrace.c:207-360` (`ptrace_fault_in`, `sys_ptrace`)
  - **Finding:** `ptrace_fault_in()` and the `PTRACE_PEEKTEXT`/`PTRACE_PEEKDATA`/`PTRACE_POKETEXT`/`PTRACE_POKEDATA` handlers pass `(uint32_t)addr` directly to `pmap_copyin_other()` / `pmap_copyout_other()` without verifying that `addr` and `addr + len` reside below `KERNEL_VA_START`. Because kernel mappings are shared across every process's `pmap`, a tracer can read or overwrite arbitrary kernel memory via its tracee. Additionally, `sys_ptrace()` does not verify that `tracee` is stopped (`SSTOP`) before reading or modifying its state.
  - **Requirement (Unwanted Behavior):**
    - **If** a `ptrace` memory request specifies an address range outside `[VM_MIN_ADDRESS, USER_VA_END)` or targets a tracee that is not stopped, **then** `sys_ptrace()` **shall** reject the request with `-EIO`, `-EFAULT`, or `-ESRCH`.

- [ ] **[REQ-SYS-KERN-003]** *(High — Signed Length Bypass & Dangling Waiters in `kern_mq_send()` and `futex_lock_pi()`)*
  - **Location:** `sys/kern/posix_mqueue.c:500-680` (`kern_mq_send`, `kern_mq_receive`), `sys/kern/futex.c:1089-1220` (`futex_lock_pi`), `sys/kern/posix_sem.c:199-370` (`ksem_drop_open`, `kern_ksem_open`)
  - **Finding:**
    1. `kern_mq_send()` validates message size with `if ((long)len > q->attr.mq_msgsize)`, allowing `len >= 0x80000000UL` on 32-bit to pass as negative if called from a personality entry point.
    2. If a thread is killed (`THREAD_ZOMBIE`) while blocked in `futex_lock_pi()` or `kern_mq_receive()`, its waiter entry / `recv_waiters` count is not cleaned up on thread exit, leaving a dangling `thread_t *` in `ps->waiters`.
  - **Requirement (Event-Driven):**
    - **When** validating message lengths in `kern_mq_send()`, the **POSIX Message Queue Subsystem** **shall** use unsigned comparisons (`len > (size_t)q->attr.mq_msgsize`), and **when** a blocked thread is aborted by signal or process exit, **shall** unlink its PI-futex and message-queue waiter state before returning.

---

## 4. VFS, Filesystems & Exec (`sys/vfs/`, `sys/fs/` & `sys/exec/`)

- [ ] **[REQ-SYS-FS-001]** *(Critical — 368-Byte Heap Buffer Overflow in `fat_mount()` and Open-File Cluster Free in `fat_unlink()`)*
  - **Location:** `sys/fs/fat/fat.c:989-1035` (`fat_mount`), `sys/fs/fat/fat.c:2097-2160` (`fat_unlink`)
  - **Finding:**
    1. In `fat_mount()`, `fat_read_sectors(fs, 0, 1, &fs->bpb)` reads a full 512-byte sector directly into `&fs->bpb` (`sizeof(fat_bpb_t) == 36` bytes inside `sizeof(fat_fs_t) == 148` bytes), overflowing `fat_fs_t` on the kernel heap by 368 bytes on every FAT mount.
    2. `fat_unlink()` immediately frees a file's cluster chain even if the file is currently open and pinned in `fat_node_cache`, leading to cluster reuse and data corruption while open descriptors still write to the unlinked file.
  - **Requirement (Event-Driven):**
    - **When** `fat_mount()` reads the boot sector, the **FAT Filesystem Driver** **shall** read the 512-byte sector into a 512-byte scratch buffer and copy only `sizeof(fat_bpb_t)` / `sizeof(fat32_ext_bpb_t)` into `fs`.
    - **When** `fat_unlink()` removes a directory entry for a file that still has active open/held references, the **FAT Filesystem Driver** **shall** defer cluster chain reclamation until the final reference is released.

- [ ] **[REQ-SYS-VFS-001]** *(High — Unpinned `fs_node_t` Recycling Across `fsnode_vop_lookup()` and `vfs_mount_legacy()`)*
  - **Location:** `sys/vfs/vnode.c:170-235` (`fsnode_vop_lookup`), `sys/vfs/vfs.c:322-420` (`vfs_mount_legacy`), `sys/vfs/vfs.c:2164-2310` (`vfs_unmount_covered`)
  - **Finding:**
    1. `fsnode_vop_lookup()` wraps `child` (`fs_node_t *`) into `vp->v_data` and immediately calls `vfs_rele(child)`, dropping the `fs_node_t` hold while `vp` stays alive and allowing the filesystem node cache (`ext2`, `fat`, `procfs`) to recycle `child` under `vp`.
    2. `vfs_mount_legacy()` sets `mountpoint->flags |= FS_MOUNTPOINT`, stores `mnt->mnt_node_covered = mountpoint`, and releases `mountpoint`. When `ext2` recycles the unpinned `mountpoint` cache slot, `FS_MOUNTPOINT` is lost (breaking `umount` in `vfs_unmount_covered()`) and `devfs`/`sysfs` `".."` lookups dereference a stale `mnt_node_covered`.
  - **Requirement (State-Driven):**
    - **While** a `struct vnode` or `struct mount` (`mnt_node_covered`) references an `fs_node_t`, the **VFS Layer** **shall** retain a `vfs_hold()` reference until the `vnode` is reclaimed or the filesystem is unmounted.

- [ ] **[REQ-SYS-VFS-002]** *(High — Missing Permission Check in `kern_utimensat()`, Relative Path Bug in `sys_truncate()`, and `bufsync()` Retry Spin)*
  - **Location:** `sys/vfs/vfs.c:2790-2860` (`kern_utimensat`), `sys/vfs/vfs.c:2440-2490` (`sys_truncate`), `sys/vfs/bio.c:581-618` (`bwrite`), `sys/vfs/bio.c:690-719` (`bufsync`)
  - **Finding:**
    1. `kern_utimensat()` updates file timestamps via `setattr_fs()` without verifying that `current_process->euid == 0`, `current_process->euid == node->uid`, or (when `times == NULL` / `UTIME_NOW`) the caller has write permission on the file.
    2. `sys_truncate()` resolves relative paths against `fs_root` instead of `current_process->cwd_node`.
    3. `bwrite()` re-marks buffers `B_DELWRI` on I/O error, causing `bufsync()` to spin infinitely in `TAILQ_FIRST(&bufqueues[BQ_DIRTY])` when a block device returns a persistent write error.
  - **Requirement (Unwanted Behavior / Event-Driven):**
    - **If** an unprivileged caller invokes `utimensat()` on a file it does not own (or without write permission when setting current time), **then** `kern_utimensat()` **shall** return `-EACCES` or `-EPERM`.
    - **When** `sys_truncate()` receives a relative path, the **VFS Layer** **shall** resolve it relative to `current_process->cwd_node`.
    - **When** `bufsync()` iterates `BQ_DIRTY`, the **Buffer Cache** **shall** bound each pass to the buffers present at the start of the pass so failed re-queued buffers cannot cause an infinite loop.

---

## 5. Networking & Device Drivers (`sys/net/` & `sys/drivers/`)

- [ ] **[REQ-SYS-DRV-001]** *(Critical — Direct User-Pointer Dereference in `tty_ioctl_kern()` via `tty_ioctl()` Fallback)*
  - **Location:** `sys/drivers/console/tty.c:88-96` (`tty_node_ioctl`), `sys/drivers/console/tty.c:1320-1575` (`tty_ioctl_kern`, `tty_ioctl`)
  - **Finding:** While `tty_ioctl()` copies user arguments into a kernel stack buffer before calling `tty_ioctl_kern()`, it falls back to passing the raw user pointer `arg` directly to `tty_ioctl_kern()` whenever `copyin()` fails (such as when a user process passes a kernel address `>= 0xC0000000`). Inside `tty_ioctl_kern()`, `TCGETS`, `TCSETS*`, `TIOCGWINSZ`, `TIOCSWINSZ`, `TIOCGPGRP`, and `TIOCSPGRP` directly dereference `arg`, enabling arbitrary kernel memory read/write from ring 3.
  - **Requirement (Unwanted Behavior):**
    - **If** `copyin()` or `copyout()` fails in `tty_ioctl()`, **then** the **TTY Driver** **shall** immediately return `-EFAULT` and **shall never** fall back to invoking `tty_ioctl_kern()` with an unvalidated pointer.

- [ ] **[REQ-SYS-NET-001]** *(Critical — Direct User-Buffer `memcpy` Under IRQ Spinlock in `netdev_sub_recv()`)*
  - **Location:** `sys/net/netdev.c:286-335` (`netdev_sub_recv`), `sys/net/af_packet.c` (`afpacket_recvfrom`)
  - **Finding:** `afpacket_recvfrom()` passes the unvalidated user pointer `buf` directly into `netdev_sub_recv()`, which executes `memcpy(buf, s->buf, copy)` while holding `sub->lock` with interrupts disabled (`spinlock_acquire_irq`). Passing a kernel address overwrites kernel memory; passing an unmapped or swapped user page triggers a page fault with interrupts disabled while holding a spinlock.
  - **Requirement (Event-Driven):**
    - **When** copying a packet to userspace in `AF_PACKET` / `netdev_sub_recv()`, the **Network Device Layer** **shall** dequeue the frame into a kernel buffer under `sub->lock`, release the spinlock, and transfer the payload to userspace via `copyout()`.

- [ ] **[REQ-SYS-NET-002]** *(High — Socket Use-After-Free in `afi_from_fd()`, Peer-Controlled MSS DoS in `tcp.c`, and Missing ICMPv6 Validation)*
  - **Location:** `sys/net/af_inet.c:1217-1235` (`afi_from_fd`), `sys/net/tcp.c:559-575` (`tcp_eff_mss`), `sys/net/tcp.c:2041-2260` (`tcp_input_locked`, `tcp_resegment_locked`), `sys/net/icmp.c:382-490` (`icmp6_handle_ns`, `icmp6_handle_na`, `icmp6_input`)
  - **Finding:**
    1. `afi_from_fd()` returns `afi_sock_t *` from `current_process->fds[fd]` without incrementing `s->refs`. Blocking calls (`afinet_connect`, `afinet_accept`, `afinet_recvfrom_rx`) race with a concurrent `close(fd)` on another thread that frees `s` while the blocking call is asleep.
    2. In `sys/net/tcp.c`, `tcp_eff_mss()` does not enforce an RFC 6691 / minimum MSS floor (e.g., 64 bytes) and updates `p->syn_mss` before validating the segment sequence number in `tcp_input_locked()`, allowing a peer advertising a tiny MSS (`0` or `1`) with IP options to trigger unbounded resegmentation in `tcp_resegment_locked()`.
    3. `icmp6_input()` does not verify the ICMPv6 checksum or RFC 4861 `hop_limit == 255` on Neighbor Solicitation / Neighbor Advertisement packets before calling `nd6_insert()`.
  - **Requirement (Event-Driven / Unwanted Behavior):**
    - **When** `afi_from_fd()` resolves a socket descriptor, the **`AF_INET` Layer** **shall** atomically increment `s->refs` under lock and release it upon syscall completion.
    - **If** a remote TCP peer advertises an MSS below `TCP_MIN_MSS` (64 bytes) or before sequence validation succeeds, **then** the **TCP Stack** **shall** clamp `snd_mss` and ignore unvalidated SYN MSS options.
    - **If** an incoming ICMPv6 packet fails checksum verification or an NS/NA packet has `hop_limit != 255`, **then** `icmp6_input()` **shall** drop the packet.

- [ ] **[REQ-SYS-DRV-002]** *(High — Master/Slave Use-After-Free & Linger Buffer Leak in `sys/drivers/devices/pty.c`)*
  - **Location:** `sys/drivers/devices/pty.c` (`pty_slave_drv_close`, `pty_master_node_read`, `pty_master_node_write`)
  - **Finding:**
    1. In `pty_slave_drv_close()`, repeatedly opening, writing to, and closing the slave while the master has not drained `p->linger` overwrites `p->linger` with a newly `kmalloc`ed `TTY_BUF_SIZE` buffer without freeing the previous `p->linger` buffer.
    2. `pty_master_node_read()` and `pty_master_node_write()` sleep on `p` without holding a reference count on `pty_pair_t`, risking use-after-free if the master FD is closed concurrently by another thread.
  - **Requirement (Event-Driven):**
    - **When** `pty_slave_drv_close()` allocates or updates `p->linger`, the **PTY Driver** **shall** free or append to any existing `p->linger` allocation, and **shall** reference-count `pty_pair_t` across blocking master/slave read and write operations.

---

## 6. Kernel Coding Style Rule Compliance (`.agent/rules/kernel-code-style-guide.md`)

- **Rule 2 (`extern` in `.c` files):** **PASS** (0 manual `extern` declarations across all non-generated `.c` files in `sys/`).
- **Rule 3 (Relative `#include "..."` paths):** **PASS** (0 relative `#include "..."` directives across all `.c` and `.h` files in `sys/`).
- [ ] **[REQ-SYS-STYLE-001]** *(Low — Rule 4: Internal Structs, Typedefs & Constants Defined in `.c` Files)*
  - **Location:** `sys/vm/vm_pager.c:39-90`, `sys/vm/vm_syscalls.c:129-135`, `sys/vm/vm_swap.h:15-16` vs `sys/vm/vm_pager.h:56-57`, `sys/net/af_inet.c`
  - **Finding:** Several subsystems define module-internal structs, typedefs, and macros directly inside `.c` implementation files rather than companion `.h` headers (e.g., `vnode_pager_t` / `swap_pager_t` in `sys/vm/vm_pager.c`, `shared_file_obj_entry_t` in `sys/vm/vm_syscalls.c`, `afi_sock_t` in `sys/net/af_inet.c`, and duplicate conflicting prototypes for `vm_swapon`/`vm_swapoff` between `sys/vm/vm_swap.h` and `sys/vm/vm_pager.h`).
  - **Requirement (Ubiquitous):**
    - The **Kernel Codebase** **shall** place shared and subsystem structure, typedef, and macro definitions in header files under `sys/` and maintain a single canonical prototype for `vm_swapon()` / `vm_swapoff()`.

---

## Summary Matrix

| Subsystem | Critical | High | Medium / Low | Total Actionable Items |
| :--- | :---: | :---: | :---: | :---: |
| `sys/arch/` & `sys/boot/` | 1 | 1 | 0 | **2** |
| `sys/vm/` | 3 | 2 | 0 | **5** |
| `sys/pm/` & `sys/kern/` | 2 | 3 | 0 | **5** |
| `sys/vfs/`, `sys/fs/` & `sys/exec/` | 1 | 2 | 0 | **3** |
| `sys/net/` & `sys/drivers/` | 2 | 2 | 0 | **4** |
| Coding Standards (`sys/`) | 0 | 0 | 1 | **1** |
| **Total** | **9** | **10** | **1** | **20** |
