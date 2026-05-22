# Specification — `head(1)` and `tail(1)`

## 1. Document control

| Field | Value |
|---|---|
| Status | Baseline |
| Subjects | `bin/head/`, `bin/tail/` |
| Verification | `tests/bin/head/`, `tests/bin/tail/` |
| Requirement syntax | EARS (Easy Approach to Requirements Syntax) |
| Quality model | INCOSE Guide for Writing Requirements (necessary, singular, unambiguous, verifiable) |

### 1.1 Scope

This specification defines the complete observable behaviour of the
Substrate `head` and `tail` utilities. Both shall be **fully
compliant with POSIX.1-2024 (IEEE Std 1003.1-2024)** and shall, in
addition, implement **all GNU coreutils extensions** and **all
BSD extensions**.

### 1.2 Normative references

- **POSIX**: IEEE Std 1003.1-2024, `head` and `tail` utility pages.
- **GNU**: GNU coreutils 9.x `head`/`tail` (long options, negative
  counts, `--follow=name`, `--retry`, `--pid`, suffixes, obsolete
  packed syntax).
- **BSD**: FreeBSD/OpenBSD `head`/`tail` (`-r`, historic `-NUM`,
  `expand_number(3)` binary suffixes, `-b` blocks).

### 1.3 Conflict-resolution policy

> **R-POLICY**: Where POSIX, GNU and BSD prescribe conflicting
> behaviour, the implementation **shall** adopt the BSD behaviour,
> provided the result remains a strict superset of POSIX-mandated
> behaviour. POSIX-mandated behaviour is never overridden.

Applied resolutions:

| Conflict | POSIX | GNU | BSD | Adopted |
|---|---|---|---|---|
| Suffix `MB` magnitude | n/a | 1000² (decimal) | 1024² (binary) | **BSD** — 1024² |
| `tail -r` | n/a | absent (`tac`) | reverse | **BSD** — provide `-r` |
| `-NUM` historic form | obsolete | obsolete | supported | **BSD** — supported |
| `-b` block unit | n/a | n/a | 512-byte | **BSD** — provide `-b` |
| Suffix case | n/a | case-sensitive | case-insensitive | **BSD** — case-insensitive |

### 1.4 Requirement attributes (INCOSE)

Each requirement carries: a unique **ID**, an **EARS** statement, a
**Source** (POSIX/GNU/BSD), and a **Verification** method (T=test,
A=analysis, I=inspection, D=demonstration).

---

## 2. User stories

### 2.1 `head`

- **US-H1** — As a log analyst, I want `head -n N file` to print the
  first N lines so that I can preview a file without paging it.
- **US-H2** — As a shell scripter, I want `head -c N` to extract an
  exact byte prefix so that I can carve fixed-size records.
- **US-H3** — As a GNU-trained user, I want `head -n -N` to print all
  but the last N lines so that I can drop a known-size trailer.
- **US-H4** — As a BSD-trained user, I want `head -20 file` and
  `head -5k file` to work so that my muscle memory and old scripts
  keep working.
- **US-H5** — As a pipeline author, I want `head` to emit
  `==> name <==` banners for multiple files so that concatenated
  output stays attributable.
- **US-H6** — As a binary-data user, I want `head -z` to treat NUL as
  the line delimiter so that I can preview NUL-separated records.
- **US-H7** — As any user, I want `head --help` / `head --version`
  so that the tool is self-documenting.

### 2.2 `tail`

- **US-T1** — As a log analyst, I want `tail -n N file` to print the
  last N lines so that I can see recent activity.
- **US-T2** — As an operator, I want `tail -f file` to stream new
  data as it is appended so that I can watch a live log.
- **US-T3** — As an operator of a log-rotating service, I want
  `tail -F file` to keep following across rotation and truncation so
  that I do not lose the stream when the file is replaced.
- **US-T4** — As a POSIX scripter, I want `tail -n +N` to start
  output at line N so that I can skip a fixed header.
- **US-T5** — As a supervisor process, I want `tail --pid=PID -f` to
  terminate when the producer dies so that watchers do not leak.
- **US-T6** — As a BSD-trained user, I want `tail -r` to print lines
  last-first so that I can read a file newest-first.
