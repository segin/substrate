# bin/sh audit - 2026-07

Full read of Substrate's hand-rolled POSIX shell (`bin/sh/`, ~7,400 lines):
`lexer.c` + `parser.c` (tokenize/parse), `expand.c` (word expansion - the
largest security surface), `exec.c` (execution/redirection/builtins/
fork-exec), `shell_var.c` + `util.c` (variable store + the shared glob
matcher `match_pattern`), and `job.c` + `prompt.c` + `sh.c` (job control,
PS1 rendering, main loop).

The shell parses and runs UNTRUSTED input: scripts, interactive lines,
`eval`, command-substitution output, `$ENV`, and PS1 from the environment.
Memory-safety, DoS-resistance, fd/signal safety, and quoting/injection
correctness are the priority.

Read by parallel auditors (one per module group), cross-checked against
source and against a host build (`sh_host`) compiled with
AddressSanitizer/UBSan for repro. `[VERIFIED]` = reproduced (ASan or
observed); `[code]` = confirmed by inspection. Substrate's primary target
is 32-bit, so `size_t` growth math (`cap*2`, `len+n`) can wrap.

Findings numbered SH-NN by severity.

## What's solid (coverage confirmed)

- The per-token / per-word / prompt string builders all funnel through
  `buffer_append` (util.c), which hard-caps at 1 MiB, keeps a NUL
  terminator, uses overflow-safe index math (32-bit `cap*2` can't wrap),
  and converts OOM into a clean `exit(1)` rather than an OOB write. The
  lexer scanner is strictly bounded (`pos < len`) and cannot run off the
  end; unterminated `'`/`"` produce a clean `TOKEN_ERROR`.
- Signal handling is correct by construction: there is no async SIGCHLD
  handler - reaping is synchronous via `waitpid(WNOHANG)`; the only async
  handler (`trap_handler`) writes `volatile sig_atomic_t` flags only, so
  the "signal handler calls malloc/printf" class does not apply.
- `shell_var_get` returns `strdup` copies on every path, so there is no
  get-then-set aliasing use-after-free; `shell_var_shift`/`unset` and the
  job-list mutation during iteration are memory-correct. Command
  substitution caps output at 256 KiB, drains the pipe, and reaps the
  child. No `strcpy`/`strcat`/`sprintf`/`gets` anywhere.

## High

### SH-01: `main` read loop uses `buf` after its scope ends (sh.c:387-400)
`char buf[1024]` is declared inside the `if (!line) { ... }` block, then
`line = buf` escapes that block; `*line` (393) and `execute_line(line)`
(398) run after `buf` is out of scope. **[VERIFIED - ASan]** every piped
or scripted line (the `el == NULL` path) triggers `stack-use-after-scope`
at sh.c:393; under optimization the compiler may reuse `buf`'s slot,
corrupting the command line. Fix: hoist `buf` to the loop-body scope.

### SH-02: arithmetic `INT_MIN / -1` and `% -1` → SIGFPE (expand.c:229,234)
The zero divisor is checked but the overflow corner is not; `LONG_MIN /
-1` traps (`idiv`). **[code]** `echo $(( (-2147483647 - 1) / -1 ))` crashes
the shell. Fix: reject `r == 0 || (left == LONG_MIN && r == -1)` as an
arithmetic error.

### SH-03: no recursion-depth cap in the parser → C stack overflow (parser.c parse_list↔parse_command cycle)
Every compound recurses back to `parse_list` with no depth counter.
**[VERIFIED - ASan]** `python3 -c 'print("("*200000+"true"+")"*200000)' |
sh` overflows the stack (SIGSEGV). Fix: thread a depth counter and fail
with a syntax error past a cap (~200), as prompt.c/expand.c already do.

### SH-04: no recursion cap in arithmetic and `${:-}` expansion → stack overflow (expand.c:190 parse_unary; :733/740 default-expansion re-entry)
Nested `$(( ((((...)))) ))` and nested `${a:-${a:-...}}` each recurse one
C frame per level, uncapped. **[code]** thousands deep → SIGSEGV. Fix:
track an expansion/arith recursion depth in `expand_state_t` and bail past
a ceiling.

### SH-05: no function/`eval` recursion cap → stack overflow (exec.c execute_function/execute_ast; eval path)
`f(){ f; }; f` and self-referential `eval` recurse on the C stack
unbounded. **[code]** SIGSEGV. Fix: a FUNCNEST-style depth counter
returning an error past N.

### SH-06: `ast_free`/`ast_to_string` recurse down long operator chains → stack overflow (parser.c:67,76,190)
`parse_list` builds a left-nested `BINARY_OP` tree for `;`/`&&`/`||`
iteratively, but free/stringify recurse `left`, so depth == operator
count. **[code]** `sh -c "$(python3 -c 'print("true;"*500000)')"` parses
then overflows in `ast_free`. Fix: iterate the left spine of
`OP_SEQ`/`OP_AND`/`OP_OR` chains.

### SH-07: `match_pattern` (util.c) is O(strlen) recursive and exponentially backtracks on `*` → stack overflow + ReDoS (util.c:17-75)
The literal/`?`/`[` paths recurse one char per match (depth O(strlen)),
and `*` handling backtracks per position (exponential with many `*`). It
backs `case`, `${var#pat}`/`${var%pat}`, and pathname expansion, all with
attacker patterns. **[code]** `x=$(printf 'a%.0s' {1..2000000}); case $x in
aaaa...) ;; esac` overflows; `case aaaa...X in a*a*a*a*a*b) ;; esac` hangs.
Fix: rewrite as an iterative matcher with single-pointer `*` backtracking
(linear).

### SH-08: `glob_match` backtracks exponentially on consecutive `*` (expand.c:382-385)
Unlike util.c's matcher it does not collapse `*` runs; a filename that
doesn't match against many-star patterns backtracks per directory entry.
**[code]** DoS. Fix: collapse consecutive `*`; bound recursion.

### SH-09: saved redirection fds leak into every redirected exec'd child (exec.c:279)
`save_redirections` uses plain `dup()` (no `FD_CLOEXEC`); the saved fd is
never closed in the child, so e.g. `ls > out` runs `ls` with the original
stdout still open on a high fd. **[code]** fd/info leak into untrusted
programs. Fix: `F_DUPFD_CLOEXEC` / set `FD_CLOEXEC` on saved fds.

### SH-10: heredoc body written synchronously to a pipe → self-deadlock >64 KiB (exec.c:1495-1516)
The parent `write()`s the whole heredoc to the pipe before any reader
exists; a body larger than the pipe buffer blocks the single shell
forever. **[code]** `cat <<EOF` + >64 KiB body. Fix: fork a writer, or
spill to an unlinked temp file and dup its read fd.

### SH-11: the `0x80` quoting marker collides with the 8th data bit → 8-bit/UTF-8 corruption and metacharacter transformation (expand.c:132,974,987)
Quoting is recorded as bit 0x80 of each byte; any input/command-output
byte >= 0x80 is treated as "quoted" and `remove_quotes` clears 0x80,
mutating it (0xAF→'/', 0xAA→'*'). **[code]** `x="café"; echo "$x"` mangles
UTF-8; `y="$(printf '\257')"; echo "$y"` yields a literal `/` (a path
metacharacter). Fix: use an out-of-band escape (CTLESC doubling) or a
parallel quote-mask buffer.

### SH-12: PS1 command substitution runs unconditionally, ignoring `promptvars` (prompt.c:424)
`evaluate_prompt` always `expand_word`s the prompt, so an environment
`PS1` with `$(...)` executes on every prompt regardless of `set +o
promptvars`. **[code]** `PS1='$(touch /tmp/pwned) '` runs the command.
Fix: gate the substitution on `shell_promptvars`.

## Medium

### SH-13: unchecked `realloc`/`malloc` in the parser and lexer allocation sites (parser.c:210,219,995,755,28,32,34; lexer.c:49,118,143,163,336)
`x = realloc(x, ...)` then `x[count++] = ...` with no NULL check (loses the
old block, writes through NULL); token/node `calloc`/`malloc`/`strdup`
dereferenced immediately. **[code]** NULL deref under memory pressure. Fix:
check every allocation (the `buffer_append` OOM convention).

### SH-14: unchecked allocations + word-list `realloc` in the expander (expand.c:45,65,113,457,519,534,552,994,1043,1079)
`capture_command_output`'s initial `malloc` and the split/glob word-list
`realloc` are unchecked; on realloc failure `capture_command_output` also
leaks `buf` and orphans the child (no drain/`waitpid`). **[code]** NULL
deref / leak / zombie under pressure. Fix: check allocations; on failure
`free(buf)` and reap.

### SH-15: unchecked `malloc`/`strdup` and NULL name/value in the variable store (shell_var.c:178,255,266,299,306,355,217,441)
`shell_var_set`/`force_set`/`set_local`/`$*`-`$@`-join do `strdup`/`malloc`
unchecked; `shell_var_get(NULL)` / `shell_var_set(...,NULL)` dereference
NULL. **[code]** NULL deref. Fix: check allocations; guard NULL name/value.

### SH-16: unchecked child-side allocations in exec (exec.c:1107,1249,1554)
`builtin_command`'s `new_argv`, `builtin_read`'s reconstruction `malloc`,
and `merge_env`'s `new_env` (in the child just before `execve`) are
unchecked → segfault instead of a clean exec-failure report. **[code]**
Fix: check and `_exit(1)`/return on NULL.

### SH-17: `read_stream_all` capacity doubling overflows on 32-bit + leaks on realloc failure (util.c:165-171)
`cap *= 2` wraps to 0 past 2 GiB → `realloc(buf,0)` then a huge `memcpy`
(heap overflow) or an infinite loop; `buf = realloc(buf,cap)` leaks the
old block on failure. Reached via `$ENV`/profile files and command
substitution. **[code]** `ENV=/huge/file`. Fix: apply the 1 MiB-style cap,
realloc into a temp pointer.

### SH-18: script file read truncates `off_t`→`size_t` on 32-bit → heap overflow (sh.c:331)
`malloc(fsize+1)` and `fread(...,fsize,...)` truncate a 64-bit `off_t`
non-identically at the 2^32 wrap; a ~4 GiB script makes `malloc(0)` while
`fread` writes 4 GiB. **[code]** Fix: reject `fsize < 0 || fsize >=
SIZE_MAX`; read in a bounded loop.

### SH-19: heredoc body accumulation is uncapped and overflow-prone (parser.c:344-384)
`read_heredoc` grows with `cap *= 2` and an unchecked `realloc`, no 1 MiB
cap; an unterminated `<<EOF` copies all input and can wrap on 32-bit.
**[code]** Fix: apply `BUFFER_MAX_SIZE` and check `realloc`.

### SH-20: `is*()` called with signed `char` → ctype UB / OOB table read (expand.c:140,173,...; util.c range compares; shell_var char compares)
Bytes >= 0x80 in values/output/IFS become negative `int` args to
`isspace`/`isdigit`/etc., undefined and potentially indexing before the
ctype table; `match_pattern` range compares mis-handle high-bit bytes.
**[code]** `IFS=$(printf '\240'); set -- $(printf 'a\240b')`. Fix: cast to
`(unsigned char)` at every call site.

## Low

### SH-21: parser/exec robustness gaps
IO-number parsed with `atoi` overflows to a negative fd (parser.c:276,459);
`dup2` return unchecked for REDIR_OUT/APPEND/IN (exec.c:1464); `builtin_exit`
uses `exit()` (flushing inherited stdio) in a forked pipeline/subshell
child (exec.c:402); a non-interactive background pipeline leaves zombies
unreaped (exec.c:2051); pipeline fork-failure mid-loop leaks pipe fds and
does not reap already-forked children (exec.c:1999); `malloc(2*(n-1)*...)`
with `n<=1` edge (exec.c:1929). **[code]** Fix per site.

### SH-22: correctness / compatibility gaps
Backtick state leaks out of a double-quoted span, swallowing the rest of
the line (lexer.c:232); heredocs on compound commands
(`while ... done <<EOF`) are never read, leaving `heredoc_content == NULL`
(parser.c parse_redirections); `${VAR:offset:len}` substring is
unimplemented and silently drops the modifier (expand.c:920); `merge_env`'s
`buf[2048]` truncates long `VAR=val` entries (exec.c:1566); the fixed
`fgets` `buf[1024]` splits long non-editline lines mid-token (sh.c:388);
PS1 octal `\nnn` / `\[` / `\]` escapes are unimplemented (prompt.c).
**[code]** Fix per site.

## Verification method

Findings verified against a host build of the shell (`sh_host`) compiled
with `-fsanitize=address,undefined`, driven by crafted hostile scripts
piped to it. 32-bit-overflow findings (SH-17/18/19) require an `-m32` build
or multi-GiB inputs to observe directly; they are fixed by inspection plus
the overflow-safe pattern already used in `buffer_append`.
