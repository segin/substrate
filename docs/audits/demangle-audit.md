# libdemangle Codebase Audit Report

**Scope:** `usr.lib/demangle/`
**Date:** April 12, 2026

---

## Executive Summary

| Severity | Count | Key Areas |
|----------|-------|-----------|
| **CRITICAL** | 3 | Stack exhaustion via recursion bypass, snprintf buffer over-read |
| **HIGH** | 3 | Substitution array limits, template depth, D language recursion |
| **MEDIUM** | 2 | Punycode overflow complexity, legacy format string logic |
| **LOW** | 2 | Buffer arithmetic fragility, stack buffer sizing |
| **TOTAL** | **10** | |

---

## CRITICAL Findings

### 1. Stack Exhaustion via Lambda Recursion — No Depth Check

- **File:** `usr.lib/demangle/itanium.c`, lines 704-730
- **Issue:** `parse_unnamed_type_name()` calls `parse_type()` in a loop without calling `parser_enter()` for recursion depth tracking. Since `parse_type()` can call back into name parsing, this creates unbounded mutual recursion.
- **Code:**
  ```c
  while (p->cur[0] != '\0' && p->cur[0] != 'E') {
      if (parse_type(p) != 0) {   // recursive, no depth check
          return -1;
      }
  }
  ```
- **Impact:** Crafted mangled name with deeply nested lambdas overflows the stack. DoS on any tool using libdemangle (e.g., profiler, debugger, `nm`).
- **Fix:** Add `parser_enter(p)` / `parser_leave(p)` around the `parse_type()` call.

### 2. Recursion Bypass in `parse_nested_name()`

- **File:** `usr.lib/demangle/itanium.c`, lines 834-880
- **Issue:** `parse_nested_name()` never calls `parser_enter()`. The recursion path `parse_nested_name()` → `parse_name_component()` → name parsing → `parse_nested_name()` bypasses the 256-deep limit entirely.
- **Impact:** Same as above — stack exhaustion via crafted input.
- **Fix:** Add `parser_enter(p)` at function entry.

### 3. snprintf Buffer Over-Read in D Language Parser

- **File:** `usr.lib/demangle/dlang.c`, lines 1030-1033
- **Issue:** `snprintf()` into a 32-byte stack buffer returns the number of characters that *would* have been written. The return value is then passed to `dlang_buf_append()` as a length, which reads beyond the 32-byte buffer when the formatted number exceeds 31 characters.
- **Code:**
  ```c
  char numbuf[32];
  int numlen = snprintf(numbuf, sizeof(numbuf), "%zu", n);
  // numlen can be > 32 if n is very large
  dlang_buf_append(&p->out, numbuf, (size_t)numlen);  // reads past buffer
  ```
- **Impact:** Information disclosure — leaks stack contents into demangled output.
- **Fix:** Clamp `numlen` to `sizeof(numbuf) - 1` before passing to append.

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
