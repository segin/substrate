# libelfobj Codebase Audit Report

**Scope:** `usr.lib/elfobj/`
**Date:** April 12, 2026

---

## Executive Summary

| Severity | Count | Key Areas |
|----------|-------|-----------|
| **CRITICAL** | 1 | section link validation |
| **HIGH** | 0 | *(all resolved)* |
| **MEDIUM** | 2 | NULL dereference, group signature |
| **LOW** | 1 | version index |
| **TOTAL** | **4** | |

This library parses untrusted ELF files supplied to the assembler/linker. All input data must be treated as adversarial.

---

## CRITICAL Findings

### 3. Section sh_link Validation — Partial State Corruption

- **File:** `usr.lib/elfobj/src/elf_read.c`, lines 393-410
- **Function:** `parse_symbols()`
- **Issue:** When `sec->link >= obj->section_count`, the function returns `ELF_ERR_FORMAT`. But earlier iterations of the loop may have already pushed symbols into the object. The object is left in an inconsistent state — some symbol tables loaded, others not.
- **Impact:** Callers that check for errors after `parse_symbols()` may still see partial data, leading to incorrect link output.
- **Fix:** Either validate all sections upfront before loading any, or roll back on error.

---

## HIGH Findings

*(All resolved)*

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
