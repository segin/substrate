# Substrate libc Codebase Audit Report

**Scope:** `lib/c/`
**Date:** April 12, 2026

---

## Executive Summary

| Severity | Count | Key Areas |
|----------|-------|-----------|
| **CRITICAL** | 5 | Buffer overflows (sprintf, strcpy, strcat), libgen edge cases, dirent bounds |
| **HIGH** | 6 | setjmp ABI, signal safety, thread safety |
| **MEDIUM** | 8 | UTF-8 validation, alignment, division by zero, float precision |
| **LOW** | 6 | Stub implementations, error handling, atexit ordering |
| **TOTAL** | **25** | |

---

## CRITICAL Findings

### 1. Buffer Overflow in `sprintf()`

- **File:** `lib/c/stdio/printf.c`, line 582
- **Issue:** `sprintf()` calls `vsnprintf(..., INT_MAX, ...)` — effectively unbounded write to caller's buffer.
- **Impact:** Classic buffer overflow. Any caller passing a too-small buffer gets memory corruption.
- **Fix:** Consider removing `sprintf()` entirely or deprecating it. Userland should use `snprintf()`.

### 2. Unsafe `strcpy()` — No Bounds Checking

- **File:** `lib/c/src/string.c`, line 166
- **Issue:** Standard C `strcpy()` with no destination size parameter.
- **Impact:** Buffer overflow on any untrusted input.
- **Note:** While this is "by design" per C standard, the libc should provide and prefer `strlcpy()`.

### 3. Unsafe `strcat()` — No Bounds Checking

- **File:** `lib/c/src/string.c`, line 184
- **Issue:** Same as `strcpy()` — no destination size.
- **Fix:** Provide `strlcat()` and document `strcat()` as discouraged.

### 4. Off-by-One in `basename()`/`dirname()`

- **File:** `lib/c/src/libgen.c`, lines 19-20, 60-61
- **Issue:** Functions modify input buffer in-place with `*(end + 1) = '\0'`. Return pointers into modified input.
- **Impact:** Writes null terminator at unexpected positions if pointer arithmetic is off. Callers unaware of in-place modification get corrupted data.
- **Fix:** Document in-place modification clearly; consider using static buffers per POSIX allowance.

### 7. Buffer Overflow in `readdir()` / `findirp`

- **File:** `lib/c/src/dirent.c`, line 22
- **Issue:** `readdir()` trusts `d_reclen` from kernel without bounds checking against the directory buffer.
- **Impact:** Malformed directory entry overruns `dirp->buf`.
- **Fix:** Add `if (dirp->buf_pos + d->d_reclen > sizeof(dirp->buf)) return NULL;`

---

## HIGH Findings

### 10. setjmp/longjmp ESP Restoration

- **File:** `lib/c/arch/i386/setjmp.S`, lines 16, 38
- **Issue:** `setjmp()` saves ESP, `longjmp()` restores it directly. If the caller's stack frame has changed (locals added/removed by compiler), restoration jumps to wrong stack state.
- **Fix:** Document that local variables in the `setjmp()` caller are clobbered after `longjmp()`. Consider saving EBP-relative frame.

### 11. Stack Buffer Overflow in printf/fprintf

- **File:** `lib/c/stdio/printf.c`, lines 590-615
- **Issue:** `printf()`, `fprintf()`, etc. use a fixed `char buf[4096]` on the stack. Format strings producing >4096 bytes overflow.
- **Fix:** Use dynamic allocation or chunked output.

### 12. Hardcoded Limits in `strtol()`/`strtoll()`

- **File:** `lib/c/src/stdlib.c`, lines 255-335
- **Issue:** Overflow cutoff uses hardcoded `2147483647L` instead of `LONG_MAX` macro. Fragile if types change.
- **Fix:** Use `LONG_MIN`/`LONG_MAX` macros.

### 13. Signal Safety in malloc/free

- **File:** `lib/c/src/stdlib.c`, lines 140-200
- **Issue:** `malloc()`, `free()`, `realloc()` use unprotected global state (`global_base`, `last` pointers, block metadata). If a signal handler calls `malloc()` while the main thread is in `malloc()`, heap corruption occurs.
- **Fix:** Mask signals around heap operations or document "no malloc in signal handlers."

### 14. Thread Safety of `strtok()`

- **File:** `lib/c/src/string.c`, line 327
- **Issue:** Static `saveptr` variable shared across all threads.
- **Fix:** Use `_Thread_local` storage or deprecate in favor of `strtok_r()`.

### 15. Thread Safety of `atexit()`

