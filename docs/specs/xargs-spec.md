# Specification: `xargs` (POSIX.1-2024 + GNU + BSD, BSD-wins)

Status: normative for `bin/xargs`. Audience: an implementing engineer or LLM.

## 1. Scope & normative references

Build an `xargs` utility that constructs and executes command lines from
items read on standard input. It shall be a strict superset of:

- **[POSIX]** IEEE Std 1003.1-2024 (POSIX.1-2024), `xargs` utility.
- **[GNU]** GNU findutils `xargs` extensions.
- **[BSD]** FreeBSD `xargs(1)` extensions.

**Conflict-resolution policy (CR):** where POSIX, GNU, and BSD disagree on
observable behavior or option semantics, **BSD wins**. Every requirement is
tagged with provenance `[POSIX]`/`[GNU]`/`[BSD]`/`[CR:BSD]`.

Target conventions (substrate): C2x, `-m32`, project libc (`-nostdinc`),
dynamic (`DYNAMIC=1`) PIE; lives at `bin/xargs/`; man page
`usr.man/man1/xargs.1`; oracle test `tests/bin/xargs/test_xargs.sh`;
registered in `bin/Makefile` SUBDIRS (alphabetical, after `write`).

## 2. Option matrix & conflict resolution

| Option (short / long) | Arg | Origin | Decision |
|---|---|---|---|
| `-0` / `--null` | — | GNU+BSD | NUL item delimiter; disables quote/backslash/`-E` |
| `-a F` / `--arg-file=F` | file | GNU | read items from `F` instead of stdin |
| `-d D` / `--delimiter=D` | delim | GNU | item delimiter (1 char or C-escape `\n\t\\\0\ooo\xHH`); disables quoting |
| `-E S` | eofstr | POSIX+BSD | logical-EOF string; empty `S` ⇒ no logical EOF |
| `-e[S]` / `--eof[=S]` | opt | GNU(dep) | alias of `-E`; bare `-e` ⇒ no logical EOF |
| `-I R` / `--replace[=R]` (`-i`) | replstr | POSIX+GNU+BSD | replace `R` per arg-template token; implies `-L 1`, `-x`, `-r`-off. **[CR:BSD]** semantics |
| `-J R` | replstr | BSD | insert all collected items at first `R`; composes with `-n`/`-L` |
| `-L N` / `--max-lines[=N]` (`-l`) | num | POSIX+GNU+BSD | `N` input *lines* per invocation |
| `-n N` / `--max-args=N` | num | POSIX+GNU+BSD | max items per invocation |
| `-o` / `--open-tty` | — | GNU+BSD | reopen stdin from `/dev/tty` in child (interactive) |
| `-P N` / `--max-procs=N` | num | GNU+BSD | run up to `N` invocations in parallel (`0` = unlimited-ish) |
| `-p` / `--interactive` | — | POSIX+GNU+BSD | prompt on `/dev/tty` before each invocation; implies `-t` |
| `-R N` | num | BSD | with `-I`: max replacements of `R` (`-1` = unlimited) |
| `-r` / `--no-run-if-empty` | — | GNU+BSD | do not run if no items read |
| `-S N` | num | BSD | with `-I`/`-J`: bytes available for replacement expansion |
| `-s N` / `--max-chars=N` | num | POSIX+GNU+BSD | max command-line length (bytes) |
| `-t` / `--verbose` | — | POSIX+GNU+BSD | echo each command to stderr before running |
| `-x` / `--exit` | — | POSIX+GNU+BSD | exit if an invocation would exceed `-s`/`-n`/`-L` |
| `--help`, `--version` | — | GNU | usage / version then exit 0 |

**CR notes (BSD wins):**
- CR-1 No default logical-EOF string (historic `_` is removed everywhere).
- CR-2 `-I` requires an explicit `replstr`; replacement is applied to every
  argument-template token that contains `R`, one invocation per input line.
  `-J` is BSD-only single-point insertion that still honors `-n`/`-L`.
