# Substrate `grep` / `egrep` / `fgrep` — Requirements Specification

**Status:** Active · **Owner:** Substrate userland · **Last updated:** 2026-05-30
**Component:** `bin/grep/` (single binary; `egrep`/`fgrep` are install-time symlinks)
**Verification corpus:** `tests/bin/grep/`
**Manual pages:** `usr.man/man1/grep.1`, `egrep.1`, `fgrep.1`

---

## 1. Purpose & Scope

This document specifies a `grep` family implementation for Substrate that is
conformant with **POSIX.1-2024 (IEEE Std 1003.1-2024, Issue 8)** §`grep`, and
that additionally implements the widely-relied-upon **GNU grep** and **BSD
(FreeBSD) grep** extensions.

A single executable provides all three historical interfaces; the active
default is selected from `basename(argv[0])`:

| Invocation | Default pattern dialect | POSIX-equivalent |
| ---------- | ----------------------- | ---------------- |
| `grep`     | Basic Regular Expression (BRE) | `grep`         |
| `egrep`    | Extended Regular Expression (ERE) | `grep -E`   |
| `fgrep`    | Fixed strings (no regex) | `grep -F`        |

`egrep`/`fgrep` are deprecated by POSIX but retained for compatibility; they
behave exactly as `grep -E` / `grep -F` would, including all other options.

### 1.1 Conflict-resolution policy

> **When a documented behavior differs between GNU grep and BSD grep, the BSD
> (FreeBSD) behavior is authoritative.** POSIX-mandated behavior overrides
> both. GNU-only behavior with no BSD counterpart is adopted only where it
> does not contradict BSD.

Concrete consequences of this policy are captured in §6 (Conflict Register).

### 1.2 Engine dependency & deviations

`grep` is built on the in-tree regex engine (`usr.lib/regex`,
`<regex.h>`/`regex_compile`/`regex_match`). The engine supports BRE/ERE
groups, intervals `{n,m}`, alternation, anchors `^`/`$`, bracket expressions
with ranges, the `\d`/`\w`/`\s` shorthands, and — as of the backreference work
— POSIX BRE **back-references** (`\1`…`\9`), with unanchored leftmost matching.
It does **not** natively support:

- POSIX bracket **character classes** (`[[:alpha:]]`, `[[:digit:]]`, …),
  equivalence classes (`[[=a=]]`), or collating symbols (`[[.ch.]]`);
- **word-boundary** assertions (`\<`, `\>`, `\b`).

To meet POSIX requirements without modifying the shared engine, `grep`
performs a **pattern-translation pass** (REQ-GREP-070..074) that rewrites
POSIX character classes inside bracket expressions into explicit ASCII ranges
before compilation, and implements `-w`/`-x` (REQ-GREP-044/045) itself using
match offsets rather than engine assertions. Back-references *are* supported:
when a BRE pattern contains one, the engine compiles it with an `NFA_BACKREF`
node and matches with a bounded backtracking matcher (the DFA/Pike-VM fast
path cannot model the non-regular construct). Locale-based collating and
equivalence classes remain **DEFERRED** (see §7); on the effectively ASCII/C
locale of Substrate this is the only conformance deviation.

---

## 2. Definitions

- **Selected line** — an input line that `grep` decides to act on: a matching
  line, or, under `-v`, a non-matching line.
- **Pattern** — one regular expression or fixed string. The pattern set is the
  union of operands and `-e`/`-f` patterns; a line matches if **any** pattern
  matches (logical OR).
- **Word character** — a member of `[A-Za-z0-9_]`.
- **Line** — a maximal byte sequence delimited by the line terminator. The
  terminator is `<newline>` by default, or NUL under `-z`.
- **Match offset** — a `[start,end)` byte span within a line, as returned by
  `regex_match` capture slot 0.

---

## 3. EARS Requirements

