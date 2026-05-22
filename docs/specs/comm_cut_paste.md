# Specification — `comm(1)`, `cut(1)`, `paste(1)`

## 1. Document control

| Field | Value |
|---|---|
| Status | Baseline |
| Subjects | `bin/comm/`, `bin/cut/`, `bin/paste/` |
| Verification | `tests/bin/comm/`, `tests/bin/cut/`, `tests/bin/paste/` |
| Requirement syntax | EARS (Easy Approach to Requirements Syntax) |
| Quality model | INCOSE Guide for Writing Requirements |

### 1.1 Scope

Defines the complete observable behaviour of the Substrate `comm`,
`cut` and `paste` utilities. All three shall be **fully compliant
with POSIX.1-2024 (IEEE Std 1003.1-2024)** and shall additionally
implement **all GNU coreutils extensions** and **all BSD
extensions**.

### 1.2 Normative references

- **POSIX**: IEEE Std 1003.1-2024 — `comm`, `cut`, `paste` pages.
- **GNU**: GNU coreutils 9.x `comm`/`cut`/`paste`.
- **BSD**: FreeBSD/OpenBSD `comm`/`cut`/`paste`.

### 1.3 Conflict-resolution policy

> **R-POLICY**: Where POSIX, GNU and BSD prescribe conflicting
> behaviour, the implementation **shall** adopt the BSD behaviour,
> provided the result remains a strict superset of POSIX-mandated
> behaviour.

Applied resolutions:

| Conflict | POSIX | GNU | BSD | Adopted |
|---|---|---|---|---|
| `comm` input-order checking | undefined if unsorted | errors unless `--nocheck-order` | never checks | **BSD** — no error by default; `--check-order` opt-in |
| `cut` whitespace fields | n/a | n/a | `-w` flag | **BSD** — provide `-w` |
| `comm` case-insensitive | n/a | absent | `-i` flag | **BSD** — provide `-i` |
| Field/column delimiter default | tab | tab | tab | tab (no conflict) |

### 1.4 Requirement attributes

Each requirement: a unique **ID**, an **EARS** statement, a
**Source** (POSIX/GNU/BSD), and a **Verification** method
(T=test, A=analysis, I=inspection).

---

## 2. User stories

### 2.1 `comm`
- **US-CM1** — As a data analyst, I want `comm file1 file2` to show
  lines unique to each file and lines in common, so I can diff two
  sorted sets.
- **US-CM2** — As a scripter, I want `comm -12 a b` to print only
  the common lines, so I can intersect two sets.
- **US-CM3** — As a scripter, I want `comm -23 a b` to print lines
  only in the first file, so I can subtract one set from another.
- **US-CM4** — As a GNU user, I want `--output-delimiter` and
  `--total` so I can reformat and summarise the comparison.
- **US-CM5** — As a BSD user, I want `-i` so the comparison ignores
  case.

### 2.2 `cut`
- **US-CT1** — As a log processor, I want `cut -f N -d:` to extract
  a field from delimited records.
- **US-CT2** — As a scripter, I want `cut -c RANGE` / `cut -b RANGE`
  to extract character/byte columns from fixed-width data.
- **US-CT3** — As a GNU user, I want `--complement` to keep
  everything *except* the selected list.
- **US-CT4** — As a BSD user, I want `-w` to split on runs of
  whitespace without specifying a delimiter.
- **US-CT5** — As a scripter, I want `-s` to drop lines that contain
  no delimiter, so non-records are filtered out.

### 2.3 `paste`
- **US-PA1** — As a scripter, I want `paste a b` to merge files
  side by side into tab-separated columns.
- **US-PA2** — As a scripter, I want `paste -d,` to choose the
  column separator.
- **US-PA3** — As a scripter, I want `paste -s` to join all lines of
  each file into a single line.

---

## 3. Requirements — `comm` (CM-*)

### 3.1 Invocation
- **CM-001** *(ubiquitous)* — `comm` **shall** accept the synopsis
  `comm [-123] file1 file2`. *POSIX. T.*
- **CM-002** *(event-driven)* — When a `file` operand is `-`, `comm`
  **shall** read standard input for it. *POSIX. T.*
