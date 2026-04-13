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
| MEDIUM   | 1     |
| LOW      | 8     |
| INFO     | 4     |
| **Total**| **13**|

---

## MEDIUM

### M-10: `builtin_test` is incomplete

**File:** `exec.c`, `builtin_test()` (~line 750)  
**Impact:** The `test`/`[` builtin only handles a very limited subset: `-z`, `-n` (3 args), and `=`, `!=`, `-lt`, `-gt`, `-eq`, `-ne` (4 args). Missing: file tests (`-e`, `-f`, `-d`, `-r`, `-w`, `-x`, `-s`, `-L`, etc.), `-a`/`-o` logical operators, `!` negation, string comparison (`<`, `>`), `-le`, `-ge`. Many standard shell scripts will fail.

**Fix:** Implement at least the POSIX-required primary expressions.

---

## LOW

### L-1: Tilde expansion only handles bare `~`

**File:** `expand.c`  
**Impact:** `~user` form is not expanded (POSIX says `~login` should expand to the user's home directory via `getpwnam()`). Only bare `~` is expanded to `$HOME`.

---

### L-2: `builtin_fg` doesn't return the job's exit status

**File:** `job.c`, `builtin_fg()` (~line 260)  
**Impact:** POSIX requires `fg` to return the exit status of the foregrounded job. The current implementation always returns 0.

---

### L-3: Duplicate forward declaration of `parse_for` in parser.c

**File:** `parser.c`  
**Impact:** Cosmetic issue, compiles fine but indicates sloppy maintenance.

---

### L-4: `$*` does not use first character of IFS

**File:** `shell_var.c`, `shell_var_get("*")` (~line 226)  
**Impact:** POSIX requires `"$*"` to join positional parameters with the first character of IFS. The implementation always joins with space.

---

### L-5: Prompt variable `p` leaked from `shell_var_get("prompt")` / `shell_var_get("PS1")`

**File:** `sh.c`, interactive loop (~line 350)  
**Impact:** Each iteration of the interactive loop calls `shell_var_get("prompt")` or `shell_var_get("PS1")`, getting strdup'd memory that is never freed. Slow leak during interactive use.

---

### L-6: `evaluate_prompt` leaks `shell_var_get("?")` saved status

**File:** `prompt.c`, `evaluate_prompt()` (~line 410)  
**Impact:** `shell_var_get("?")` returns strdup'd memory stored in `saved_status_str`, then `saved_status = strdup(saved_status_str)`. But `saved_status_str` itself (`shell_var_get`'s return) is never freed.

```c
char *saved_status_str = shell_var_get("?");
char *saved_status = saved_status_str ? strdup(saved_status_str) : NULL;
// saved_status_str never freed
```

---

### L-7: Case statement expansion only matches first pattern per item

**File:** `exec.c`, `execute_case()` (~line 2025)  
**Impact:** POSIX case items can have multiple `|`-separated patterns. Only the first pattern is matched. This is actually a parser-level issue — the AST `case_item` only stores one pattern string.

---

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
| `sh.c` interactive loop | prompt, PS1 | LOW |
| `prompt.c` `evaluate_prompt()` | ? | LOW |
| `prompt.c` `expand_prompt_escapes()` | ? | LOW |
| `expand.c` `get_ifs()` | IFS | MEDIUM |

**Recommendation:** Consider changing `shell_var_get()` to return a non-owning pointer (to the internal storage) and only returning copies for special variables ($#, $$, $*, etc.), or introduce a `shell_var_peek()` that returns the internal pointer without copying.

---

## POSIX Compliance Gaps

1. **$*:** Doesn't use first char of IFS for joining
4. **test:** Missing file tests, logic operators, negation
5. **echo:** Missing `-n` flag
7. **case:** Single pattern per item (no `|` alternatives)
8. **~user:** Not expanded
