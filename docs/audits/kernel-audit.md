# Substrate Kernel Codebase Audit Report

**Scope:** `sys/` directory exclusively
**Date:** April 12, 2026
**Build status:** Clean — compiles with `-Wall -Wextra -Werror` and **zero warnings**

---

## Executive Summary

| Severity | Count | Key Areas |
|----------|-------|-----------|
| **CRITICAL** | 16 | ELF loader, VM, pmap, VirtIO, fork, copy functions, sleep/turnstile pools |
| **HIGH** | 16 | IRQ dispatch UAF, device refcount, VFS cache races, /dev/kmem, TLS validation |
| **MEDIUM** | 16 | Integer overflows, lock ordering gaps, static buffer races, hash sizing |
| **LOW** | 12 | Code style, comments, hardcoded constants, performance |
| **TOTAL** | **60** | |

---

## CRITICAL Findings (Must Fix)

### Security — Exploitable from Userspace

1. **ELF integer overflow → kernel mapping** — `sys/exec/formats/elf.c`: `vaddr + p_memsz` can wrap 32 bits, bypassing the `>= 0xC0000000` check. Crafted ELF loads segments into kernel space. *Privilege escalation.*

2. **ELF TLS segment not validated** — `sys/exec/formats/elf.c`: PT_TLS `p_vaddr + p_memsz` can point into kernel space. *Information leak / privilege escalation.*

3. **ELF segment overlap not checked** — Multiple PT_LOAD segments can map the same VA range, corrupting each other.

4. **FAT LFN integer overflow** — `sys/fs/fat/fat.c`: `(order & 0x3F - 1) * 13` can be negative. Crafted FAT directory entry → kernel heap corruption.

5. **UDF extent underflow** — `sys/fs/udf/udf.c`: `ext_len - ext_off` can wrap unsigned → arbitrary kernel heap read.

6. **/dev/kmem no address validation** — `sys/drivers/devices/kmem.c`: User-controlled offset read/written directly as kernel VA. Root can already read `/dev/kmem`, but there's no range validation at all.

### Stability — Kernel Crash / Hang

7. **subr_copy on_fault mechanism** — `sys/kern/subr_copy.c`: `copyin`/`copyout`/`copyinstr` store local label addresses via `&&fault` in `current_thread->on_fault`, which is never cleared on success. Stale fault handler → stack corruption on unrelated page fault.

8. **Sleep queue pool exhausted → threads lost** — `sys/kern/sleepq.c`: Fixed 128-entry pool. `sleepq_alloc()` returns NULL silently; callers don't check → threads lost forever.

9. **Turnstile pool exhausted → priority inversion** — `sys/kern/turnstile.c`: Fixed 64-entry pool. Same pattern as sleepq.

10. **VirtIO DMA uses kernel virtual addresses** — `sys/drivers/virtio/virtio_blk.c`: Descriptor `addr` fields contain `0xC0000000+` addresses. Device reads wrong physical memory → data corruption / hang.

11. **ELF loader leaks pages on failure** — `sys/exec/formats/elf.c`: pmm-allocated pages and `page_maps` array not freed when `read()` fails mid-segment. DoS via repeated exec of malformed ELFs.

12. **Fork race — child visible before ready** — `sys/pm/process.c`: `proc_add_child()` links child to parent before thread creation completes. `wait()` can reap half-initialized child → double-free on pmap.

13. **Fork race — proctree_lock not held** — `sys/pm/process.c`: Lock released before `proc_add_child()`. Parent exit during the gap orphans child without reparenting.

### Memory Safety — pmap Layer

14. **pmap_fork accesses inactive pmap via direct-map** — `sys/arch/i386/pmap.c`: `src_pt_phys + 0xC0000000` assumes all page tables are in direct-map range. No bounds check.

15. **pmap_copy same issue** — `sys/arch/i386/pmap.c`: Destination pmap dereferenced via `pdir_phys + 0xC0000000` without validation.

16. **vm_map_insert: hole_consume() called without write lock** — `sys/vm/vm_map.c`: RB-tree modified before lock is acquired → memory corruption in concurrent allocations.

---

## HIGH Findings

### Concurrency

17. **IRQ dispatch use-after-free** — `sys/kern/irq.c`: Lock released before calling handler, `curr->next` read after re-acquire → `curr` may be freed by concurrent `free_irq()`.

18. **Vnode vrele use-after-free** — `sys/vfs/vnode.c`: After `vnode_reclaim()`, vnode may be freed but function continues to use it.

19. **vm_object_deallocate no atomics** — `sys/vm/vm_object.c`: `ref_count--` without locking → two threads both see 0 → double-free.

20. **Name cache TOCTOU** — `sys/vfs/vfs_cache.c`: `vref(*vpp)` under lock, but vnode can be freed after lock release before caller uses it.

21. **Driver probe TOCTOU** — `sys/kern/driver.c`: Device list iterated while lock is released during probe. `bus_next` can point to freed device.

### Validation

22. **Device refcount underflow → double-free** — `sys/kern/device.c`: No guard against `ref_count <= 0` before free.

23. **UMA slab bounds check after use** — `sys/vm/uma_core.c`: Index calculated and used before bounds check → buffer overwrite.

24. **FAT cluster chain bounds** — `sys/fs/fat/fat.c`: `fat32[cluster]` index not properly validated against `fat_table_size / 4`.

25. **vm_map_init silently returns NULL header** — `sys/vm/vm_map.c`: If sentinel alloc fails, `map->header = NULL` → crash on any lookup.

