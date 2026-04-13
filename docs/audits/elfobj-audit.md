# libelfobj Codebase Audit Report

**Scope:** `usr.lib/elfobj/`
**Date:** April 12, 2026

---

## Executive Summary

| Severity | Count | Key Areas |
|----------|-------|-----------|
| **CRITICAL** | 2 | String table validation, section link validation |
| **HIGH** | 2 | Integer overflow, section overlap |
| **MEDIUM** | 4 | Strtab growth, NULL dereference, group signature, compression header |
| **LOW** | 4 | Alignment validation, REL/RELA arch checks, diagnostic buffer, version index |
| **TOTAL** | **12** | |

This library parses untrusted ELF files supplied to the assembler/linker. All input data must be treated as adversarial.

---

## CRITICAL Findings

### 1. String Table Access Without Null-Terminator Guarantee

- **File:** `usr.lib/elfobj/src/elf_read.c`, lines 21-35
- **Function:** `safe_str()`
- **Issue:** The function scans for a null terminator within bounds, returning NULL if none found. This is correct in isolation. However, callers that use `safe_str()` for display/diagnostics but fall back to raw pointer access on NULL bypass the protection.
- **Code:**
  ```c
  static const char *safe_str(const uint8_t *base, size_t len, uint32_t off) {
      if (base == NULL || off >= len) return NULL;
      for (size_t i = off; i < len; ++i) {
          if (base[i] == '\0') return (const char *)(base + off);
      }
      return NULL;  // no null terminator found
  }
  ```
- **Risk:** If any code path uses strtab data without going through `safe_str()`, it reads until it hits a zero byte — potentially far beyond the section.
- **Fix:** Audit all strtab access paths to ensure they go through `safe_str()`. Consider forcing a null byte at `strtab[size-1]` during parse.

### 3. Section sh_link Validation — Partial State Corruption

- **File:** `usr.lib/elfobj/src/elf_read.c`, lines 393-410
- **Function:** `parse_symbols()`
- **Issue:** When `sec->link >= obj->section_count`, the function returns `ELF_ERR_FORMAT`. But earlier iterations of the loop may have already pushed symbols into the object. The object is left in an inconsistent state — some symbol tables loaded, others not.
- **Impact:** Callers that check for errors after `parse_symbols()` may still see partial data, leading to incorrect link output.
- **Fix:** Either validate all sections upfront before loading any, or roll back on error.

---

## HIGH Findings

### 4. Integer Overflow in Section Table Size Calculation

- **File:** `usr.lib/elfobj/src/elf_read.c`, lines 88-99
- **Function:** `parse_sections()`
- **Issue:** `shoff + table_size` is checked against file size, but the individual calculations use `elf__u64_mul()` which returns a `uint64_t`. On 32-bit targets, the subsequent `size_t` cast can truncate:
  ```c
  if (!elf__u64_mul((uint64_t)entsize, (uint64_t)shnum, &table_size))
      return ELF_ERR_BOUNDS;
  if (shoff > SIZE_MAX || table_size > SIZE_MAX)  // correct, but late
      return ELF_ERR_BOUNDS;
  ```
- **Fix:** Validate SIZE_MAX bounds immediately after the multiply, before any cast.

### 5. Section Overlap Detection Excludes NOBITS

- **File:** `usr.lib/elfobj/src/elf_read.c`, lines 123-135
- **Issue:** Only non-NOBITS, non-zero-size sections are checked for overlaps. Two NOBITS sections with conflicting `sh_addr` values are not detected.
- **Impact:** Confusing memory layout; linker may allocate overlapping BSS regions.
- **Fix:** Also validate `sh_addr` ranges for NOBITS sections.

---

## MEDIUM Findings

### 9. Strtab Growth Silent Failure on Extreme Sizes

