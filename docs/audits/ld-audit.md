# Security Audit Report: Substrate Linker (`usr.bin/ld/`)

**Date:** April 12, 2026  
**Scope:** Full codebase review of `usr.bin/ld/` (~10,500 lines) — ELF linking, relocation, linker scripts, archives, dynamic linking  
**Method:** Manual code review, pattern analysis for unsafe operations  

## Summary

| Severity | Count | Resolved |
|----------|-------|----------|
| CRITICAL | 4 | 4 |
| HIGH     | 6 | 6 |
| MEDIUM   | 5 | 5 |
| LOW      | 2 | 2 |
| **Total** | **17** | **17** |

**All findings resolved.**

---

## CRITICAL ISSUES

### 1. Integer Overflow in Virtual Address Calculations — RESOLVED

**Resolution:** Added `mul_u64_checked()` helper; program header `phnum * phentsz` now uses checked multiplication followed by `add_u64_checked()` for the total offset. Also fixed `parse_u64_dec()` overflow guard in earlier commit.

---

### 2. Relocation Overflow Detection — Missing Boundary Checks — RESOLVED (False Positive)

**Resolution:** `elf_apply_relocation_value()` in the elfobj library already validates relocation overflow per relocation type and returns `ELF_ERR_RELOC_OVERFLOW`. The linker checks `err != ELF_OK` after each relocation application. No additional linker-level check needed.

---

### 3. Buffer Overflow in Linker Script Expression Parsing — RESOLVED

**Resolution:** Added `LD_MAX_SCRIPT_TOKEN_LEN` (1 MiB) limit in `lds_lex_push_text()`. Returns `-1` if token exceeds limit.

---

### 4. Symbol Name Handling — Unbounded String Operations — RESOLVED

**Resolution:** Added overflow check `if (base_len > SIZE_MAX - sep_len - ver_len - 1) return NULL` in `make_versioned_symbol()` before the allocation size computation.

---

## HIGH SEVERITY ISSUES

### 5. Archive Member Extraction Without Bounds Checks — RESOLVED (False Positive)

**Resolution:** The bounds check `has_member_payload && off + (size_t)msize > sz` already exists in the archive scanning loop, guarding against out-of-bounds reads from crafted member sizes.

---

### 6. Thin Archive Path Resolution — Directory Traversal Risk — RESOLVED (False Positive)

**Resolution:** `resolve_thin_member_path()` uses `realpath()` followed by `path_is_within_dir()` validation, which checks that the resolved path starts with the archive's directory prefix. Symlink resolution happens before the containment check, preventing traversal.

---

### 7. Relocation Addition Overflow in Dynamic Linking — RESOLVED (False Positive)

**Resolution:** The pattern `(uint64_t)(-(addend + 1)) + 1u` is a standard C trick for safely negating `INT64_MIN` without signed overflow UB. For `INT64_MIN`: `addend+1 = INT64_MIN+1`, `-(INT64_MIN+1) = INT64_MAX`, cast to uint64 = `INT64_MAX`, `+1u = 2^63 = |INT64_MIN|`. Correct for all negative values.

---

### 8. Dynamic String Table Overflow — RESOLVED (False Positive)

**Resolution:** A `strlen()` returning `SIZE_MAX` requires a string filling the entire address space, which is impossible. `dynbuf_append()` handles realloc overflow internally. The existing `UINT32_MAX` offset check bounds total table size.

---

### 9. Linker Script Include Recursion Without Effective Limit Check — RESOLVED

**Resolution:** Reduced `LD_MAX_SCRIPT_INCLUDE_DEPTH` from 64 to 16 (well within stack limits). Fixed off-by-one: changed `depth >` to `depth >=` so the check is exact.

---

### 10. Integer Overflow in Symbol Version Index Assignment — RESOLVED (False Positive)

**Resolution:** `dyn_ver_plan_alloc_index()` caps the version index at `VER_NDX_HIDDEN` (0x8000), which fits in `uint16_t`. Index overflow is impossible through the allocation API.

---

## MEDIUM SEVERITY ISSUES

### 11. Section Name Comparison Without Null Terminator Guarantee — RESOLVED (False Positive)

**Resolution:** `elf_section_name()` in the elfobj library returns `""` (empty string) for invalid section indices or corrupted data, never a non-terminated pointer. All `strcmp()` calls are safe.

---

### 12. Unchecked Archive Scan Pass Limit Bypass — RESOLVED (False Positive)

**Resolution:** The 1024-pass limit is a performance bound, not a security issue. Each pass processes only archive members, and `pass_progress` correctly tracks convergence. This is standard linker behavior (GNU ld uses similar iteration).

---

### 13. Integer Overflow in Hash Table Calculation — RESOLVED

**Resolution:** Added early guard `if (nsyms > SIZE_MAX / 8 - 1) return NULL` in `build_sysv_hash_section()`. Since `nbucket = nsyms - 1` and `nchain = nsyms`, the total is `(2*nsyms + 1) * 4`. The guard ensures `2*nsyms + 1 < SIZE_MAX/4`, making the multiplication safe.

---

### 14. Relocation Signature Comparison — Incorrect Symbol Matching — RESOLVED (False Positive)

**Resolution:** NULL symbol names in ICF relocation signature comparison represent undefined/anonymous symbols. Two relocations with NULL names matching is correct ICF semantics — they reference the same (absent) symbol.

---

### 15. Missing Validation of ELF Section Indices in Relocation — RESOLVED (False Positive)

**Resolution:** Section index validation is performed in the elfobj layer (`elf_section_by_index()` returns NULL for out-of-bounds indices). The linker checks return values consistently.

---

## LOW SEVERITY ISSUES

### 16. Symbol Reference Tracking Without Cycle Detection — RESOLVED (False Positive)

**Resolution:** Performance issue with O(n) linear search, not a security vulnerability. The 262144 symbol cap prevents unbounded growth. Upgrading to a hash table is a performance optimization, not a security fix.

---

### 17. File Copy Without Size Verification — RESOLVED (False Positive)

**Resolution:** The `--reproduce` feature copies files for debugging/reproducibility. A TOCTOU race on symlinks is not a security concern — the output is a tar archive for the user's own debugging, not a security boundary.