- **US-T7** — As a tuning-conscious operator, I want
  `--sleep-interval` and `--max-unchanged-stats` so that I can trade
  latency against syscall load.

---

## 3. Requirements — `head` (HD-*)

### 3.1 Invocation & operands

- **HD-001** *(ubiquitous)* — The `head` utility **shall** accept the
  synopsis `head [-c number | -n number] [file...]`.
  *Source: POSIX. Verify: T.*
- **HD-002** *(event-driven)* — When no `file` operand is given, the
  `head` utility **shall** read the standard input.
  *Source: POSIX. Verify: T.*
- **HD-003** *(event-driven)* — When a `file` operand is `-`, the
  `head` utility **shall** read the standard input for that operand.
  *Source: POSIX. Verify: T.*
- **HD-004** *(event-driven)* — When `--` is encountered, the `head`
  utility **shall** treat all following arguments as operands.
  *Source: POSIX. Verify: T.*

### 3.2 Counting modes

- **HD-010** *(ubiquitous)* — The `head` utility **shall** default to
  printing the first 10 lines of each input.
  *Source: POSIX. Verify: T.*
- **HD-011** *(event-driven)* — When `-n number` is given, the `head`
  utility **shall** print the first `number` lines.
  *Source: POSIX. Verify: T.*
- **HD-012** *(event-driven)* — When `-c number` is given, the `head`
  utility **shall** print the first `number` bytes.
  *Source: POSIX. Verify: T.*
- **HD-013** *(event-driven)* — When `-n -N` (negative) is given, the
  `head` utility **shall** print all but the last `N` lines.
  *Source: GNU. Verify: T.*
- **HD-014** *(event-driven)* — When `-c -N` (negative) is given, the
  `head` utility **shall** print all but the last `N` bytes.
  *Source: GNU. Verify: T.*
- **HD-015** *(event-driven)* — When the count is `0`, the `head`
  utility **shall** produce no data output and **shall** exit `0`.
  *Source: POSIX. Verify: T.*
- **HD-016** *(unwanted)* — If both `-n` and `-c` are given, then the
  `head` utility **shall** report an error to standard error and
  exit non-zero. *Source: POSIX. Verify: T.*

### 3.3 Long options & GNU/BSD extras

- **HD-020** — The `head` utility **shall** accept `--lines=[-]N` as a
  synonym for `-n` and `--bytes=[-]N` as a synonym for `-c`.
  *Source: GNU. Verify: T.*
- **HD-021** — The `head` utility **shall** accept `-q`/`--quiet`/
  `--silent` to suppress file banners.
  *Source: GNU. Verify: T.*
- **HD-022** — The `head` utility **shall** accept `-v`/`--verbose` to
  force file banners. *Source: GNU. Verify: T.*
- **HD-023** — The `head` utility **shall** accept `-z`/
  `--zero-terminated`, making NUL the line delimiter.
  *Source: GNU. Verify: T.*
- **HD-024** — The `head` utility **shall** accept `--help` and
  `--version`, printing to standard output and exiting `0`.
  *Source: GNU. Verify: T.*
- **HD-025** *(event-driven)* — When an argument matches `-NUM` (a
  leading digit), the `head` utility **shall** interpret it as the
  historic line count. *Source: BSD. Verify: T.*
- **HD-026** *(event-driven)* — When an argument matches the obsolete
  packed form `-NUM[bkm][clqv]`, the `head` utility **shall** honour
  it (`b`=×512, `k`=×1024, `m`=×1048576; `c`=bytes, `l`=lines,
  `q`=quiet, `v`=verbose). *Source: GNU obsolete. Verify: T.*

### 3.4 Suffixes

- **HD-030** *(where feature)* — Where a numeric argument carries a
  suffix, the `head` utility **shall** accept (case-insensitively)
  `K M G T P E` as binary multipliers (1024^n) and **shall** accept
  the IEC (`KiB`…) and decimal-SI (`kB`…) spellings as aliases of
  the **same binary value**. *Source: BSD `expand_number`. Verify: T.*
- **HD-031** *(unwanted)* — If a numeric argument is malformed or
  overflows `int64_t`, then the `head` utility **shall** report an
  error and exit non-zero. *Source: INCOSE-derived. Verify: T.*