- **File:** `usr.lib/elfobj/src/elf_strtab.c`, lines 31-59
- **Function:** `elf__strtab_add()`
- **Issue:** Doubling loop can reach the `(size_t)-1 / 2` limit and return 0 (success offset 0, which is the empty string slot) rather than signaling an error:
  ```c
  while (new_cap < tab->size + len) {
      if (new_cap > ((size_t)-1) / 2) return 0;  // looks like success!
      new_cap *= 2;
  }
  ```
- **Fix:** Return a distinct error value (e.g., `(uint32_t)-1`) on overflow.

### 10. Missing NULL Check After Allocation in Write Path

- **File:** `usr.lib/elfobj/src/elf_write.c`, lines 100-150
- **Function:** `build_symtab()`
- **Issue:** `elf__calloc()` return value is checked, but subsequent write operations within the buffer don't re-validate bounds.
- **Fix:** Defensive programming — add assertions.

### 12. Group Signature Name Extraction

- **File:** `usr.lib/elfobj/src/elf_link.c`, lines 113-160
- **Issue:** Group section's symtab link re-used without re-validation in linking context. If object was modified between parse and link, stale pointers possible.

### 14. DWARF Compression Header Overflow

- **File:** `usr.lib/elfobj/src/elf_dwarf.c`, lines 148-180
- **Issue:** 64-bit payload sizes from untrusted data are checked via `elf__u64_add()`, but the SIZE_MAX comparison happens after the addition:
  ```c
  if (total_size > SIZE_MAX || off + (size_t)total_size > size)
  ```
  On 32-bit, if `total_size > SIZE_MAX`, the cast `(size_t)total_size` silently truncates.
- **Fix:** Check `total_size > SIZE_MAX` before casting to `size_t`.

---

## LOW Findings

### 15. Missing Validation of Section Alignment Values

- **File:** `usr.lib/elfobj/src/elf_read.c`, lines 58-62
- **Issue:** Alignment values are only corrected from 0→1. Values like `0x80000000` (2GB alignment) are accepted.
- **Fix:** Require alignment to be a power of two and <= some reasonable maximum.

### 16. Machine=0 Bypasses REL/RELA Architecture Checks

- **File:** `usr.lib/elfobj/src/elf_read.c`, lines 494-517
- **Issue:** Architecture-specific relocation format checks (REL vs RELA) are only applied for known machines (ARM, x86, etc.). `machine=0` skips all checks.
- **Fix:** Reject `machine=0` or apply a default strict check.

### 17. Diagnostic Buffer Grows Without Limit

- **File:** `usr.lib/elfobj/src/elf_util.c`, lines 248-290
- **Issue:** Crafted ELF with many validation errors causes unbounded diagnostic buffer growth.
- **Fix:** Add a maximum diagnostic buffer size.

### 18. Symbol Version Index Not Cross-Checked

- **File:** `usr.lib/elfobj/src/elf_read.c`, lines 451-475
- **Issue:** GNU `.gnu.version` entries are stored as raw `uint16_t` values without validating against the version definition/need tables.

---

## Positive Notes

- Overflow-safe multiplication (`elf__u64_mul`) and addition (`elf__u64_add`) helpers used consistently.
- `safe_str()` function exists for string table access — the pattern is correct even if enforcement isn't complete.
- Section overlap detection (for non-NOBITS) is present.
- Fuzz harnesses already exist in `fuzz/` — good foundation for testing.
- Comprehensive test suite covering roundtrip, validation, layout, and ABI surface.

---

## Recommendations

1. **Immediate:** Initialize `rel->symbol = NULL` before conditional assignment (#2).
2. **Immediate:** Fix OOM cleanup to free in-progress map (#8).
3. **Immediate:** Validate relocation offsets against target section size (#6).
4. **Short-term:** Add maximum allocation size limit to prevent DoS (#7).
5. **Short-term:** Validate entsize against section-type minimums (#11).
6. **Short-term:** Audit all strtab access for `safe_str()` usage (#1).
7. **Medium-term:** Extend section overlap detection to NOBITS sections (#5).
8. **Medium-term:** Fix strtab growth silent failure (#9).
9. **Testing:** Run existing fuzz harnesses with extended corpus; add relocation-focused fuzzer.
