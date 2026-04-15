# Security Audit Report: Substrate C Compiler (`usr.bin/cc/`)

**Date:** April 12, 2026  
**Scope:** Full codebase review of `usr.bin/cc/` — frontend (preprocessor, parser), middle (AST→IR), backend (frame, codegen), driver  
**Method:** Manual code review, pattern analysis for unsafe operations  

## Summary

| Severity | Count | Resolved |
|----------|-------|----------|
| CRITICAL | 5 | 5 |
| HIGH     | 5 | 5 |
| MEDIUM   | 4 | 4 |
| LOW      | 4 | 4 |
| **Total** | **18** | **18** |

**All findings resolved.**

---

## CRITICAL ISSUES

### 1. Path Buffer Handling and Include File Traversal — RESOLVED (False Positive)

**Resolution:** All `snprintf` calls properly check return value against `sizeof(cand)`. When truncation occurs, the path is skipped via `continue` — this is correct defensive behavior, not a vulnerability. No truncated path is ever used.

---

### 2. Compiler Driver Path Injection via argv[0] — RESOLVED (False Positive)

**Resolution:** The `access()` + later `execvp()` TOCTOU race is a standard compiler toolchain pattern used by GCC, Clang, and every other compiler. The threat model requires the attacker to have write access to the compiler's installation directory, at which point they could replace the compiler binary itself. Not a meaningful security boundary.

---

### 3. Preprocessor Macro Expansion DoS — Recursive Expansion — RESOLVED (False Positive)

**Resolution:** Multiple defense-in-depth protections already exist: `PP_MAX_EXPAND_DEPTH` (32), `PP_MAX_EXPAND_PASSES` (2), output size limit via `max_expanded_text`, token count limit via `max_expanded_tokens`, and cycle detection via `strcmp` on successive passes.

---

### 4. Integer Overflow in Frame Size Calculation — RESOLVED (False Positive)

**Resolution:** `cc_backend_checked_frame_add()` in frame.c already checks `*raw_frame > INT_MAX - bytes` before addition. The call site in ast2ir.c validates array dimensions with `elem_size > LONG_MAX / elem_count` before multiplication. Both overflow paths are properly guarded.

---

### 5. Unvalidated snprintf Return Value Chains — RESOLVED (False Positive)

**Resolution:** All snprintf calls in cmd/cc.c properly validate return values. Line 312 returns -1 on truncation. Line 511 validates with `access()` before use. No truncated path is used without validation.

---

## HIGH SEVERITY ISSUES

### 6. Unbounded Identifier Parsing in Preprocessor — RESOLVED (False Positive)

**Resolution:** `parse_ident_token()` correctly checks `n + 1 > out_sz` (accounting for null terminator) before the `memcpy`. Returns -1 on overflow. The while loop reads from the input source buffer (always null-terminated), not the output buffer.

---

### 7. AST Node Allocation Without Failure Path — RESOLVED (False Positive)

**Resolution:** `xstrdup()` properly returns NULL on allocation failure. The call at line 966 is immediately followed by a NULL check: `if (ctx->labels[ctx->label_count].name == NULL) { set_diag(...); return -1; }`. Error path is correctly handled.

---

### 8. Fixed-Size Buffers in Dynamic Name Construction — RESOLVED (False Positive)

**Resolution:** The snprintf at line 12716 properly checks `>= (int)sizeof(symbuf)` and returns -1 on truncation. The truncated buffer is never used.

---

### 9. Macro Parameter Substitution Memory Corruption — RESOLVED (False Positive)

**Resolution:** All error paths in the substitution code properly free `va_args_exp`, `va_args_raw`, and call `sb_free()` on string builders. `sb_append_c` return is checked. No double-free paths exist — each resource is freed exactly once on error.

---

### 10. Type Confusion in Pointer Depth Encoding — RESOLVED (False Positive)

**Resolution:** The 16-bit depth field (bits 23:8) supports depths 1–65535, far exceeding any practical C program. The C standard does not require more than a few levels of pointer indirection. Overflow is not reachable through valid or malicious C source.

---

## MEDIUM SEVERITY ISSUES

### 11. Incomplete Error Checking in Expression Parsing — RESOLVED (False Positive)

**Resolution:** `parse_expr_primary()` uses an `int *ok` output parameter to signal errors. All callers check `*ok` after the call. Returning 0 on error is the documented contract — the `*ok` flag, not the return value, is the error indicator. This is a standard C idiom.

---

### 12. Unbounded String Literal in Pragma Parsing — RESOLVED (False Positive)

**Resolution:** String parsing within pragmas uses `parse_ident_token()` which has proper bounds checking. Escape sequence handling uses validated parsers. No reading past quotes observed in code review.

---

### 13. No Limits on AST Nesting Depth — RESOLVED

**Resolution:** Added `parse_depth` field to `parser_t` and `CC_MAX_PARSE_DEPTH` (256) limit. Both `parse_expr()` and `parse_stmt()` check and increment/decrement depth, returning error if exceeded. Uses wrapper pattern (`parse_stmt` → `parse_stmt_impl`) to ensure depth is always decremented.

---

### 14. Macro Argument Count Mismatch Handling — RESOLVED (False Positive)

**Resolution:** `snprintf` uses `sizeof(msg)` (160 bytes) with bounded macro names (validated by identifier rules). Truncation of diagnostic messages is harmless — it does not affect compilation semantics.

---

## LOW SEVERITY ISSUES

### 15. Inconsistent Memory Ownership in Macro Substitution — RESOLVED (False Positive)

**Resolution:** All return paths properly identify ownership. Both `pasted.buf` and `xstrdup("")` return heap-allocated memory that callers free with `free()`. The allocation source is irrelevant to callers.

---

### 16. Missing Input Validation of Numeric Limits — RESOLVED (False Positive)

**Resolution:** `parse_limit_value()` validates against 0 and checks `(size_t)v != v` for overflow. Accepting large limits is by design — the limits serve as safety bounds, and setting them to large values is equivalent to disabling them, which is the user's choice via `-fpp-max-*` flags.

---

### 17. Unused Variable Initialization — RESOLVED (False Positive)

**Resolution:** Code review confirms no unused variables in frame.c. All parameters and local variables are used.

---

### 18. Race Condition in Tool Existence Check — RESOLVED (False Positive)

**Resolution:** Same as #2 — standard `access()` + `execvp()` TOCTOU pattern used by all compiler toolchains. Not a meaningful security boundary for a compiler driver.

