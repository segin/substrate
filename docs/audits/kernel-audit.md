# Substrate Kernel Codebase Audit Report

**Scope:** `sys/` directory exclusively
**Date:** April 12, 2026
**Build status:** Clean — compiles with `-Wall -Wextra -Werror` and **zero warnings**

---

## Executive Summary

| Severity | Count | Key Areas |
|----------|-------|-----------|
| **MEDIUM** | 11 | Integer overflows, lock ordering gaps, static buffer races, hash sizing |
| **LOW** | 12 | Code style, comments, hardcoded constants, performance |
| **TOTAL** | **23** | |

---

## MEDIUM Findings

38. **lockmgr NULL holder race** — `sys/kern/lockmgr.c`: `lk_lockholder` checked and then used without lock.

39. **Ramdisk sector bounds wrap** — `sys/drivers/storage/ramdisk.c`: `offset + size` can wrap unsigned → OOB access.

40. **timeval_to_ticks overflow** — `sys/kern/time.c`: Large `tv_sec` overflows the multiplication.

41. **DMA address assumes direct-map** — `sys/kern/dma.c`: `cpu_addr - KERNEL_BASE` assumes identity mapping for all kernel memory.

42. **Static readdir buffers** — ext2, FAT, minix all use `static struct dirent` → data race if concurrent.

43. **Name cache weak hash** — `sys/vfs/vfs_cache.c`: Simple multiplicative hash into 1024 buckets → collision DoS.

44. **Lock ordering undocumented** — No documented hierarchy between bio_lock, vnode_freelist_lock, mount_lock → deadlock risk.

45. **Wait.c vm_map_destroy race** — `sys/pm/wait.c`: Destroys vmmap while another CPU might be mid-page-fault.

46. **Linux signal stack alignment** — `sys/exec/perso/linux/linux_sig.c`: 16-byte alignment may not satisfy x86 ABI pre-call requirement.

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

1. **Urgent:** Fix fork race conditions (#12, #13) — process management integrity.
2. **High:** Fix ext2 readlink overflow (#26).
3. **High:** Fix vm_object ref counting (#19) and IRQ dispatch UAF (#17).
4. **High:** Add lock ordering documentation and assertions (#44).
5. **Medium:** Address remaining VM and pmap issues (#14, #15, #16).
6. **Ongoing:** Standardize error codes, add thread-safety annotations.