- **CM-003** *(unwanted)* — If other than two file operands are
  given, then `comm` **shall** error and exit non-zero. *POSIX. T.*

### 3.2 Comparison & output
- **CM-010** *(ubiquitous)* — `comm` **shall** read both inputs as
  sorted line streams and produce three columns: lines only in
  file1, lines only in file2, lines in both. *POSIX. T.*
- **CM-011** — Column 2 **shall** be prefixed by one delimiter when
  column 1 is not suppressed; column 3 **shall** be prefixed by one
  delimiter per non-suppressed lower column. *POSIX. T.*
- **CM-012** *(event-driven)* — When `-1`/`-2`/`-3` is given, `comm`
  **shall** suppress column 1/2/3 respectively. *POSIX. T.*
- **CM-013** — The default column delimiter **shall** be a tab.
  *POSIX. T.*
- **CM-014** — `comm` **shall** detect equal lines only when they
  are byte-identical (or, with `-i`, case-folded-identical).
  *POSIX/BSD. T.*

### 3.3 Extensions
- **CM-020** *(where feature)* — Where `-i` is given, `comm`
  **shall** compare lines case-insensitively. *BSD. T.*
- **CM-021** — `comm` **shall** accept `--output-delimiter=STR`,
  replacing the tab between columns with `STR`. *GNU. T.*
- **CM-022** — `comm` **shall** accept `--total`, printing a final
  line `count1<delim>count2<delim>countboth<delim>total`. *GNU. T.*
- **CM-023** — `comm` **shall** accept `--check-order` and
  `--nocheck-order`; the default **shall** be `--nocheck-order`
  (BSD). *GNU/BSD. T.*
- **CM-024** *(event-driven)* — When `--check-order` is in effect
  and an input is found to be unsorted, `comm` **shall** write a
  diagnostic and exit non-zero. *GNU. T.*
- **CM-025** — `comm` **shall** accept `-z`/`--zero-terminated`
  (NUL line delimiter), `--help` and `--version`. *GNU. T.*

---

## 4. Requirements — `cut` (CT-*)

### 4.1 Invocation & modes
- **CT-001** *(ubiquitous)* — `cut` **shall** accept the synopses
  `cut -b list [-n] [file...]`, `cut -c list [file...]`,
  `cut -f list [-d delim] [-s] [file...]`. *POSIX. T.*
- **CT-002** *(unwanted)* — If none of `-b`/`-c`/`-f` is given, or
  more than one is, then `cut` **shall** error and exit non-zero.
  *POSIX. T.*
- **CT-003** *(event-driven)* — When a `file` operand is `-` or
  absent, `cut` **shall** read standard input. *POSIX. T.*

### 4.2 List parsing
- **CT-010** — `cut` **shall** parse a `list` as comma-separated
  ranges, each `N`, `N-M`, `N-` (N to end) or `-M` (start to M),
  1-based, in any order, with overlaps merged. *POSIX. T.*
- **CT-011** *(unwanted)* — If a list element is malformed, has a
  zero index, or has decreasing range bounds, then `cut` **shall**
  error and exit non-zero. *POSIX. T.*

### 4.3 Byte/character mode
- **CT-020** *(state-driven)* — While in `-b` or `-c` mode, `cut`
  **shall** output the selected byte/character positions of each
  line in ascending position order. *POSIX. T.*
- **CT-021** — `cut` **shall** accept `-n` with `-b` (no effect in
  the C locale). *POSIX. I.*

### 4.4 Field mode
- **CT-030** *(state-driven)* — While in `-f` mode, `cut` **shall**
  split each line on the delimiter and output the selected fields
  separated by the delimiter. *POSIX. T.*
- **CT-031** — The default field delimiter **shall** be a tab; `-d`
  **shall** set it to its (single-byte) argument. *POSIX. T.*
- **CT-032** *(event-driven)* — When a line contains no delimiter,
  `cut` **shall** output the whole line, unless `-s` is given, in
  which case the line **shall** be omitted. *POSIX. T.*
- **CT-033** *(where feature)* — Where `-w` is given, `cut`
  **shall** treat runs of whitespace as a single field delimiter
  and **shall** ignore `-d`. *BSD. T.*

