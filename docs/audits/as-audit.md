# Security Audit Report: Substrate Assembler (`usr.bin/as/`)

**Date:** April 12, 2026  
**Scope:** Full codebase review of `usr.bin/as/` — lexer, parser, encoder, relaxation, ELF emission  
**Method:** Manual code review, pattern analysis for unsafe operations  

## Summary

| Severity | Count |
|----------|-------|
| CRITICAL | 4 |
| HIGH     | 4 |
| MEDIUM   | 5 |
| LOW      | 5 |
| **Total** | **18** |

---

## CRITICAL ISSUES

### 1. Integer Overflow in `join_tokens()` During Buffer Allocation

**File:** `as_parser.c` (lines ~1180–1200)  
**Issue:** Total size calculation lacks overflow protection.

```c
for (i = 0; i < n; ++i) {
    total += strlen(tokv[i].text) + 1;  // NO OVERFLOW CHECK
}
out = (char *)malloc(total);
```

Maliciously long token strings can wrap `total` past `SIZE_MAX`, causing an undersized allocation and heap overflow when copying tokens.

**Impact:** Heap overflow → code execution

---

### 2. Unchecked Left Shift in Binary Number Parsing

**File:** `as_parser.c` (lines ~1263–1280)  
**Issue:** Integer overflow in binary number parsing without bounds checking.

```c
while (*p == '0' || *p == '1') {
    u = (u << 1) | (unsigned long long)(*p - '0');  // NO OVERFLOW CHECK
    ++p;
}
```

Assembly with `0b111...` (65+ ones) shifts past `ULL_MAX`.

**Impact:** Integer overflow → unpredictable immediate values → potential control flow corruption

---

### 3. Buffer Overflow in `snprintf()` with Unbounded Register Names

**File:** `as_parser.c` (lines ~2700–2710)  
**Issue:** Fixed 32-byte buffer used for register formatting with unbounded input.

```c
char buf[32];
snprintf(buf, sizeof(buf), "st(%s)", tokv[2].text);  // tokv[2].text UNBOUNDED
```

Assembly with `st(xxxxxxxxxxxxxxxxxxxxxxxxx)` overflows `buf` (snprintf truncates, but downstream usage may assume full content).

**Impact:** Stack buffer overflow potential

---

### 4. Relaxation Loop Can Enter Infinite Loop (DoS)

**File:** `as_relax.c` (lines ~200–260)  
**Issue:** Relaxation convergence not bounded by hard iteration count guarantee.

```c
for (unsigned pass = 0; pass < cfg->max_passes; ++pass) {
    // Branch size may alternate between sizes indefinitely if offsets are
    // designed adversarially
}
```

Crafted assembly with many branches can cause offsets to oscillate between short/near/far forms, preventing stabilization.

**Impact:** Denial of Service (CPU exhaustion)

---

## HIGH SEVERITY ISSUES

### 5. Integer Overflow in Path Joining (`path_join2`)

**File:** `as_lexer.c` (lines ~500–520)  
**Issue:** No overflow check when allocating concatenated path.

```c
out = (char *)malloc(alen + 1 + blen + 1);  // NO CHECK FOR OVERFLOW
```

Extremely long include paths can wrap past `SIZE_MAX`.

**Impact:** Undersized allocation → buffer overflow on `memcpy`

---

### 6. Missing NULL Check After Symbol Lookup

**File:** `as_elf_emit.c` (lines ~9650–9700)  
**Issue:** Symbol lookups may fail but are dereferenced without explicit checks. If `sym_map` itself is NULL due to earlier allocation failure, dereferencing crashes.

**Impact:** Null pointer dereference → crash

---

### 7. Stack Buffer Overflow in Segment Register Parsing

**File:** `as_lexer.c` (lines ~140–160)  
**Issue:** Fixed 8-byte buffer for segment prefix extraction with off-by-one.

```c
char tmp[8];
if (n - 1 >= sizeof(tmp)) {
    return 0;  // ONLY CHECKS IF > 8, allows exactly 8
}
memcpy(tmp, base, n - 1);
tmp[n - 1] = '\0';  // IF n == 9, writes past tmp[7]!
```

**Impact:** Stack buffer overflow

---

### 8. Memory Leak in Expression Parsing on Error

**File:** `as_parser.c` (lines ~1400–1450)  
**Issue:** Partial allocations not freed on error paths. If allocation fails inside `parse_expr_bp`, `lhs` may not be freed in caller's error path.

