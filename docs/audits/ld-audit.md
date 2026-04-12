# Security Audit Report: Substrate Linker (`usr.bin/ld/`)

**Date:** April 12, 2026  
**Scope:** Full codebase review of `usr.bin/ld/` (~10,500 lines) — ELF linking, relocation, linker scripts, archives, dynamic linking  
**Method:** Manual code review, pattern analysis for unsafe operations  

## Summary

| Severity | Count |
|----------|-------|
| CRITICAL | 4 |
| HIGH     | 6 |
| MEDIUM   | 5 |
| LOW      | 2 |
| **Total** | **17** |

---

## CRITICAL ISSUES

### 1. Integer Overflow in Virtual Address Calculations

**File:** `ld.c` (lines ~1145–1160, ~8325–8355)  
**Issue:** While checked arithmetic functions exist (`add_u64_checked`, `align_up_u64_checked`), there are unchecked alignment operations that can overflow.

Malicious ELF objects with sections having large sizes or excessive alignment requirements (up to 2^64) can cause wrapping in virtual address assignment.

**Impact:** Out-of-bounds memory access, section metadata corruption, crash or code execution

---

### 2. Relocation Overflow Detection — Missing Boundary Checks

**File:** `ld.c` (lines ~1295–1320, ~5900–10000+)  
**Issue:** The linker does not validate relocation targets against address space boundaries during relocation processing.

```c
// R_X86_64_64: write_u64_endian(target_loc, sym_addr + addend);
// No check that sym_addr + addend doesn't wrap or exceed 64-bit space
```

Malicious objects with relocations targeting symbols beyond boundaries or addends causing signed/unsigned wraparound.

**Impact:** Undetected memory corruption, silent address truncation, runtime loader crashes

---

### 3. Buffer Overflow in Linker Script Expression Parsing

**File:** `ld.c` (lines ~1050–1100, specifically ~1348)  
**Issue:** Linker script lexer allocates token text without enforced size limit.

```c
static int lds_lex_push_text(lds_tok_t *tok, const unsigned char *start, size_t n) {
    tok->text = (char *)malloc(n + 1);
    if (tok->text == NULL) return -1;
    memcpy(tok->text, start, n);   // No bounds check on 'n'
    tok->text[n] = '\0';
    return 0;
}
```

Malicious linker scripts with identifiers of unbounded length, very long string escape sequences, or numeric literals with thousands of digits.

**Impact:** Heap buffer overflow, heap metadata corruption, arbitrary code execution

---

### 4. Symbol Name Handling — Unbounded String Operations

**File:** `ld.c` (lines ~2150–2200, ~4950)  
**Issue:** `make_versioned_symbol()` concatenates strings from potentially untrusted DSOs without bounds.

```c
sep_len = strlen(sep);
ver_len = strlen(ver_name);    // ver_name from malicious DSO — no bounds check
out = (char *)malloc(base_len + sep_len + ver_len + 1);
// If ver_name has strlen > UINT_MAX/2, addition can overflow
```

**Impact:** Integer overflow in allocation size, out-of-bounds write, heap corruption

---

## HIGH SEVERITY ISSUES

### 5. Archive Member Extraction Without Bounds Checks

**File:** `ld.c` (lines ~3200–3350)  
**Issue:** Member size field parsed from archive header but no check that `off + msize` doesn't exceed buffer size before reading.

```c
if (parse_u64_dec((const char *)hdr + 48, 10, &msize) != 0) break;
off += 60;
if (off > sz) break;
mdata = buf + off;
mname = decode_ar_name((const char *)hdr, mdata, msize, strtab, strtab_sz, &name_extra);
// NO CHECK that (off + msize) doesn't exceed buf size
```

Malicious `.a` archives with member size claiming size > remaining archive data.

**Impact:** Out-of-bounds read, information disclosure, DoS

---

### 6. Thin Archive Path Resolution — Directory Traversal Risk

**File:** `ld.c` (lines ~3650–3700)  
**Issue:** `resolve_thin_member_path()` uses `realpath()` which can fail silently. If `archive_real` is a symlink to `/tmp`, the logical containment check is bypassed.

Thin archives that reference `../../../etc/passwd` or exploit symbolic link races during `realpath()` resolution.

**Impact:** Arbitrary file read, potential code injection from unexpected locations

---

### 7. Relocation Addition Overflow in Dynamic Linking

**File:** `ld.c` (lines ~5800–5900, ~4410–4430)  
**Issue:** Negative addend overflow logic in `resolve_runtime_relative_addend()` is incorrect.

```c
} else {
    uint64_t neg = (uint64_t)(-(addend + 1)) + 1u;
    if (sym_addr < neg) {  // WRONG for signed negative addends
        return -1;
    }
}
```

The subtraction overflow check does not correctly handle all negative addend values.

**Impact:** Silent address underflow, relocation corruption

---

### 8. Dynamic String Table Overflow