- **File:** `lib/c/src/stdlib.c`, lines 1-50
- **Issue:** `__atexit_funcs[]` and `__atexit_count` are global with no synchronization.
- **Fix:** Use atomic operations for the counter.

---

## MEDIUM Findings

### 18. Incomplete UTF-8 Validation in wchar

- **File:** `lib/c/src/wchar.c`, lines 1-70
- **Issue:** UTF-8 parser accepts overlong sequences and surrogate codepoints (U+D800-U+DFFF).
- **Fix:** Validate against overlong encodings and reserved ranges.

### 19. Format String Vulnerability in scanf

- **File:** `lib/c/stdio/scanf.c`
- **Issue:** Format string not validated — lower risk than printf since scanf reads input, but attack surface exists with user-controlled formats.

### 20. Memory Leak in `sysctl_helpers.c`

- **File:** `lib/c/src/sysctl_helpers.c`, lines 50-60
- **Issue:** Partial-failure paths don't free intermediate buffers in all cases.

### 21. `aligned_alloc()` Rejects Large Alignments

- **File:** `lib/c/src/stdlib.c`, lines 294-300
- **Issue:** Returns NULL for alignments >16 bytes. Legitimate code requesting 64-byte (cache line) or 4096-byte (page) alignment silently fails.
- **Fix:** Over-allocate and align within the block.

### 22. Division by Zero in div64

- **File:** `lib/c/src/div64.c`
- **Issue:** Calls `__builtin_trap()` on division by zero — kills the process with no recovery.
- **Fix:** Document this behavior or return an error.

### 23. Float Formatting Precision

- **File:** `lib/c/stdio/printf.c`, lines 70-80
- **Issue:** Simple rounding can accumulate errors. `%.20f` may not produce exactly 20 correct digits.

### 24. Incomplete `getcwd()` Implementation

- **File:** `lib/c/src/sys.c`, line 133
- **Issue:** POSIX says if `buf` is NULL, `getcwd()` should allocate internally. This implementation doesn't.

### 25. `readdir()` assumes valid `d_reclen` from kernel  

- **File:** `lib/c/src/dirent.c`
- **Issue:** See CRITICAL #7 — this is the bounds-checking gap.

---

## LOW Findings

### 26. Incomplete errno Mapping in `strerror()`

- **File:** `lib/c/src/string.c`, lines 368-400
- **Issue:** Not all POSIX errno values mapped. Some return "Unknown error."

### 27. Stub pwd/grp Implementation

- **Files:** `lib/c/src/pwd.c`, `lib/c/src/grp.c`
- **Issue:** Only returns mock data for root. Any other user lookup returns NULL.
- **Fix:** Read from `/etc/passwd` and `/etc/group`.

### 28. fnmatch Character Range Edge Cases

- **File:** `lib/c/fnmatch.c`, lines 77-95
- **Issue:** `[a\-z]` doesn't correctly match literal `-` in all cases.

### 29. `getenv()` Doesn't Set errno

- **File:** `lib/c/src/stdlib.c`, line 379
- **Issue:** Returns NULL without setting errno. Caller can't distinguish "not found" from error.

### 31. `atexit()` Handler Re-Registration

- **File:** `lib/c/src/stdlib.c`, lines 730-735
- **Issue:** If `atexit()` is called from within an atexit handler, the new handler won't run. POSIX allows this, but it should be documented.

### 32. FILE Structure Not Fully Initialized

- **File:** `lib/c/stdio/stdio_core.c`, lines 25-45
- **Issue:** `fdopen()` doesn't zero all FILE fields in some paths. Uninitialized fields can cause subtle bugs.
- **Fix:** `memset(f, 0, sizeof(FILE))` on allocation.

---

## Positive Notes

- Clean separation of arch-specific code (i386/ for crt0, setjmp, syscall).
- Functional printf/scanf family with format specifier support.
- `strlcpy`/`strlcat` available as safer alternatives.

---

## Recommendations

1. **Immediate:** Resolve the remaining unbounded string/printf entry points (#1, #2, #3).
2. **Immediate:** Add bounds validation for `readdir()` records (#7).
3. **Short-term:** Mask signals in malloc/free (#13) or document the restriction.
4. **Short-term:** Make `strtok()` and `atexit()` thread-safe (#14, #15).
5. **Medium-term:** Implement proper `aligned_alloc()` (#21).
6. **Medium-term:** Read `/etc/passwd` and `/etc/group` in pwd/grp (#27).
7. **Testing:** Fuzz printf/scanf with extreme format strings and values.