### 4.5 Extensions
- **CT-040** — `cut` **shall** accept `--complement`, selecting the
  positions/fields *not* in the list. *GNU. T.*
- **CT-041** — `cut` **shall** accept `--output-delimiter=STR`,
  used between emitted fields/ranges in place of the input
  delimiter. *GNU. T.*
- **CT-042** — `cut` **shall** accept `-z`/`--zero-terminated`, the
  long option spellings (`--bytes`/`--characters`/`--fields`/
  `--delimiter`/`--only-delimited`), `--help` and `--version`.
  *GNU. T.*

---

## 5. Requirements — `paste` (PA-*)

- **PA-001** *(ubiquitous)* — `paste` **shall** accept the synopsis
  `paste [-s] [-d list] [file...]`. *POSIX. T.*
- **PA-002** *(event-driven)* — When a `file` operand is `-` or
  absent, `paste` **shall** read standard input. *POSIX. T.*
- **PA-010** *(state-driven)* — While in the default (parallel)
  mode, `paste` **shall** read one line from each file in turn and
  emit them joined by the delimiter, stopping when every file is at
  EOF; an exhausted file contributes an empty column. *POSIX. T.*
- **PA-011** *(state-driven)* — While `-s` is given, `paste`
  **shall** join all lines of each file into one output line,
  separately per file. *POSIX. T.*
- **PA-012** — The default delimiter **shall** be a tab; `-d list`
  **shall** supply a delimiter list cycled per column. *POSIX. T.*
- **PA-013** — In a `-d` list, `paste` **shall** interpret the
  escapes `\n`, `\t`, `\\` and `\0` (empty/no separator). *POSIX. T.*
- **PA-020** — `paste` **shall** accept `-z`/`--zero-terminated`,
  `--delimiters`, `--help` and `--version`. *GNU. T.*

---

## 6. Common requirements (all three)

- **CX-001** — Each utility **shall** exit `0` on success and `>0`
  on any error, writing diagnostics to standard error. *POSIX. T.*
- **CX-002** *(unwanted)* — If an input file cannot be opened, the
  utility **shall** report it, continue with remaining operands
  where the semantics allow, and exit non-zero. *POSIX. T.*
- **CX-003** — `--` **shall** terminate option processing. *POSIX. T.*
- **CX-004** — `--help`/`--version` **shall** print to standard
  output and exit `0`. *GNU. T.*

---

## 7. LLM-optimized actionable tasklist

All three utilities are **greenfield** (no prior implementation).

### 7.1 `comm`
- [ ] **TASK-CM1** — Create `bin/comm/comm.c` + `bin/comm/Makefile`.
  Implement CM-001…CM-025. Stream both inputs line-by-line
  (`getdelim`), 3-way merge, suppression mask, configurable
  delimiter, `-i`, `--total`, `--check-order`, `-z`.
- [ ] **TASK-CM2** — Add `tests/bin/comm/test_comm.sh`.

### 7.2 `cut`
- [ ] **TASK-CT1** — Create `bin/cut/cut.c` + `bin/cut/Makefile`.
  Implement CT-001…CT-042. Range-list parser with merge,
  byte/char/field modes, `-d`/`-s`/`-n`/`-w`, `--complement`,
  `--output-delimiter`, `-z`.
- [ ] **TASK-CT2** — Add `tests/bin/cut/test_cut.sh`.

### 7.3 `paste`
- [ ] **TASK-PA1** — Create `bin/paste/paste.c` + `bin/paste/Makefile`.
  Implement PA-001…PA-020. Parallel + serial modes, cycled
  delimiter list with escape decoding, `-z`.
- [ ] **TASK-PA2** — Add `tests/bin/paste/test_paste.sh`.

### 7.4 Integration
- [ ] **TASK-INT1** — Add `comm`, `cut`, `paste` to `bin/Makefile`
  `SUBDIRS` (alphabetical).
- [ ] **TASK-INT2** — Build all three for the substrate target;
  confirm `make -C bin/comm`, `bin/cut`, `bin/paste` pass.
- [ ] **TASK-INT3** — Run the three test suites (host `NATIVE_BUILD`)
  and confirm green.