- CR-3 Default = invoke utility **at least once even when input is empty**
  (constant args only); `-r` suppresses that.
- CR-4 Exit codes follow BSD/GNU: `0` ok; `123` any child exited 1–125;
  `124` a child exited 255 (stop); `125` a child killed by signal (stop);
  `126` child found but not executable; `127` child not found; `1` xargs
  self error.
- CR-5 `-s` default derived from `sysconf(_SC_ARG_MAX)` minus current
  environment size minus headroom (BSD style), floored at POSIX `LINE_MAX`.
- CR-6 Quoting (default mode only): unquoted blank/tab/newline separate
  items; `'…'` and `"…"` group (no escapes inside); `\` escapes any next
  byte. `-0` and `-d` disable all quoting and escaping.

## 3. User stories

- **US-1** As a shell user, I want to feed a long list of filenames into a
  command so I don't hit "argument list too long". *(POSIX core)*
- **US-2** As a scripter, I want `find … -print0 | xargs -0 …` so filenames
  with spaces/newlines are safe. *(`-0`)*
- **US-3** As a scripter, I want `xargs -I{} cp {} /dest` to run one copy per
  input item with the name substituted. *(`-I`)*
- **US-4** As a power user, I want `-P N` to parallelize independent
  invocations across N children. *(`-P`)*
- **US-5** As a cautious operator, I want `-p` to confirm each command and
  `-t` to see what runs. *(`-p`/`-t`)*
- **US-6** As a scripter, I want `-r` so an empty input runs nothing. *(`-r`)*
- **US-7** As a BSD user, I want `-J` single-point insertion and `-o` to make
  the child interactive. *(BSD `-J`/`-o`)*
- **US-8** As a tooling author, I want exit codes that distinguish child
  failure (123), abort (124/125), and not-found (126/127). *(CR-4)*
- **US-9** As a porter, I want GNU long options (`--null`, `--arg-file`,
  `--max-procs`, …) to work for portability. *(GNU)*
- **US-10** As an integrator, I want `xargs` with no utility operand to
  default to `echo`. *(POSIX)*

## 4. Requirements (EARS)

Patterns: **U** ubiquitous, **EV** event ("When …"), **ST** state
("While …"), **UW** unwanted ("If … then …"), **OP** optional ("Where …").

### 4.1 Item reading & tokenization
- **R-1 (U)** [POSIX] The system shall read items from standard input (or the
  `-a` file) and treat blanks (space, tab) and newlines as item separators.
- **R-2 (OP)** [CR:BSD] Where neither `-0` nor `-d` is given, the system shall
  process single quotes, double quotes (grouping without inner escapes) and
  backslash (escapes the next byte) per CR-6.
- **R-3 (EV)** [GNU/BSD] When `-0`/`--null` is given, the system shall use NUL
  as the sole item delimiter and disable quoting, backslash, and `-E`.
- **R-4 (EV)** [GNU] When `-d`/`--delimiter` is given, the system shall use the
  decoded delimiter byte as the sole separator and disable quoting/backslash.
- **R-5 (UW)** [POSIX] If an item (after quote/escape removal) exceeds the
  effective command-line size limit, then the system shall write a diagnostic
  and exit with status 1.
- **R-6 (OP)** [GNU] Where `-a F` is given, the system shall read items from
  file `F`; multiple `-a` are read in order.

### 4.2 Logical EOF
- **R-7 (OP)** [POSIX/BSD] Where `-E S` (or `-e S`) is given with non-empty
  `S`, the system shall stop reading items at the first item equal to `S`.
- **R-8 (U)** [CR:BSD] The system shall use no logical-EOF string by default.
- **R-9 (EV)** [GNU/BSD] When `-0` or `-d` is in effect, logical EOF shall be
  disabled.

### 4.3 Command-line construction & limits
- **R-10 (U)** [POSIX] The system shall append read items to the
  `utility [argument…]` operand words to form each command line.
- **R-11 (U)** [POSIX] The system shall default `utility` to `echo` when no
  operand is given.
- **R-12 (OP)** [POSIX/BSD] Where `-n N` is given, the system shall place at
  most `N` items per invocation.
- **R-13 (OP)** [POSIX/BSD] Where `-L N` is given, the system shall consume `N`
  nonempty input lines per invocation; a trailing blank continues a line.
- **R-14 (U)** [CR:BSD] The system shall bound each command line to the `-s`
  size; default per CR-5 from `sysconf(_SC_ARG_MAX)` − environment − headroom,
  floored at `LINE_MAX` (2048).
- **R-15 (UW)** [POSIX] If `-x` is set and a full `-n`/`-L` group cannot fit in
  the `-s` limit, then the system shall write a diagnostic and exit 1.
- **R-16 (U)** [POSIX] Absent `-x`, the system shall pack as many items as fit
  in `-s` (subject to `-n`/`-L`) and invoke utility once per packed group.

### 4.4 Replacement modes
- **R-17 (OP)** [CR:BSD] Where `-I R` is given, the system shall, for each
  input line, build one command line replacing every occurrence of `R` inside
  each operand argument word with the line's content; it shall imply `-L 1`,
  imply `-x`, and not run on empty input. `-i[R]` is an alias defaulting `R` to
  `{}`.
- **R-18 (OP)** [BSD] Where `-J R` is given, the system shall insert all
  collected items (subject to `-n`/`-L`) at the first operand word equal to /
  containing `R`, replacing that single occurrence.
- **R-19 (OP)** [BSD] Where `-R N` is given with `-I`, the system shall replace
  at most `N` occurrences of `R` (`N<0` ⇒ unlimited).
- **R-20 (OP)** [BSD] Where `-S N` is given, the system shall size the
  replacement expansion buffer to `N` bytes.

### 4.5 Invocation, interaction, parallelism
- **R-21 (EV)** [POSIX] When a command line is complete, the system shall
  `fork`/`execvp` the utility, search `PATH`, and pass the constructed argv.
- **R-22 (OP)** [POSIX/BSD] Where `-t` (or `-p`) is set, the system shall write
  the command (space-joined) to stderr before invoking it.
- **R-23 (OP)** [POSIX/BSD] Where `-p` is set, the system shall prompt on
  `/dev/tty` (`?...`) and invoke only on an affirmative (`y`/`Y…`) reply;
  `-p` implies `-t`.
- **R-24 (OP)** [GNU/BSD] Where `-o` is set, the child shall have stdin
  reopened from `/dev/tty`.
- **R-25 (OP)** [GNU/BSD] Where `-P N` is given, the system shall keep up to
  `N` invocations running concurrently and reap them as they finish (`N≤0` ⇒
  a large bound).
- **R-26 (UW)** [CR:BSD] If input is empty and `-r` is not given, then the
  system shall invoke the utility exactly once with only the constant args;
  if `-r` is given it shall invoke nothing and exit 0.

### 4.6 Exit status & diagnostics
- **R-27 (UW)** [CR:BSD] If any invocation exits 1–125, then xargs shall exit
  123 after processing all input.
- **R-28 (UW)** [CR:BSD] If an invocation exits 255, then xargs shall stop and
  exit 124.
- **R-29 (UW)** [CR:BSD] If an invocation is killed by a signal, then xargs
  shall stop and exit 125.
- **R-30 (UW)** [CR:BSD] If the utility cannot be executed (found, not
  executable) it shall exit 126; if not found, 127.
- **R-31 (U)** [POSIX] On success with no failing children, xargs shall exit 0.
- **R-32 (UW)** [POSIX] If a self error (bad option, I/O, alloc) occurs, then
  xargs shall write to stderr and exit 1.

### 4.7 Substrate integration (verification surface)
- **R-33 (U)** `make -C bin/xargs` (target) and `make -C bin/xargs
  NATIVE_BUILD=1` (host) shall both build clean with `-Wall -Wextra -Werror`.
- **R-34 (U)** A man page `usr.man/man1/xargs.1` shall document every option
  with SEE ALSO and EXIT STATUS sections.
- **R-35 (U)** `tests/bin/xargs/test_xargs.sh` shall oracle-compare the host
  build against the system `xargs` across the option surface and pass.
- **R-36 (U)** `xargs` shall be registered in `bin/Makefile` SUBDIRS.

## 5. LLM-optimized actionable tasklist

Each task is atomic and independently verifiable. `[req]` cites requirements.

- [x] **T1 — Skeleton.** Create `bin/xargs/{xargs.h,xargs.c,xargs_input.c,
  xargs_exec.c}` + `Makefile` (`PROG=xargs`, `SRCS=xargs.c xargs_input.c
  xargs_exec.c`, `DYNAMIC=1`, include the two Makefiles). Register `xargs` in
  `bin/Makefile` SUBDIRS after `write`. *(R-33,R-36)*
- [x] **T2 — State & options.** In `xargs.h` define `struct xargs_opts`
  (delim mode, eofstr, replstr/-I/-J, max_args/-n, max_lines/-L, max_chars/-s,
  max_procs/-P, flags: t,p,x,r,o,0; `-R`,`-S`; arg files). In `xargs.c` parse
  with `getopt_long` the full §2 matrix incl. long aliases; apply CR implications
  (`-I`⇒`-L1`,`-x`; `-p`⇒`-t`; `-0`/`-d`⇒disable quoting+EOF). *(R-3,R-4,R-7..9,R-17,R-23)*
- [x] **T3 — Size default.** Compute default `-s` per CR-5
  (`sysconf(_SC_ARG_MAX)` − env bytes − headroom, floor `LINE_MAX`=2048); honor
  explicit `-s`/`-S`. *(R-14,CR-5)*
- [x] **T4 — Tokenizer.** `xargs_input.c`: a reader returning the next item or
  EOF from stdin/`-a` files, implementing CR-6 quoting+backslash in default
  mode, NUL in `-0`, single byte in `-d`, and logical-EOF (`-E`). Track line
  boundaries for `-L`/`-I`. *(R-1..R-9,R-13)*
- [x] **T5 — Command builder.** Group items into argv honoring `-n`, `-L`,
  `-s` packing and `-x` overflow rule; produce a NULL-terminated `argv`
  starting from the operand words. *(R-10..R-12,R-15,R-16)*
- [x] **T6 — Replacement.** Implement `-I` (per-line, per-token replace, `-R`
  count, `-S` buffer) and `-J` (single-point insert). *(R-17..R-20)*
- [x] **T7 — Executor.** `xargs_exec.c`: `-t`/`-p` echo+prompt on `/dev/tty`,
  `-o` child stdin from `/dev/tty`, `fork`/`execvp`, status capture, exit-code
  mapping (R-27..R-31). *(R-21..R-24,R-27..R-32)*
- [x] **T8 — Parallelism.** Add `-P N`: maintain ≤N live children, reap with
  `waitpid`, propagate worst exit per CR-4. *(R-25)*
- [x] **T9 — Empty-input rule.** Implement CR-3/`-r`. *(R-26)*
- [x] **T10 — Man page.** Write `usr.man/man1/xargs.1` (NAME, SYNOPSIS,
  DESCRIPTION, OPTIONS, EXIT STATUS, EXAMPLES, SEE ALSO, STANDARDS noting
  POSIX.1-2024 + GNU/BSD + BSD-wins). *(R-34)*
- [x] **T11 — Tests.** Write `tests/bin/xargs/test_xargs.sh` oracle-comparing
  the `NATIVE_BUILD=1` host binary to the system `xargs` over: basic, `-n`,
  `-L`, `-s`, `-0`, `-d`, `-I`, `-E`, `-r`, `-t`, exit codes. *(R-35)*
- [x] **T12 — Build + verify.** `make -C bin/xargs` and `… NATIVE_BUILD=1`
  clean; run the test harness; fix to green. *(R-33,R-35)*
