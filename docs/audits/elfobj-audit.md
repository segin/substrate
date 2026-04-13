# libelfobj Codebase Audit Report

**Scope:** `usr.lib/elfobj/`
**Date:** April 12, 2026

---

## Executive Summary

| Severity | Count | Key Areas |
|----------|-------|-----------|
| **CRITICAL** | 0 | *(all resolved)* |
| **HIGH** | 0 | *(all resolved)* |
| **MEDIUM** | 1 | group signature |
| **LOW** | 0 | *(all resolved)* |
| **TOTAL** | **1** | |

This library parses untrusted ELF files supplied to the assembler/linker. All input data must be treated as adversarial.

---

## CRITICAL Findings

*(All resolved)*

---

## HIGH Findings

*(All resolved)*

---

## MEDIUM Findings

### 12. Group Signature Name Extraction

- **File:** `usr.lib/elfobj/src/elf_link.c`, lines 113-160
- **Issue:** Group section's symtab link re-used without re-validation in linking context. If object was modified between parse and link, stale pointers possible.

---

## LOW Findings

*(All resolved)*

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