Requirements use EARS templates: **Ubiquitous** ("The system shall …"),
**Event-driven** ("WHEN <trigger> the system shall …"), **State-driven**
("WHILE <state> the system shall …"), **Unwanted** ("IF <condition> THEN the
system shall …"), **Optional** ("WHERE <feature> the system shall …"), and
**Complex** (combinations). Each has an ID, a source standard, and a
verification method **V** ∈ {T = automated test, I = inspection, D =
demonstration}.

### 3.1 Invocation & dialect selection

| ID | Requirement | Src | V |
|----|-------------|-----|---|
| REQ-GREP-001 | The system shall accept the synopsis `grep [OPTIONS] PATTERN [FILE...]` and `grep [OPTIONS] (-e PATTERN \| -f FILE)... [FILE...]`. | POSIX | T |
| REQ-GREP-002 | WHEN no `-e` and no `-f` option is supplied, the system shall treat the first non-option operand as the single pattern. | POSIX | T |
| REQ-GREP-003 | WHEN at least one `-e` or `-f` option is supplied, the system shall treat every non-option operand as a FILE operand. | POSIX | T |
| REQ-GREP-004 | WHILE invoked with `basename(argv[0]) == "egrep"`, the system shall default the dialect to ERE as if `-E` were given. | BSD/GNU | T |
| REQ-GREP-005 | WHILE invoked with `basename(argv[0]) == "fgrep"`, the system shall default the dialect to fixed-string as if `-F` were given. | BSD/GNU | T |
| REQ-GREP-006 | WHEN both a dialect-selecting option and a conflicting program name apply, the system shall honor the explicit option. | BSD | T |
| REQ-GREP-007 | The system shall treat the operand `-` as standard input. | POSIX | T |
| REQ-GREP-008 | WHEN no FILE operand is given, the system shall read standard input. | POSIX | T |
| REQ-GREP-009 | The system shall stop option processing at the first `--` operand and treat all subsequent operands as files. | POSIX | T |

### 3.2 Pattern dialects

| ID | Requirement | Src | V |
|----|-------------|-----|---|
| REQ-GREP-020 | WHERE `-G`/`--basic-regexp` is active (the default for `grep`), the system shall interpret patterns as POSIX Basic Regular Expressions. | POSIX | T |
| REQ-GREP-021 | WHERE `-E`/`--extended-regexp` is active, the system shall interpret patterns as POSIX Extended Regular Expressions. | POSIX | T |
| REQ-GREP-022 | WHERE `-F`/`--fixed-strings` is active, the system shall interpret each pattern as a literal string with no metacharacters. | POSIX | T |
| REQ-GREP-023 | WHEN multiple dialect options are given, the system shall apply the last one specified (BSD last-wins). | BSD | T |
| REQ-GREP-024 | The system shall treat each `<newline>` within a `-e` argument or `-f` file as a pattern separator yielding multiple alternative patterns. | POSIX | T |
| REQ-GREP-025 | WHERE `-F` is active, the system shall match a line if any fixed pattern occurs as a substring (subject to `-w`/`-x`). | POSIX | T |
| REQ-GREP-026 | IF an empty pattern is supplied THEN the system shall treat it as matching every line. | POSIX | T |
| REQ-GREP-027 | WHERE BRE is active, the system shall support back-references `\1`..`\9` matching the text most recently captured by the corresponding group. | POSIX | T |
| REQ-GREP-028 | IF a BRE back-reference names a group that has not been opened THEN the system shall reject the pattern and exit 2. | POSIX | T |
| REQ-GREP-029 | WHERE ERE is active, the system shall treat `\N` as the literal digit N (BSD: ERE has no back-references). | BSD | T |

### 3.3 Pattern sources

| ID | Requirement | Src | V |
|----|-------------|-----|---|
| REQ-GREP-030 | The system shall accept one or more `-e PATTERN` / `--regexp=PATTERN` options, accumulating each into the pattern set. | POSIX | T |
| REQ-GREP-031 | The system shall accept one or more `-f FILE` / `--file=FILE` options, adding each newline-separated line of FILE as a pattern. | POSIX | T |
| REQ-GREP-032 | WHEN the `-f` operand is `-`, the system shall read patterns from standard input. | GNU/BSD | T |
| REQ-GREP-033 | IF a `-f` FILE cannot be opened THEN the system shall report a diagnostic to stderr and exit with status 2. | POSIX | T |
| REQ-GREP-034 | WHEN a `-f` FILE is empty, the system shall match no lines (the pattern set is empty and nothing matches). | GNU | T |

### 3.4 Matching control

| ID | Requirement | Src | V |
|----|-------------|-----|---|
| REQ-GREP-040 | WHERE `-i`/`--ignore-case` is active, the system shall match without regard to ASCII case in both regex and fixed-string modes. | POSIX | T |
| REQ-GREP-041 | WHERE `-v`/`--invert-match` is active, the system shall select lines that do not match any pattern. | POSIX | T |
| REQ-GREP-044 | WHERE `-w`/`--word-regexp` is active, the system shall select a line only if a match is bounded on both sides by a non-word character or a line edge. | POSIX | T |
| REQ-GREP-045 | WHERE `-x`/`--line-regexp` is active, the system shall select a line only if a match spans the entire line. | POSIX | T |
| REQ-GREP-046 | WHEN `-w` and `-x` are combined, the system shall apply `-x` semantics (whole-line) which subsume word boundaries. | BSD | T |

### 3.5 General output control

| ID | Requirement | Src | V |
|----|-------------|-----|---|
| REQ-GREP-050 | WHERE `-c`/`--count` is active, the system shall suppress normal output and write, per file, the count of selected lines. | POSIX | T |
| REQ-GREP-051 | WHERE `-l`/`--files-with-matches` is active, the system shall write only the names of files containing at least one selected line, each once. | POSIX | T |
| REQ-GREP-052 | WHERE `-L`/`--files-without-match` is active, the system shall write only the names of files containing no selected line. | GNU/BSD | T |
| REQ-GREP-053 | WHERE `-m NUM`/`--max-count=NUM` is active, the system shall stop reading a file after NUM selected lines from that file. | GNU/BSD | T |
| REQ-GREP-054 | WHERE `-o`/`--only-matching` is active, the system shall write each non-empty, non-overlapping match on its own output line instead of the whole line. | POSIX | T |
| REQ-GREP-055 | WHERE `-q`/`--quiet`/`--silent` is active, the system shall write nothing to stdout and exit with status 0 as soon as a line is selected. | POSIX | T |
| REQ-GREP-056 | WHERE `-s`/`--no-messages` is active, the system shall suppress diagnostics about nonexistent or unreadable files. | POSIX | T |
| REQ-GREP-057 | WHEN `-c` is combined with `-o`, the system shall count matches rather than selected lines (GNU behavior; BSD lacks a counter-example). | GNU | T |
| REQ-GREP-058 | WHEN `-c` is combined with `-m NUM`, the reported count shall not exceed NUM. | GNU/BSD | T |

### 3.6 Output line prefixing

| ID | Requirement | Src | V |
|----|-------------|-----|---|
| REQ-GREP-060 | WHERE `-n`/`--line-number` is active, the system shall prefix each output line with its 1-based line number within the file. | POSIX | T |
| REQ-GREP-061 | WHERE `-b`/`--byte-offset` is active, the system shall prefix each output line (or match, under `-o`) with its 0-based byte offset within the file. | GNU/BSD | T |
| REQ-GREP-062 | WHEN more than one FILE is searched, or under `-r`, the system shall prefix each output line with the file name followed by `:`. | POSIX | T |
| REQ-GREP-063 | WHERE `-H`/`--with-filename` is active, the system shall always prefix output with the file name even for a single file. | BSD/GNU | T |
| REQ-GREP-064 | WHERE `-h`/`--no-filename` is active, the system shall never prefix output with the file name. | BSD/GNU | T |
| REQ-GREP-065 | WHEN `--label=LABEL` is given, the system shall use LABEL as the presented name for standard input. | GNU | T |
| REQ-GREP-066 | WHEN a prefix accompanies a count (`-c`) or file-name listing (`-l`/`-L`), the system shall use `:` as the separator between name and count and a bare name for listings. | POSIX | T |

### 3.7 Character-class translation (engine-gap mitigation)

| ID | Requirement | Src | V |
|----|-------------|-----|---|
| REQ-GREP-070 | The system shall recognize POSIX character classes `[:alnum:] [:alpha:] [:blank:] [:cntrl:] [:digit:] [:graph:] [:lower:] [:print:] [:punct:] [:space:] [:upper:] [:xdigit:]` inside bracket expressions. | POSIX | T |
| REQ-GREP-071 | WHEN a recognized class appears inside a bracket expression, the system shall substitute the equivalent ASCII range set before compilation. | POSIX | T |
| REQ-GREP-072 | The system shall preserve a leading `]` and `^` negation semantics of bracket expressions during translation. | POSIX | T |
| REQ-GREP-073 | The system shall leave `[:…:]` sequences outside any bracket expression untouched. | POSIX | I |
| REQ-GREP-074 | IF an unknown `[:name:]` class is encountered inside a bracket expression THEN the system shall report an invalid-pattern diagnostic and exit 2. | POSIX | T |

### 3.8 File & directory selection

| ID | Requirement | Src | V |
|----|-------------|-----|---|
| REQ-GREP-080 | WHERE `-r`/`-R`/`--recursive` is active, the system shall search each FILE that is a directory recursively. | GNU/BSD | T |
| REQ-GREP-081 | WHEN `-r` is active and no FILE operand is given, the system shall search the current directory (BSD/GNU behavior). | BSD | T |
| REQ-GREP-082 | WHERE `--include=GLOB` is active during a recursive search, the system shall examine only files whose base name matches GLOB. | GNU/BSD | T |
| REQ-GREP-083 | WHERE `--exclude=GLOB` is active during a recursive search, the system shall skip files whose base name matches GLOB. | GNU/BSD | T |
| REQ-GREP-084 | WHERE `-d ACTION`/`--directories=ACTION` is given with ACTION ∈ {read, skip, recurse}, the system shall handle directory operands accordingly. | GNU/BSD | T |
| REQ-GREP-085 | IF a directory is given without `-r` or `-d recurse` THEN the system shall, by default, skip it with a diagnostic (`-d skip` implied is BSD default: a warning, exit 2 contribution suppressed unless no match). | BSD | T |

### 3.9 Binary file handling

| ID | Requirement | Src | V |
|----|-------------|-----|---|
| REQ-GREP-090 | WHEN a searched file contains a NUL byte within the inspected region, the system shall classify it as binary. | GNU/BSD | T |
| REQ-GREP-091 | WHEN a binary file is selected and no overriding option applies, the system shall, instead of printing matching lines, print `Binary file NAME matches` once and continue. | GNU/BSD | T |
| REQ-GREP-092 | WHERE `-a`/`--text` is active, the system shall process binary files as text. | GNU/BSD | T |
| REQ-GREP-093 | WHERE `-I` is active, the system shall treat a binary file as containing no matches. | GNU/BSD | T |
| REQ-GREP-094 | WHERE `--binary-files=TYPE` is active with TYPE ∈ {binary, text, without-match}, the system shall behave as default / `-a` / `-I` respectively. | GNU | T |

### 3.10 Delimiters, color, info

| ID | Requirement | Src | V |
|----|-------------|-----|---|
| REQ-GREP-100 | WHERE `-z`/`--null-data` is active, the system shall treat input and output line terminators as NUL rather than `<newline>`. | GNU/BSD | T |
| REQ-GREP-101 | WHERE `--color[=WHEN]`/`--colour[=WHEN]` is active with WHEN ∈ {auto, always, never}, the system shall surround matched text with the ANSI SGR sequence when output is enabled. | GNU/BSD | T |
| REQ-GREP-102 | WHEN `--color=auto` is active and stdout is not a terminal, the system shall not emit color sequences. | GNU/BSD | T |
| REQ-GREP-103 | WHEN `--help` is given, the system shall write usage to stdout and exit 0. | GNU/BSD | T |
| REQ-GREP-104 | WHEN `-V`/`--version` is given, the system shall write version information to stdout and exit 0. | GNU/BSD | T |
| REQ-GREP-105 | The system shall accept the shorthand `-NUM` as equivalent to `-C NUM`. | GNU/BSD | T |

### 3.11 Context control

| ID | Requirement | Src | V |
|----|-------------|-----|---|
| REQ-GREP-110 | WHERE `-A NUM`/`--after-context=NUM` is active, the system shall print NUM trailing context lines after each selected line. | GNU/BSD | T |
| REQ-GREP-111 | WHERE `-B NUM`/`--before-context=NUM` is active, the system shall print NUM leading context lines before each selected line. | GNU/BSD | T |
| REQ-GREP-112 | WHERE `-C NUM`/`--context=NUM` is active, the system shall print NUM lines of context on both sides. | GNU/BSD | T |
| REQ-GREP-113 | WHEN context output separates non-adjacent groups, the system shall print a `--` separator line between groups. | GNU/BSD | T |
| REQ-GREP-114 | WHEN context lines are printed, the system shall prefix them with `-` as the name/number separator instead of `:`. | GNU/BSD | T |

### 3.12 Exit status & robustness (Unwanted-behavior)

| ID | Requirement | Src | V |
|----|-------------|-----|---|
| REQ-GREP-120 | WHEN at least one line is selected and no error occurs, the system shall exit with status 0. | POSIX | T |
| REQ-GREP-121 | WHEN no line is selected and no error occurs, the system shall exit with status 1. | POSIX | T |
| REQ-GREP-122 | IF an error occurs (bad option, bad pattern, unreadable file) THEN the system shall exit with status 2, EXCEPT that under `-q` a successful match still yields 0. | POSIX | T |
| REQ-GREP-123 | IF a pattern fails to compile THEN the system shall write a diagnostic naming the pattern and exit 2 before reading input. | POSIX | T |
| REQ-GREP-124 | The system shall handle input lines of arbitrary length bounded only by available memory, growing its line buffer as needed. | POSIX | T |
| REQ-GREP-125 | The system shall correctly match and emit lines containing embedded NUL bytes when `-a`/`-z` make such lines text. | GNU | T |
| REQ-GREP-126 | IF a memory allocation fails THEN the system shall report the failure to stderr and exit 2 without crashing. | INCOSE | I |
| REQ-GREP-127 | WHEN the final line of input lacks a terminator, the system shall still process and (if selected) emit it, appending a terminator on output. | POSIX | T |

### 3.13 Performance & quality (non-functional)

| ID | Requirement | Src | V |
|----|-------------|-----|---|
| REQ-GREP-130 | The system shall search input in a single forward pass with O(lines) line buffering plus O(before-context) retained lines. | INCOSE | I |
| REQ-GREP-131 | The system shall not leak heap allocations across files (verified under the host ASAN test build). | INCOSE | T |
| REQ-GREP-132 | The system shall build for both the Substrate target (dynamic, `libregex.so.0`) and the host (`NATIVE_BUILD=1`) from one source tree. | Project | T |

---

## 4. User Stories

- **US-1 — Developer searching a tree.** As a developer, I want `grep -rn foo .`
  so I can find every occurrence of `foo` with file and line context across a
  directory tree. *(REQ-GREP-080, 060, 062)*
- **US-2 — Sysadmin filtering logs.** As an admin, I want
  `grep -i -e warn -e error /var/log/messages` to OR several case-insensitive
  patterns. *(REQ-GREP-030, 040, 024)*
- **US-3 — Script gating on presence.** As a script author, I want
  `grep -q PATTERN file` to set exit status without output so I can branch on
  it. *(REQ-GREP-055, 120, 121)*
- **US-4 — Extracting tokens.** As a user, I want `grep -oE '[0-9]+' file` to
  print just the numbers, one per line. *(REQ-GREP-054, 021, 070)*
- **US-5 — Counting.** As a user, I want `grep -c PATTERN *.c` to see how many
  matching lines each file has. *(REQ-GREP-050, 062)*
- **US-6 — Whole-word / whole-line.** As a user, I want `grep -w main` and
  `grep -Fx config` to avoid substring false positives. *(REQ-GREP-044, 045)*
- **US-7 — Class-based patterns.** As a user, I want `grep '[[:upper:]]'` to
  work even though the engine has no native class support. *(REQ-GREP-070, 071)*
- **US-8 — Binary safety.** As a user, I want `grep PATTERN /bin/ls` to print
  `Binary file … matches` rather than dump control bytes to my terminal.
  *(REQ-GREP-090, 091)*
- **US-9 — Context.** As a reviewer, I want `grep -C2 TODO src.c` to see two
  lines around each hit. *(REQ-GREP-112, 113)*
- **US-10 — Compatibility.** As a porter, I want `egrep`/`fgrep` to exist and
  behave as `grep -E`/`grep -F` so existing scripts run unmodified.
  *(REQ-GREP-004, 005)*

---

## 5. Acceptance Criteria (traceable)

Every REQ-GREP-* with V=T has at least one case in `tests/bin/grep/`. The
suite is organized as:

- `test_grep_posix.py` — POSIX-mandated behavior (dialects, exit codes,
  `-cevFilnqsvx`, `-`, `--`).
- `test_grep_ext.py` — GNU/BSD extensions (`-o -w -m -A -B -C -r -H -h -b`,
  `--include/--exclude`, `--color`, `-z`, binary handling).
- `test_grep_class.py` — POSIX character-class translation.
- `test_grep_regression.py` — fixed historical bugs.
- ASAN target (`grep_asan`) for REQ-GREP-131.

---

## 6. Conflict Register (BSD wins)

| Topic | GNU | BSD (chosen) | Req |
|-------|-----|--------------|-----|
| Multiple dialect flags | last wins | last wins | REQ-GREP-023 |
| Directory w/o `-r` | warn, exit per `-d` | warn & skip | REQ-GREP-085 |
| `-r` with no file operand | search `.` | search `.` | REQ-GREP-081 |
| `--color` spelling | `--color`/`--colour` | both | REQ-GREP-101 |
| `-w` + `-x` | `-x` wins | `-x` wins | REQ-GREP-046 |

(Most GNU and BSD behaviors coincide; the register lists only the points
examined. Where they coincide the requirement cites both.)

---

## 7. Deferred / Out of scope

- ~~**DEFER-1: BRE back-references `\1`…`\9`.**~~ **DONE** — implemented in
  `usr.lib/regex` via an `NFA_BACKREF` node and a bounded backtracking matcher;
  see REQ-GREP-027..029.
- **DEFER-2: Locale collating symbols `[[.ch.]]` and equivalence classes
  `[[=a=]]`.** Substrate is effectively C/POSIX locale, ASCII; no multichar
  collation. Translation pass rejects them as unsupported.
- **DEFER-3: `--color` of context/line-number fields (GREP_COLORS sub-fields).**
  Only matched text is colored.
- **DEFER-4: Perl `-P` regexps.** Not POSIX; engine PCRE adapter is optional
  and off by default.

---

## 8. LLM-Optimized Actionable Tasklist

> Execute top-to-bottom. Each task is independently checkable; IDs map to the
> requirements above. `[x]` = done.

### Phase A — Spec
- [x] **A1** Author this specification (`docs/specs/grep-spec.md`). *(all)*

### Phase B — Core implementation (`bin/grep/`)
- [ ] **B1** `grep.h`: shared `struct grep_ctx` (all option fields), enums for
  dialect / binary-mode / dir-action / color-when, function prototypes.
- [ ] **B2** `grep.c`: length-aware, terminator-parameterized line reader
  (NUL-safe, unbounded growth, no-final-terminator handling). *(REQ 100,124,127)*
- [ ] **B3** `grep_opts.c`: `-NUM` pre-pass; `getopt_long` table covering the
  full option set; `argv[0]` mode detection; `--` handling; pattern/file
  operand split. *(REQ 001-009, 020-023, 105, 103-104)*
- [ ] **B4** `grep_pattern.c`: pattern accumulation from operands/`-e`/`-f`
  (newline split, `-` stdin, open errors); POSIX class translation pass;
  compile to regex or store fixed; per-line match decision incl. `-i`,`-v`,
  `-w`,`-x`,`-o` offsets. *(REQ 024-034, 040-046, 054, 070-074, 123)*
- [ ] **B5** `grep.c`: per-file processing engine — selection, counting,
  `-l`/`-L`, `-m`, `-q` early-exit, binary detection & modes, context
  ring-buffer with `--` separators, output prefixing (`-H`/`-h`/`-n`/`-b`/
  multi-file/`--label`), `--color`. *(REQ 050-066, 090-094, 101-102, 110-114)*
- [ ] **B6** `grep.c`: file/dir iteration & recursion (`-r`/`-R`, `-d`,
  `--include`/`--exclude` via `fnmatch`, default-dir under `-r`). *(REQ 080-085)*
- [ ] **B7** `grep.c`: exit-status accounting (0/1/2 with `-q`/`-s` rules) and
  allocation-failure paths. *(REQ 120-127, 056)*

### Phase C — Build & symlinks
- [ ] **C1** `bin/grep/Makefile`: multi-file `SRCS`, `egrep`/`fgrep` symlink
  build + install targets, `EXTRA_CLEAN`. *(REQ 004-005, 132)*
- [ ] **C2** Confirm `bin/Makefile` SUBDIRS includes `grep` (egrep/fgrep are
  symlinks, not subdirs).

### Phase D — Documentation
- [ ] **D1** `usr.man/man1/grep.1` full page (all options, EXIT STATUS,
  EXAMPLES, SEE ALSO, conformance note re DEFER-1).
- [ ] **D2** `usr.man/man1/egrep.1`, `fgrep.1` reference stubs.

### Phase E — Verification
- [ ] **E1** `tests/bin/grep/Makefile` (cat-style: build host + hooks + asan).
- [ ] **E2** `test_grep_posix.py`, **E3** `test_grep_ext.py`,
  **E4** `test_grep_class.py`, **E5** `test_grep_regression.py`.
- [ ] **E6** Build `NATIVE_BUILD=1`, run full suite, fix to green.
- [ ] **E7** Build Substrate target (`make -C bin/grep`) clean.
- [ ] **E8** ASAN run for REQ-GREP-131.

### Phase F — Close-out
- [ ] **F1** Commit per logical unit; update `ARCHITECTURE.md`/CHANGELOG if the
  userland surface note warrants it.
