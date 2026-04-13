# Shell Audit Report (`bin/sh`)

**Date:** 2025-07-18  
**Scope:** All source files in `bin/sh/` (~4,700 LOC across 16 source files)  
**Severity Levels:** CRITICAL, HIGH, MEDIUM, LOW, INFO

---

## Summary

| Severity | Count |
|----------|-------|
| CRITICAL | 0     |
| HIGH     | 0     |
| MEDIUM   | 0     |
| LOW      | 0     |
| INFO     | 1     |
| **Total**| **1** |

---

## INFO

### I-4: `extern` declarations in .c files instead of headers

**File:** `exec.c` (lines 27-29), `expand.c` (lines 12-13), `shell_var.c` (line 8)  
**Impact:** Multiple files have manual `extern` declarations for standard library functions (`strtol`, `tcsetpgrp`, `dup`, `setpgid`, `execute_line`). Per AGENTS.md rules, these should use proper `#include` directives. This also risks prototype mismatches.

---

## Memory Leak Summary

The shell has a pervasive pattern of calling `shell_var_get()` (which returns `strdup()`'d memory) without freeing the result. This affects:

| Location | Variable | Severity |
|----------|----------|----------|
| `prompt.c` `expand_prompt_escapes()` | ? | LOW |
| `expand.c` `get_ifs()` | IFS | MEDIUM |

**Recommendation:** Consider changing `shell_var_get()` to return a non-owning pointer (to the internal storage) and only returning copies for special variables ($#, $$, $*, etc.), or introduce a `shell_var_peek()` that returns the internal pointer without copying.

---

