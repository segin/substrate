# Security Audit Report: Substrate C Compiler (`usr.bin/cc/`)

**Date:** April 12, 2026  
**Scope:** Full codebase review of `usr.bin/cc/` — frontend (preprocessor, parser), middle (AST→IR), backend (frame, codegen), driver  
**Method:** Manual code review, pattern analysis for unsafe operations  

## Summary

| Severity | Count |
|----------|-------|
| CRITICAL | 5 |
| HIGH     | 5 |
| MEDIUM   | 4 |
| LOW      | 4 |
| **Total** | **18** |

---

## CRITICAL ISSUES

### 1. Path Buffer Handling and Include File Traversal

**File:** `frontend/preproc.c` (lines ~2334–2345, ~2370–2385, ~2939–2975)  
**Issue:** Multiple `snprintf` buffer operations with `PATH_MAX`-sized buffers fail silently by skipping processing rather than reporting.

```c
if (snprintf(cand, sizeof(cand), "%s/%s", st->user_include_paths.items[i], spec)
    >= (int)sizeof(cand)) {
    continue;  // Silently skips — may hide legitimate includes from verification
}
```

Directory traversal via `../` sequences in include specifications can get truncated but still represent valid (unintended) paths.

**Impact:** Directory traversal, potential inclusion of unintended files

---

### 2. Compiler Driver Path Injection via argv[0]

**File:** `cmd/cc.c` (lines ~1614–1620)  
**Issue:** Tool search path derived from `self` (`argv[0]`) combined with `sibling_dir`/`tool_name` without canonicalization.

```c
if (snprintf(buf, bufsz, "%.*s/../%s/%s", (int)dirlen, self, sibling_dir, tool_name) > 0 &&
    access(buf, X_OK) == 0) {
    return find_tool_in(buf, ..., tool_name);
}
```

Attacker controls compilation via symlink `./cc` → `/malicious/path`; compiler searches for `as`, `ld` in attacker-controlled directories.

**Impact:** Arbitrary code execution during compilation (assembler/linker injection)

---

### 3. Preprocessor Macro Expansion DoS — Recursive Expansion

**File:** `frontend/preproc.c` (lines ~4400–4850, 17–24)  
**Issue:** Multiple expansion passes (`PP_MAX_EXPAND_PASSES=2`) with high depth (`PP_MAX_EXPAND_DEPTH=32`) and no cycle detection for user-defined macros. Limits can be overridden via `-fpp-max-*` flags.

```c
#define A B
#define B A(A(A(...)))  /* exponential expansion */
A   /* Triggers massive expansion, consuming CPU/memory */
```

**Impact:** Denial of Service (compiler hangs/OOM)

---

### 4. Integer Overflow in Frame Size Calculation

**File:** `backend/frame.c` (lines ~10–24), triggered from `middle/ast2ir.c` (line ~150+)  
**Issue:** `cc_backend_checked_frame_add()` checks for overflow, but the call site in `ast2ir.c` performs unvalidated arithmetic on array dimensions before calling.

```c
long array_size = array_len * element_size;  /* No overflow check */
cc_backend_checked_frame_add(&frame, array_size, ...);
```

Array declarations like `int arr[1000000][1000000]` cause multiplication overflow wrapping to a small positive value, bypassing frame size limits.

**Impact:** Stack frame miscalculation → stack smashing or undetected OOB

---

### 5. Unvalidated snprintf Return Value Chains

**File:** `cmd/cc.c` (lines ~312–320, ~511–520)  
**Issue:** Some snprintf calls check `> 0` but not `< PATH_MAX`, potentially using truncated paths.

```c
if (snprintf(path, sizeof(path), "%.*s/resource/include", (int)dirlen, self) > 0 &&
    path_exists(path)) {
    /* Path is used but upper bound check missing */
}
```

**Impact:** Truncated path usage → wrong include directories

---

## HIGH SEVERITY ISSUES

### 6. Unbounded Identifier Parsing in Preprocessor

**File:** `frontend/preproc.c` (lines ~2401–2410)  
**Issue:** Parser reads beyond `out_sz` bounds during loop, checking AFTER reading.

```c
static int parse_ident_token(const char **sp, char *out, size_t out_sz) {
    while (is_ident_char((unsigned char)p[n])) { n++; }  /* No maximum enforced */
    if (n == 0 || n + 1 > out_sz) { return -1; }
```

Pragma directives with overly long identifier names can read past buffer tail.

**Impact:** Buffer over-read

---

### 7. AST Node Allocation Without Failure Path

**File:** `middle/ast2ir.c` (line ~966)  
**Issue:** Many `malloc`/`xstrdup` calls used without NULL check before dereferencing. Inconsistent error handling means some codepaths use unvalidated memory.

**Impact:** NULL dereference crashes on OOM

---

### 8. Fixed-Size Buffers in Dynamic Name Construction

