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
| LOW      | 1     |
| INFO     | 4     |
| **Total**| **5** |

---

## LOW

### L-8: `builtin_echo` doesn't handle `-n` flag

**File:** `exec.c`, `builtin_echo()` (~line 722)  
**Impact:** The `-n` flag (suppress trailing newline) is a common extension supported by nearly all shells. The current implementation always outputs a newline and would print `-n` as literal text.

---

## INFO

### I-1: `shell_promptvars` / SHELL_PROMPT_MODE is set readonly at init

**File:** `sh.c`, `init_environment()` (~line 125)  
**Impact:** `SHELL_PROMPT_MODE` is exported and set readonly. However, `builtin_set -o promptvars` calls `shell_var_force_set()` to bypass readonly. This is intentional but surprising — the readonly is for user scripts, not the shell itself. Consider documenting this behavior.

---

### I-2: `capture_command_output()` has no size limit

**File:** `expand.c`, `capture_command_output()` (~line 35)  
**Impact:** Command substitution reads unlimited output into a dynamically grown buffer. A malicious or buggy command could exhaust memory. Consider adding a configurable limit.

---

### I-3: No SIGPIPE handling in prompt evaluation

**File:** `prompt.c`  
**Impact:** If prompt evaluation involves command substitution via `expand_word()`, a broken pipe could terminate the shell. The signal masking in `evaluate_prompt()` blocks SIGINT/SIGTERM/SIGCHLD but not SIGPIPE.

---

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

## POSIX Compliance Gaps

5. **echo:** Missing `-n` flag