**File:** `ld.c` (lines ~2680–2720, ~4980–5000)  
**Issue:** `dynstr_append_cstr()` checks offset against `UINT32_MAX` but does not bound `strlen(name)` from attacker-controlled symbols.

```c
off = *len;
if (off > UINT32_MAX) return -1;  // Check exists
n = strlen(name) + 1;             // NO BOUNDS CHECK on strlen output
```

Massive `.dynsym` with thousands of versioned symbols with long names.

**Impact:** Unbounded memory allocation, OOM DoS

---

### 9. Linker Script Include Recursion Without Effective Limit Check

**File:** `ld.c` (lines ~1600–1650, ~1743)  
**Issue:** Depth check occurs after the recursive call is made, not before.

```c
if (depth > LD_MAX_SCRIPT_INCLUDE_DEPTH) {  // LD_MAX_SCRIPT_INCLUDE_DEPTH = 64
    return -1;
}
// BUT: 64 levels of recursion can exhaust stack before check triggers
```

Linker scripts with 63 levels of `INCLUDE` directives.

**Impact:** Stack overflow, DoS

---

### 10. Integer Overflow in Symbol Version Index Assignment

**File:** `ld.c` (lines ~5100–5150)  
**Issue:** Version indexes assigned as `uint16_t` without overflow check.

```c
write_u16_endian(buf + off + 4, endian, plan->defs[i].index);
// If >65536 versioned symbols, index field truncates silently
```

**Impact:** Version table corruption, symbol resolution failures at runtime

---

## MEDIUM SEVERITY ISSUES

### 11. Section Name Comparison Without Null Terminator Guarantee

**File:** `ld.c` (lines ~8950–9000)  
**Issue:** Multiple `strcmp()` calls on `elf_section_name()` without verifying null termination. If section data is corrupted, name may not be null-terminated.

**Impact:** Out-of-bounds read

---

### 12. Unchecked Archive Scan Pass Limit Bypass

**File:** `ld.c` (line ~3150)  
**Issue:** Archive scan loop can iterate `LD_MAX_ARCHIVE_SCAN_PASSES` (1024) times even for tiny archives if `pass_progress` is repeatedly set.

**Impact:** Quadratic time complexity, linker hangup on adversarial archives

---

### 13. Integer Overflow in Hash Table Calculation

**File:** `ld.c` (lines ~5200–5250)  
**Issue:** In `build_gnu_hash_section()` / `build_sysv_hash_section()`, if `dynsym_len` is crafted to be near `SIZE_MAX`, the bucket/chain calculation can wrap.

```c
// nsyms = dynsym_len / entsz;
// nbucket = nsyms - 1;
// buf = (uint8_t *)malloc((2 + nbucket + nchain) * 4);
```

**Impact:** Undersized hash table allocation, out-of-bounds write

---

### 14. Relocation Signature Comparison — Incorrect Symbol Matching

**File:** `ld.c` (lines ~8600–8650)  
**Issue:** In `sections_reloc_signature_equal()`, if both symbol names are NULL (undefined symbols), the comparison succeeds even if from different symbol tables.

**Impact:** Incorrect section folding in ICF mode, wrong symbol binding

---

### 15. Missing Validation of ELF Section Indices in Relocation

**File:** `ld.c` (lines ~7900–7950)  
**Issue:** `shndx` validated at some read points but not all. Different code paths may access section indices without bounds checking.

**Impact:** Potential out-of-bounds if validation is missed on an alternate path

---

## LOW SEVERITY ISSUES

### 16. Symbol Reference Tracking Without Cycle Detection

**File:** `ld.c` (lines ~3800–3850)  
**Issue:** Reference tracking uses linear search with `LD_MAX_TRACKED_SYMBOLS = 262144`. Repeated queries become O(n²).

**Impact:** Linker slowdown (unlikely to exceed limits in practice)

---

### 17. File Copy Without Size Verification

**File:** `ld.c` (lines ~7450–7480)  
**Issue:** For `--reproduce`, file is read and written without size sanity check. If a symlink changes between read and write, size mismatch occurs.

**Impact:** Reproducibility false positive, not a security issue

---

## Recommendations

### Immediate (Before Release)
1. Add relocation overflow detection with per-relocation bounds checks
2. Implement symbol name length limits (< 1MB per combined string)
3. Add maximum archive member size enforcement
4. Fix negative addend overflow logic in `resolve_runtime_relative_addend()`
5. Validate linker script token lengths before allocation

### Short-term
6. Add comprehensive integer overflow checks to all VA arithmetic
7. Implement thin archive path validation with race condition protection
8. Limit dynamic symbol table size and version count
9. Move depth check before recursive call in linker script include handling

### Long-term (Hardening)
10. Fuzz test linker with AFL/libFuzzer on archives, scripts, and ELF inputs
11. Add AddressSanitizer and UBSan builds to CI/CD
12. Document maximum input limits and enforce at input boundaries
