# libdemangle Codebase Audit Report

**Scope:** `usr.lib/demangle/`
**Date:** April 12, 2026

---

## Executive Summary

| Severity | Count | Key Areas |
|----------|-------|-----------|
| **CRITICAL** | 0 | *(all resolved)* |
| **HIGH** | 3 | Substitution array limits, template depth, D language recursion |
| **MEDIUM** | 2 | Punycode overflow complexity, legacy format string logic |
| **LOW** | 2 | Buffer arithmetic fragility, stack buffer sizing |
| **TOTAL** | **7** | |

---

## CRITICAL Findings

*(All resolved)*

---

## HIGH Findings

### 4. Fixed Substitution Array Limit (256 entries)

- **File:** `usr.lib/demangle/itanium.c`
- **Issue:** Itanium ABI substitutions stored in a fixed 256-element array. Deeply-templated C++ names (e.g., Boost, Eigen) can exceed this, causing silent truncation or out-of-bounds access.
- **Fix:** Dynamic allocation or a higher limit with bounds checking.

### 5. Template Stack Depth (64 levels)

- **File:** `usr.lib/demangle/itanium.c`
- **Issue:** Only 64 template nesting levels allowed. Deeply nested C++ template metaprogramming code can exceed this.
- **Fix:** Document the limit; consider dynamic growth.

### 6. D Language Template Recursion Path

- **File:** `usr.lib/demangle/dlang.c`
- **Issue:** Template instance parsing can reach recursion limits without consistent depth tracking across all entry points.
- **Fix:** Audit all recursive call sites in D parser for depth checks.

---

## MEDIUM Findings

### 7. Punycode Integer Overflow Complexity

- **File:** `usr.lib/demangle/rust.c`, lines 667-682
- **Issue:** Overflow checks in Rust v0 punycode decoding are mathematically correct but brittle:
  ```c
  if (digit > (UINT32_MAX - i) / w) { ... }
  ```
  The relationship between `digit`, `i`, and `w` is non-obvious and easy to break during maintenance.
- **Fix:** Add comments explaining the invariants; consider using a `checked_mul()` helper.

### 8. Legacy Format Token Handling

- **File:** `usr.lib/demangle/itanium.c`
- **Issue:** Complex token-based formatting logic is correct but difficult to maintain. State machine transitions are implicit.
- **Fix:** Add state machine documentation or refactor to explicit states.

---

## LOW Findings

### 9. Buffer Arithmetic Fragility

- **File:** `usr.lib/demangle/buffer.c`, lines 9-46
- **Note:** The buffer growth code is actually well-protected (overflow checks on doubling, allocation failure check, size_t overflow guard). However, the pattern of `buf->len + extra + 1u` appearing in multiple places is fragile if one site forgets the `+1u`.
- **Fix:** Extract to a helper or macro.

### 10. Stack Buffer Usage in Formatters

- **File:** `usr.lib/demangle/dlang.c`, `itanium.c`
- **Issue:** Several functions use 32-byte or 64-byte stack buffers for number formatting. On memory-constrained targets these are fine, but the pattern requires careful review (see finding #3).

---

## Positive Notes

- `buffer.c` has solid overflow protection: doubling overflow check, size_t overflow guard, allocation failure check.
- Recursion limit (`DM_RECURSION_DEFAULT_LIMIT = 256`) exists — the issue is inconsistent enforcement.
- Clean separation between Itanium, Rust, and D parsers.

---

## Recommendations

1. **Immediate:** Add `parser_enter()` calls to `parse_nested_name()` and `parse_unnamed_type_name()` (#1, #2).
2. **Immediate:** Fix snprintf return value clamping in dlang.c (#3).
3. **Short-term:** Audit all recursive call sites across all three parsers for depth enforcement.
4. **Short-term:** Replace fixed substitution/template arrays with dynamic allocation.
5. **Testing:** Fuzz with AFL/libFuzzer using deeply nested names, boundary values, and malformed input.