### 3.5 Banners

- **HD-040** *(state-driven)* — While more than one input is
  processed and banners are not suppressed, the `head` utility
  **shall** precede each input's data with `==> name <==`.
  *Source: POSIX. Verify: T.*
- **HD-041** — The `head` utility **shall** print a newline before
  every banner except the first. *Source: POSIX. Verify: T.*
- **HD-042** — The `head` utility **shall** render the standard-input
  banner name as `standard input` (no parentheses).
  *Source: GNU/BSD. Verify: T.*

### 3.6 Errors & exit status

- **HD-050** — The `head` utility **shall** exit `0` when all inputs
  are processed without error. *Source: POSIX. Verify: T.*
- **HD-051** *(unwanted)* — If any input cannot be opened or read,
  then the `head` utility **shall** write a diagnostic to standard
  error, continue with remaining operands, and exit non-zero.
  *Source: POSIX. Verify: T.*
- **HD-052** *(unwanted)* — If a write to standard output fails, then
  the `head` utility **shall** exit non-zero.
  *Source: POSIX. Verify: A.*

---

## 4. Requirements — `tail` (TL-*)

### 4.1 Invocation & operands

- **TL-001** *(ubiquitous)* — The `tail` utility **shall** accept the
  synopsis `tail [-f] [-c number | -n number] [file...]`.
  *Source: POSIX. Verify: T.*
- **TL-002** — TL-002…TL-004 mirror HD-002…HD-004 (stdin default,
  `-` operand, `--` terminator). *Source: POSIX. Verify: T.*

### 4.2 Counting modes

- **TL-010** *(ubiquitous)* — The `tail` utility **shall** default to
  printing the last 10 lines of each input.
  *Source: POSIX. Verify: T.*
- **TL-011** *(event-driven)* — When `-n number` is given, the `tail`
  utility **shall** print the last `number` lines.
  *Source: POSIX. Verify: T.*
- **TL-012** *(event-driven)* — When `-c number` is given, the `tail`
  utility **shall** print the last `number` bytes.
  *Source: POSIX. Verify: T.*
- **TL-013** *(event-driven)* — When a count is prefixed with `+`,
  the `tail` utility **shall** start output at that line/byte
  (origin 1). *Source: POSIX. Verify: T.*
- **TL-014** *(unwanted)* — If a `+` count is `+0`, then the `tail`
  utility **shall** report an error and exit non-zero (origin is 1).
  *Source: INCOSE-derived. Verify: T.*
- **TL-015** *(event-driven)* — When `-b number` is given, the `tail`
  utility **shall** count in 512-byte blocks. *Source: BSD. Verify: T.*
- **TL-016** *(unwanted)* — If incompatible count modes (`-b`/`-c`/
  `-n`) are combined, then the `tail` utility **shall** error and
  exit non-zero. *Source: INCOSE-derived. Verify: T.*

### 4.3 Follow

- **TL-020** *(state-driven)* — While `-f`/`--follow[=descriptor]` is
  in effect, the `tail` utility **shall**, after the initial output,
  keep the descriptor open and emit data as it is appended.
  *Source: POSIX/GNU. Verify: D.*
- **TL-021** *(state-driven)* — While `--follow=name` is in effect,
  the `tail` utility **shall** track the file by path and reopen it
  when the inode/device changes (rotation).
  *Source: GNU. Verify: D.*
- **TL-022** — The `tail` utility **shall** treat `-F` as equivalent
  to `--follow=name --retry`. *Source: GNU. Verify: D.*
- **TL-023** *(event-driven)* — When a followed file is truncated,
  the `tail` utility **shall** resume from offset 0.
  *Source: GNU. Verify: D.*
- **TL-024** *(state-driven)* — While `--pid=PID` is in effect with
  follow, the `tail` utility **shall** terminate once all named PIDs
  have exited. *Source: GNU. Verify: D.*
- **TL-025** — The `tail` utility **shall** poll at
  `--sleep-interval` seconds (default 1.0) and **shall** honour
  `--max-unchanged-stats` (default 5) for `--follow=name` reopen
  checks. *Source: GNU. Verify: A.*
