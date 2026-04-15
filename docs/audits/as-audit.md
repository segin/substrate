# Security Audit Report: Substrate Assembler (`usr.bin/as/`)

**Date:** April 12, 2026  
**Scope:** Full codebase review of `usr.bin/as/` — lexer, parser, encoder, relaxation, ELF emission  
**Method:** Manual code review, pattern analysis for unsafe operations  

## Summary

| Severity | Count | Resolved |
|----------|-------|----------|
| CRITICAL | 4 | 4 |
| HIGH     | 4 | 4 |
| MEDIUM   | 5 | 5 |
| LOW      | 5 | 5 |
| **Total** | **18** | **18** |

**All findings resolved.**

---

## CRITICAL ISSUES

### 1. Integer Overflow in `join_tokens()` During Buffer Allocation — RESOLVED

**Resolution:** Added overflow check: `if (total > SIZE_MAX - tlen) return NULL` before each accumulation in the token length summation loop.

---

### 2. Unchecked Left Shift in Binary Number Parsing — RESOLVED

**Resolution:** Added overflow guard `if (v > (UINT64_MAX - d) / base) return -1` before the `v = v * base + d` operation in `expr_parse_number()`. Applies to all bases (2, 8, 10, 16).

---

### 3. Buffer Overflow in `snprintf()` with Unbounded Register Names — RESOLVED (False Positive)

**Resolution:** `snprintf()` safely truncates output to buffer size and always null-terminates. Truncation of a register name in a formatting context does not lead to a buffer overflow — it produces a diagnostic or lookup failure, which is handled.

---

### 4. Relaxation Loop Can Enter Infinite Loop (DoS) — RESOLVED (False Positive)

**Resolution:** The relaxation loop is bounded by `cfg->max_passes` (default 16): `for (pass = 1; pass <= max_passes; ++pass)`. This provides a hard upper bound. Oscillation is limited to at most 16 passes, not infinite.

---

## HIGH SEVERITY ISSUES

### 5. Integer Overflow in Path Joining (`path_join2`) — RESOLVED

**Resolution:** Added overflow check `if (alen > SIZE_MAX - blen - 2) return NULL` before the allocation size computation.

---

### 6. Missing NULL Check After Symbol Lookup — RESOLVED (False Positive)

**Resolution:** Symbol lookups in `as_elf_emit.c` are guarded by `if (sym != NULL)` checks before dereferencing. The return value from `find_symbol()` is properly validated.

---

### 7. Stack Buffer Overflow in Segment Register Parsing — RESOLVED (False Positive)

**Resolution:** The bounds check `if (n - 1 >= sizeof(tmp))` correctly rejects `n >= 9` (i.e., `n - 1 >= 8`). Maximum valid `n` is 8, so `tmp[n-1]` writes at most to index 7. The `>=` comparison is correct — no off-by-one.

---

### 8. Memory Leak in Expression Parsing on Error — RESOLVED (False Positive)

**Resolution:** Code review confirms `free_expr()` and direct `free()` calls properly clean up all allocated memory on error paths in the expression parser.

---

## MEDIUM SEVERITY ISSUES

### 9. Off-by-One in Relaxation Branch Sizing — RESOLVED (False Positive)

**Resolution:** Branch sizes (SHORT=2, NEAR=5, FAR=6) match standard x86 encoding conventions. The relaxation algorithm uses these as hints for convergence; slight oversizing is safe behavior.

---

### 10. Signed Cast Without Range Check in Shift Operations — RESOLVED (False Positive)

**Resolution:** Scale values are validated to be exactly 1, 2, 4, or 8 before the cast: `if (sc->value != 1 && sc->value != 2 && sc->value != 4 && sc->value != 8) return -1`. The cast is safe.

---

### 11. String Concatenation Without Bounds in Trace Format — RESOLVED (False Positive)

**Resolution:** The trace buffer uses `snprintf` with `sizeof(trace_buf)` and detects truncation. Trace strings are diagnostic — truncation is harmless and handled.

---

### 12. Unchecked Array Index in Register List Parsing — RESOLVED (False Positive)

**Resolution:** `comp_count < 3` guard ensures `comp_count` never exceeds 3, so array indices 0–3 stay in bounds for `comp_starts[4]` and `comp_ends[4]`.

---

### 13. Potential Division-by-Zero in Data Parsing — RESOLVED (False Positive)

**Resolution:** Division and modulo operations check `if (r == 0) return -1` before the operation. All division operators are properly guarded.

---

## LOW SEVERITY ISSUES

### 14. Allocation Size Calculation Without Overflow Awareness — RESOLVED

**Resolution:** Added overflow guard in `bytebuf_reserve()`: `if (ncap > SIZE_MAX / 2) return -1` before `ncap *= 2`. Also added overflow check in `as_symtab.c` symbol table growth: `if (ncap < ctx->tab->cap || ncap > SIZE_MAX / sizeof(*next)) return NULL`.

---

### 15. Implicit Type Conversion in Shift Amounts — RESOLVED (False Positive)

**Resolution:** Shift amounts are masked with `r & 63`, preventing undefined behavior from shifts >= 64. This is equivalent to x86 hardware behavior.

---

### 16. Race Condition Risk in Include File Handling (Future) — RESOLVED (False Positive)

**Resolution:** Same TOCTOU pattern as all compilers/assemblers: `file_readable()` is just a path resolution hint; the actual `fopen()` happens later and will fail gracefully if the file is gone. Single-threaded design makes exploitation impractical. Not a meaningful security boundary.

---

### 17. Missing INCBIN File Size Validation — RESOLVED (False Positive)

**Resolution:** `.incbin` file handling reads the file during emission; if the file is too large, `malloc`/`fread` failures are handled. Size validation during parsing is not needed — runtime failure is the correct behavior (same as GNU as).

---

### 18. Symbol Name Length Not Validated — RESOLVED (False Positive)

**Resolution:** Symbol names are allocated with `xstrdup()` which uses `malloc(strlen(name) + 1)`. There is no fixed-size buffer to overflow. Memory exhaustion from extremely long symbols is a system-level limit, not a program vulnerability.
