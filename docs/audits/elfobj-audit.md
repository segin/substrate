# libelfobj Codebase Audit Report

**Scope:** `usr.lib/elfobj/`
**Date:** April 12, 2026

---

## Executive Summary

| Severity | Count | Key Areas |
|----------|-------|-----------|
| **CRITICAL** | 2 | String table validation, section link validation |
| **HIGH** | 2 | Integer overflow, section overlap |
| **MEDIUM** | 2 | NULL dereference, group signature |
| **LOW** | 3 | REL/RELA arch checks, diagnostic buffer, version index |
| **TOTAL** | **9** | |

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

### 10. Missing NULL Check After Allocation in Write Path

- **File:** `usr.lib/elfobj/src/elf_write.c`, lines 100-150
- **Function:** `build_symtab()`
- **Issue:** `elf__calloc()` return value is checked, but subsequent write operations within the buffer don't re-validate bounds.
- **Fix:** Defensive programming — add assertions.

### 12. Group Signature Name Extraction

- **File:** `usr.lib/elfobj/src/elf_link.c`, lines 113-160
- **Issue:** Group section's symtab link re-used without re-validation in linking context. If object was modified between parse and link, stale pointers possible.

---

## LOW Findings

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

1. **Immediate:** Audit all strtab access for `safe_str()` usage (#1).
2. **Immediate:** Ensure symbol parsing is atomic/rollback-safe on failure (#3).
3. **Short-term:** Strengthen section layout overlap checks for NOBITS memory ranges (#5).
4. **Short-term:** Add bounds assertions around write-path symbol table emission (#10).
5. **Short-term:** Re-validate group signature dependencies at final link use sites (#12).
6. **Medium-term:** Validate section alignment values and architecture relocation policy edge-cases (#15, #16).
7. **Medium-term:** Add cap for diagnostic buffer growth and cross-check version indexes (#17, #18).
8. **Testing:** Run existing fuzz harnesses with extended corpus; add relocation-focused fuzzer.