- **TL-026** *(event-driven)* — When standard input is a FIFO/pipe,
  the `tail` utility **shall not** enter the follow loop on it.
  *Source: GNU. Verify: A.*

### 4.4 BSD reverse & extras

- **TL-030** *(event-driven)* — When `-r` is given, the `tail`
  utility **shall** emit lines (or bytes, with `-c`) in reverse
  order. *Source: BSD. Verify: T.*
- **TL-031** *(unwanted)* — If `-r` is combined with `-f`/`-F`, then
  the `tail` utility **shall** error and exit non-zero.
  *Source: BSD-derived. Verify: T.*
- **TL-032** — The `tail` utility **shall** accept `-q`/`-v`/`-z`,
  `--quiet`/`--silent`/`--verbose`/`--zero-terminated` with the same
  meaning as `head`. *Source: GNU. Verify: T.*
- **TL-033** — The `tail` utility **shall** accept the historic
  `-NUM` and packed `-NUM[bcl][f]` forms.
  *Source: BSD/GNU obsolete. Verify: T.*
- **TL-034** — The `tail` utility **shall** accept `--help` and
  `--version`. *Source: GNU. Verify: T.*

### 4.5 Suffixes, banners, errors

- **TL-040** — Suffix handling **shall** match HD-030, plus the GNU
  lowercase `b` = ×512. *Source: GNU/BSD. Verify: T.*
- **TL-041** — Banner rules **shall** match HD-040…HD-042.
  *Source: POSIX. Verify: T.*
- **TL-050** — Exit-status rules **shall** match HD-050…HD-052.
  *Source: POSIX. Verify: T.*

### 4.6 Performance

- **TL-060** *(state-driven)* — While the input is seekable, the
  `tail` utility **shall** locate the tail by seeking, not by
  buffering the whole file. *Source: INCOSE-derived. Verify: A.*
- **TL-061** *(state-driven)* — While the input is a non-seekable
  stream, the `tail` utility **shall** bound `-c` memory to the
  requested byte count. *Source: INCOSE-derived. Verify: A.*

---

## 5. Verification matrix (summary)

| Group | Reqs | Method |
|---|---|---|
| head invocation/modes/options | HD-001…HD-031 | T |
| head banners/errors | HD-040…HD-052 | T, A |
| tail invocation/modes | TL-001…TL-016 | T |
| tail follow | TL-020…TL-026 | D, A |
| tail reverse/extras/perf | TL-030…TL-061 | T, A |

---

## 6. LLM-optimized actionable tasklist

Audit performed against `bin/head/head.c` and `bin/tail/*.c` at
baseline. Each task is independently executable and verifiable.

### 6.1 `head`

- [ ] **TASK-H1 — Fix stdin banner text.** In `bin/head/head.c`,
  change the standard-input banner name from `(standard input)` to
  `standard input` (HD-042). Both the no-operand path and the `-`
  operand path. *Verify: `printf '' | head -v` shows
  `==> standard input <==`.*
- [ ] **TASK-H2 — Audit-confirm HD-001…HD-031, HD-040, HD-041,
  HD-050, HD-051** already satisfied; add any missing test cases to
  `tests/bin/head/test_head.sh`.
- [ ] **TASK-H3 — Regression test pass.** Run `tests/bin/head/`.

### 6.2 `tail`

- [ ] **TASK-T1 — Fix stdin banner text.** In `bin/tail/tail_main.c`
  (and `tail_follow.c`), change `(standard input)` to
  `standard input` (TL-041).
- [ ] **TASK-T2 — Audit-confirm TL-001…TL-061** already satisfied;
  extend `tests/bin/tail/test_tail.sh` with `+N`, `-b`, `-r`,
  suffix and mode-conflict cases.
- [ ] **TASK-T3 — Regression test pass.** Run `tests/bin/tail/`.

### 6.3 Baseline audit result

The pre-existing implementations already satisfy the large majority
of HD-* and TL-* requirements (modes, negative/`+` counts, follow,
follow-name, retry, `-r`, `--pid`, suffixes, packed/historic forms,
`--help`/`--version`, exit status). The only behavioural
non-conformance found is the standard-input banner text
(`(standard input)` vs the GNU/BSD `standard input`), addressed by
TASK-H1 and TASK-T1.