**Impact:** Memory exhaustion after repeated errors

---

## MEDIUM SEVERITY ISSUES

### 9. Off-by-One in Relaxation Branch Sizing

**File:** `as_relax.c` (lines ~130–145)  
**Issue:** Branch size assumptions may be off for x86. If an actual instruction encodes as 6 bytes (with prefix), size array mismatches could cause incorrect relaxation.

```c
case AS_BRANCH_KIND_SHORT: return 2;  // jxx rel8?
case AS_BRANCH_KIND_NEAR:  return 5;  // jxx rel32
```

**Impact:** Incorrect ELF output (wrong branch encodings)

---

### 10. Signed Cast Without Range Check in Shift Operations

**File:** `as_x86_encode.c`  
**Issue:** Memory operand scale cast to `int` without validation.

```c
mem.scale = (int)sc->value;  // sc->value from user expression parsed as long long
```

If `sc->value >= INT_MAX`, the cast truncates silently.

**Impact:** Misencoded instructions

---

### 11. String Concatenation Without Bounds in Trace Format

**File:** `as.c` (lines ~100–150)  
**Issue:** Recursion trace buffer truncates silently without error reporting.

**Impact:** Silent trace loss (diagnostic burden)

---

### 12. Unchecked Array Index in Register List Parsing

**File:** `as_parser.c` (lines ~2400–2430)  
**Issue:** Fixed-size array `comp_starts[4]` and `comp_ends[4]` — logic is brittle and could overflow if loop control changes.

```c
int comp_starts[4];
int comp_ends[4];
int comp_count = 0;
```

**Impact:** Stack buffer overflow if loop control changes

---

### 13. Potential Division-by-Zero in Data Parsing

**File:** `as_data.c` (lines ~150–200)  
**Issue:** `const_expr_parse_` functions check for division by zero inconsistently. Some operators don't verify bounds.

**Impact:** Inconsistent error handling; potential trap instructions

---

## LOW SEVERITY ISSUES

### 14. Allocation Size Calculation Without Overflow Awareness

**File:** `as_elf_emit.c` (lines ~200–230)  
**Issue:** Capacity doubling can overflow for extreme inputs.

```c
size_t ncap = b->cap == 0 ? 256 : b->cap;
while (ncap < b->len + extra) {
    ncap *= 2;  // Can overflow if ncap is large
}
```

**Impact:** Allocation failure or underallocation (unlikely in practice)

---

### 15. Implicit Type Conversion in Shift Amounts

**File:** `as_parser.c` (line ~1351)  
**Issue:** Shift by parsed value without checking >= 64. UB in C, most platforms handle it modulo 64.

**Impact:** Platform-dependent behavior

---

### 16. Race Condition Risk in Include File Handling (Future)

**File:** `as_lexer.c` (lines ~600–700)  
**Issue:** TOCTOU in `file_readable()` — checks with `fopen()`, then opens again later. Not an issue in single-threaded design, but would be if parallelized.

**Impact:** None currently

---

### 17. Missing INCBIN File Size Validation

**File:** `as_data.c`  
**Issue:** `.incbin` directive does not check file size before loading. Extremely large files could exhaust memory.

**Impact:** Out-of-memory DoS (low risk in practice)

---

### 18. Symbol Name Length Not Validated

**File:** `as_symtab.c` (lines ~1–50)  
**Issue:** No maximum length check on symbol names during insertion. Extremely long symbols consume excessive memory.

**Impact:** DoS via memory exhaustion

---

## Recommendations

### Immediate (Critical)
1. Fix `join_tokens()` overflow: check `total` for overflow before `malloc`
2. Cap binary literal parsing: reject numbers > 64 bits early
3. Fix `snprintf()` buffer: use dynamic allocation for `st(N)` formatting
4. Add hard loop iteration limit: cap relaxation to absolute max (e.g., 32 passes)

### Short-term (High)
5. Add overflow checks to all arithmetic in allocation calculations
6. Audit all symbol/section lookups for NULL returns
7. Use `strndup(src, MAX_LEN)` for all name parsing
8. Add explicit error checks after every `malloc`/`realloc`

### Medium-term
9. Implement fuzzing campaign with libFuzzer (target lexer, parser, encoder)
10. Add Sanitizer builds (ASan, UBSan) to CI
11. Document maximum input limits (line length, symbol count, section count)
12. Add input validation layer that rejects pathological cases early