26. **EXT2 readlink stack buffer overflow** — `sys/fs/ext2/ext2.c`: trusts inode `i_size` for 256-byte buffer.

27. **Spinlock cpu_id set after acquisition** — `sys/kern/spinlock.c`: Window where `spinlock_is_held()` returns false for a held lock → missed recursive lock detection.

### Resource Leaks

28. **procfs memory leak** — `sys/fs/procfs.c`: Multiple early returns bypass `kfree()` of allocated buffer. DoS via repeated `/proc` reads.

29. **syscall.c kern_write leak** — `sys/kern/syscall.c`: Partial-write error path returns without freeing `kbuf`.

30. **TTY device node no refcount** — `sys/drivers/console/tty.c`: Devfs retains reference to `fs_node_t` after TTY free → UAF.

31. **Device unregister orphans children** — `sys/kern/device.c`: Zeroing `sibling` pointer breaks linked list → children lost.

32. **Symlink loop returns node instead of error** — `sys/vfs/vfs.c`: At `MAX_SYMLINK_DEPTH`, returns the symlink instead of ELOOP. Callers may retry infinitely.

---

## MEDIUM Findings

33. **vm_kmem integer overflow** — `sys/vm/vm_kmem.c`: `size + sizeof(header)` can wrap → too-small allocation → heap overflow.

34. **rb_delete_fixup NULL crash** — `sys/vm/vm_map.c`: If both `x` and `x_parent` are NULL, `p->left` dereferences NULL.

35. **OOM victim selection no proc_lock** — `sys/vm/vm_page.c`: Iterates process slots without locking → UAF on concurrent exit.

36. **P2V macro inconsistency in vm_swap** — `sys/vm/vm_swap.c`: Some code paths may double-offset `phys_addr`.

37. **Mutex adaptive spin TOCTOU** — `sys/kern/mutex.c`: Owner pointer read and checked across multiple statements without synchronization.

38. **lockmgr NULL holder race** — `sys/kern/lockmgr.c`: `lk_lockholder` checked and then used without lock.

39. **Ramdisk sector bounds wrap** — `sys/drivers/storage/ramdisk.c`: `offset + size` can wrap unsigned → OOB access.

40. **timeval_to_ticks overflow** — `sys/kern/time.c`: Large `tv_sec` overflows the multiplication.

41. **DMA address assumes direct-map** — `sys/kern/dma.c`: `cpu_addr - KERNEL_BASE` assumes identity mapping for all kernel memory.

42. **Static readdir buffers** — ext2, FAT, minix all use `static struct dirent` → data race if concurrent.

43. **Name cache weak hash** — `sys/vfs/vfs_cache.c`: Simple multiplicative hash into 1024 buckets → collision DoS.

44. **Lock ordering undocumented** — No documented hierarchy between bio_lock, vnode_freelist_lock, mount_lock → deadlock risk.

45. **Wait.c vm_map_destroy race** — `sys/pm/wait.c`: Destroys vmmap while another CPU might be mid-page-fault.

46. **Linux signal stack alignment** — `sys/exec/perso/linux/linux_sig.c`: 16-byte alignment may not satisfy x86 ABI pre-call requirement.

47. **VirtIO no barrier before notify** — `sys/drivers/virtio/virtio_blk.c`: Missing explicit memory fence.

48. **pmap_enter no PTE_D for writable** — `sys/arch/i386/pmap.c`: Relies entirely on hardware dirty bit; no comment documenting this.

---

## LOW Findings

49. Fixed pool sizes hardcoded (sleepq 128, turnstile 64, pgrp hash 16).

50. Inconsistent error return codes (-1 vs -errno vs positive errno).

51. Naive O(nm) `strstr()` in `main.c`.

52. Missing thread-safety documentation on most functions.

53. `pmap_activate` has misplaced PCID comment.

54. ELF image cache has no LRU/collision strategy.

55. DevFS 128-byte name field could overflow from long paths.

56. Console input ring buffer not fully atomic on SMP.

57. Futex robust list walk limit of 4096 could miss entries.

58. `boot.S` maps 4MB for LAPIC when 4KB suffices.

59. Early GDT → full GDT transition not documented.

60. Framebuffer luma calculation not cached.

---

## Build System Notes (Positive)

- Compiles cleanly with `-Wall -Wextra -Werror` — **zero warnings**.
- Two-pass link for kernel symbol table is correct.
- Supports multiboot, EFI, FreeBSD, and zImage output formats.
- Recursive Makefile structure is consistent.

---

## Recommendations (Priority Order)

1. **Immediate:** Fix ELF loader integer overflow and segment validation (#1, #2, #3) — these are exploitable from any local user running a crafted binary.
2. **Immediate:** Fix VirtIO DMA addressing (#10) — silent data corruption.
3. **Immediate:** Fix `subr_copy.c` on_fault cleanup (#7) — latent crash on any page fault after copyin/copyout.
4. **Urgent:** Fix sleep queue and turnstile pool exhaustion (#8, #9) — system hang under load.
5. **Urgent:** Fix fork race conditions (#12, #13) — process management integrity.
6. **High:** Add bounds validation for FS parsers (FAT #4, UDF #5, ext2 #26).
7. **High:** Fix vm_object ref counting (#19) and IRQ dispatch UAF (#17).
8. **High:** Add lock ordering documentation and assertions (#44).
9. **Medium:** Address remaining VM and pmap issues (#14, #15, #16).
10. **Ongoing:** Standardize error codes, add thread-safety annotations.
