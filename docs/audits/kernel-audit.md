# Substrate Kernel Codebase Audit Report

**Scope:** `sys/` directory exclusively
**Date:** April 12, 2026
**Build status:** Clean — compiles with `-Wall -Wextra -Werror` and **zero warnings**

---

## Executive Summary

| Severity | Count | Key Areas |
|----------|-------|-----------|
| **MEDIUM** | 0 | *(all resolved)* |
| **LOW** | 10 | Code style, comments, hardcoded constants, performance |
| **TOTAL** | **10** | |

---

## MEDIUM Findings

*(All resolved)*

---

## LOW Findings

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