**File:** `middle/ast2ir.c` (lines ~1301, ~12716)  
**Issue:** Symbol names from parsed input are unbounded, but formatted into fixed buffers.

```c
if (snprintf(symbuf, sizeof(symbuf), "__cc_clit_%s_%zu", tu->globals[i].name, i)
    >= (int)sizeof(symbuf)) {
    return -1;
}
```

Very long global variable names cause truncation/symbol collision.

**Impact:** Symbol name truncation, potential collisions

---

### 9. Macro Parameter Substitution Memory Corruption

**File:** `frontend/preproc.c` (lines ~4650–4800)  
**Issue:** Complex token-pasting with multiple `sb_t` (string builder) operations. If `sb_append_c` fails mid-operation, partially initialized buffers may be freed twice or data corrupted.

```c
if (sb_append_c(&out, m->body[i]) != 0) {
    free(va_args_exp); free(va_args_raw);
    sb_free(&out); return NULL;  /* Cleanup inconsistent */
}
```

**Impact:** Double-free or use-after-free on allocation failure

---

### 10. Type Confusion in Pointer Depth Encoding

**File:** `include/cc_frontend.h` (lines ~158–185)  
**Issue:** Dynamic pointer type encoding assumes 16-bit depth field won't overflow.

```c
#define CC_TYPE_PTR_DYN_DEPTH_MASK 0x00FFFF00u
```

Crafted AST with pointer depth > 65535 causes wrapping in the 16-bit field.

**Impact:** Type confusion → incorrect memory layout assumptions in codegen

---

## MEDIUM SEVERITY ISSUES

### 11. Incomplete Error Checking in Expression Parsing

**File:** `frontend/preproc.c` (lines ~4200–4250)  
**Issue:** `parse_expr_primary` returns `pp_num_from_signed(0)` on error, but `0` is also a valid result. Caller cannot distinguish error from valid zero.

**Impact:** Malformed `#if` conditionals silently default to 0

---

### 12. Unbounded String Literal in Pragma Parsing

**File:** `frontend/preproc.c` (lines ~2518–2560)  
**Issue:** While loop bounds check exists, string escape sequences are not handled, could read past quote.

**Impact:** Buffer over-read on obfuscated input

---

### 13. No Limits on AST Nesting Depth

**File:** `frontend/parser.c`  
**Issue:** Parser recursively descends for nested expressions/declarations/statements without depth limit. Expression like `(((((...((x)...)))))` with 10,000+ levels causes stack overflow.

**Impact:** Denial of Service (compiler crash)

---

### 14. Macro Argument Count Mismatch Handling

**File:** `frontend/preproc.c` (lines ~4733–4736)  
**Issue:** Diagnostic message buffer (256 bytes default) used with macro names from parsed source that are not validated for length.

**Impact:** Diagnostic truncation, minor information loss

---

## LOW SEVERITY ISSUES

### 15. Inconsistent Memory Ownership in Macro Substitution

**File:** `frontend/preproc.c` (lines ~4666–4700)  
**Issue:** Function returns `malloc`'d `pasted.buf` on success but `xstrdup("")` on empty — inconsistent allocator use.

**Impact:** Caller confusion about `free()` semantics

---

### 16. Missing Input Validation of Numeric Limits

**File:** `frontend/preproc.c` (lines ~2476–2510)  
**Issue:** Limit parsing rejects 0 but doesn't validate that limits are reasonable (e.g., `max_include_depth = ULLONG_MAX`).

**Impact:** Minor DoS via extremely large limits

---

### 17. Unused Variable Initialization

**File:** `backend/frame.c` (lines ~19–23)  
**Issue:** `context` parameter may be unused in some codepaths of `checked_frame_add`.

**Impact:** Dead code / minor clarity issue

---

### 18. Race Condition in Tool Existence Check

**File:** `cmd/cc.c` (line ~1619)  
**Issue:** Tool existence verified with `access()` but tool could be deleted/replaced between check and `execvp()`.

**Impact:** Graceful failure, low risk

---

## Recommendations

### Immediate (Critical)
1. **Patch #2 (argv[0] injection):** Use `realpath()` on `argv[0]`; canonicalize before path construction; use absolute tool paths
2. **Patch #4 (integer overflow in frame size):** Add overflow checks in `ast2ir.c` before passing to frame functions
3. Audit all `snprintf` calls for silent truncation fallthrough (#1, #6, #8, #12)

### Short-term (High)
4. Tighten macro expansion limits (#3) and document why they exist
5. Add AST recursion depth limit (#13) to prevent stack exhaustion
6. Require NULL checks immediately after allocation; use single allocator strategy
7. Centralize `snprintf` wrapper to enforce bounds and fail-fast

### Medium-term
8. Implement fuzzing campaign targeting preprocessor, parser, and codegen
9. Add Sanitizer builds (ASan, UBSan) to CI
10. Document maximum input limits (nesting depth, symbol count, macro expansion)
