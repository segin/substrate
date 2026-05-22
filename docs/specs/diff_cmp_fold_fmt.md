# Specification — `diff(1)`, `cmp(1)`, `fold(1)`, `fmt(1)`

## 1. Document control

| Field | Value |
|---|---|
| Status | Baseline |
| Subjects | `bin/diff/`, `bin/cmp/`, `bin/fold/`, `bin/fmt/` |
| Verification | `tests/bin/{diff,cmp,fold,fmt}/` |
| Requirement syntax | EARS (Easy Approach to Requirements Syntax) |
| Quality model | INCOSE Guide for Writing Requirements |

### 1.1 Scope

Defines the complete observable behaviour of the Substrate `diff`,
`cmp`, `fold` and `fmt` utilities. Each shall be **fully compliant
with POSIX.1-2024 (IEEE Std 1003.1-2024)** where that standard
specifies the utility, and shall additionally implement **all GNU
extensions** and **all BSD extensions**. (`fmt` is not a POSIX
utility; it is specified here from the BSD and GNU definitions.)

### 1.2 Normative references

- **POSIX**: IEEE Std 1003.1-2024 — `diff`, `cmp`, `fold` pages.
- **GNU**: GNU diffutils 3.x (`diff`, `cmp`); GNU coreutils 9.x
  (`fold`, `fmt`).
- **BSD**: FreeBSD / OpenBSD `diff`, `cmp`, `fold`, `fmt`.

### 1.3 Conflict-resolution policy

> **R-POLICY**: Where POSIX, GNU and BSD prescribe conflicting
> behaviour, the implementation **shall** adopt the BSD behaviour,
> provided the result remains a strict superset of POSIX-mandated
> behaviour.

Applied resolutions:

| Conflict | POSIX | GNU | BSD | Adopted |
|---|---|---|---|---|
| `cmp -l` radix | octal | octal | octal (`-x` = hex) | **BSD** — octal default, add `-x` |
| `cmp` size pre-check | n/a | n/a | `-z` | **BSD** — provide `-z` |
| `fmt` width spec | n/a | `-w`/`-g` | positional `goal [max]` | **BSD** — positional operands; also accept `-w`/`-g` |
| `fmt -p` | n/a | `-p STRING` (prefix filter) | `-p` (indented paragraphs) | **BSD** — `-p` takes no argument |
| `diff` default format | normal | normal | normal | identical |
| `diff -s` | absent | report identical | report identical | **BSD/GNU** — provide `-s` |

### 1.4 Common requirements

- **R-C1**: Each utility shall exit `0`, `1`, `2` for, respectively,
  no-difference / difference-found / error — except `fold` and `fmt`
  which exit `0` on success and `>0` only on error.
- **R-C2**: When an operand is `-`, the utility shall read standard
  input in its place.
- **R-C3**: When given `--help`, each utility shall print a usage
  summary to standard output and exit `0`.
- **R-C4**: When given `--version`, each utility shall print
  `<name> (Substrate) <ver>` to standard output and exit `0`.
- **R-C5**: If an input file cannot be opened, then the utility
  shall write a diagnostic to standard error naming the file and the
  `strerror` reason, and shall exit `2` (`diff`/`cmp`) or `>0`
  (`fold`/`fmt`).
- **R-C6**: The utility shall treat `--` as the end-of-options
  delimiter.

---

## 2. `cmp(1)` — compare two files byte by byte

### 2.1 Synopsis

```
cmp [-l|-s|-x] [-bhz] [-i skip|-i skip1:skip2] [-n limit]
    file1 file2 [skip1 [skip2]]
```

### 2.2 EARS requirements

- **R-CMP1**: The `cmp` utility shall compare `file1` and `file2`
  byte by byte.
- **R-CMP2**: While neither `-l`, `-s` nor `-x` is in effect, when
  the first differing byte is found, `cmp` shall write
  `<file1> <file2> differ: char <N>, line <L>` to standard output
  and exit `1`, where `N` and `L` are 1-based.
- **R-CMP3**: Where `-l` is given, for every differing byte position
  `cmp` shall write `<N> <oct1> <oct2>` (decimal position, octal
  byte values) and shall not stop at the first difference.
- **R-CMP4**: Where `-x` is given, `cmp` shall behave as for `-l`
  but emit byte values in hexadecimal. *(BSD)*
- **R-CMP5**: Where `-b` is given, `cmp` shall additionally print the
  differing bytes as characters. *(GNU; BSD `-b` print-bytes)*
- **R-CMP6**: Where `-s` is given, `cmp` shall produce no output and
  shall report the result through the exit status only.
- **R-CMP7**: Where `-i skip` is given, `cmp` shall skip `skip`
  bytes of both inputs; where `-i skip1:skip2` is given it shall
  skip `skip1` of `file1` and `skip2` of `file2`. *(GNU)*
- **R-CMP8**: Where the optional `skip1`/`skip2` operands are given,
  `cmp` shall skip that many bytes of the corresponding file; an
  operand offset may carry a `k`/`M`/`G` (1024-based) suffix.
- **R-CMP9**: Where `-n limit` is given, `cmp` shall compare at most
  `limit` bytes. *(GNU)*
- **R-CMP10**: Where `-z` is given, `cmp` shall first compare the
  two file sizes and, if they differ, report the difference without
  reading content. *(BSD)*
- **R-CMP11**: If, with neither `-l`/`-x` active, EOF is reached on
  one file before the other, then `cmp` shall write
  `cmp: EOF on <file> ...` to standard error and exit `1`.
- **R-CMP12**: If the two inputs are byte-for-byte identical over
  the compared range, then `cmp` shall produce no output and exit
  `0`.
- **R-CMP13**: Where `-h` is given, `cmp` shall not dereference a
  symbolic-link operand. *(BSD; degrades to no-op without `lstat`
  semantics)*

### 2.3 User stories

- *As a build engineer*, I want `cmp -s a b` so a script can branch
  on whether two artefacts are identical.
- *As a forensic analyst*, I want `cmp -l` / `cmp -x` to enumerate
  every differing offset in octal or hex.
- *As a packager*, I want `cmp -z` to reject mismatched files
  cheaply when only the size needs checking.

---

## 3. `fold(1)` — fold long lines

### 3.1 Synopsis

```
fold [-bs] [-w width | -width] [file...]
```

### 3.2 EARS requirements

- **R-FOLD1**: The `fold` utility shall break input lines so no
  output line exceeds `width` columns (default `80`).
- **R-FOLD2**: While `-b` is not in effect, `fold` shall account
  column width specially: `<tab>` advances to the next multiple of
  8, `<backspace>` decrements the count by one (not below zero),
  and `<carriage-return>` resets the count to zero.
- **R-FOLD3**: Where `-b` is given, `fold` shall count bytes and
  shall break exactly at `width` bytes, disabling R-FOLD2.
- **R-FOLD4**: Where `-s` is given, when a line must be broken
  `fold` shall break at the last `<blank>` within `width`, if one
  exists, so words are not split.
- **R-FOLD5**: Where `-w width` is given, `fold` shall use `width`;
  the obsolete `-width` (a leading-digit option) shall be accepted
  as an equivalent. *(POSIX legacy + BSD)*
- **R-FOLD6**: When an output line reaches `width`, `fold` shall
  emit a `<newline>` and continue the same logical line.
- **R-FOLD7**: If `width` is not a positive integer, then `fold`
  shall write a diagnostic to standard error and exit `>0`.
- **R-FOLD8**: The `fold` utility shall preserve input `<newline>`
  characters (an embedded newline always ends an output line).

### 3.3 User stories

- *As a terminal user*, I want `fold -w 72` to wrap a wide log file
  to my screen.
- *As a typesetter*, I want `fold -s -w 65` to wrap prose without
  splitting words.
- *As a tooling author*, I want `fold -b` to chunk binary-ish data
  at exact byte boundaries.

---

## 4. `fmt(1)` — simple text formatter

### 4.1 Synopsis

```
fmt [-cmnps] [-d chars] [-l num] [-t num] [-w width] [-g goal]
    [goal [maximum]] [file...]
```

### 4.2 EARS requirements

- **R-FMT1**: The `fmt` utility shall reflow each paragraph of input
  so that lines approach but do not gratuitously exceed the goal
  width (default `65`); the hard maximum defaults to `goal + 10`.
- **R-FMT2**: The `fmt` utility shall treat one or more blank lines
  as a paragraph separator and shall reproduce blank lines verbatim.
- **R-FMT3**: The `fmt` utility shall collapse runs of inter-word
  whitespace within a reflowed paragraph to a single space, and
  shall place two spaces after a sentence-ending character.
- **R-FMT4**: Where the positional `goal` operand is given, `fmt`
  shall use it as the goal width; where `maximum` is also given it
  shall use that as the hard limit. *(BSD)*
- **R-FMT5**: Where `-w width` or `-g goal` is given, `fmt` shall
  honour it equivalently to the positional operands. *(GNU)*
- **R-FMT6**: Where `-s` is given, `fmt` shall only split lines
  longer than the goal and shall never join short lines. *(BSD+GNU)*
- **R-FMT7**: Where `-c` is given, `fmt` shall use crown-margin
  mode: the indentation of the first two lines of a paragraph is
  preserved, the first for the first output line and the second for
  all subsequent output lines. *(BSD+GNU)*
- **R-FMT8**: Where `-p` is given, `fmt` shall allow indented
  paragraphs — a change of indentation starts a new paragraph and
  the indentation is preserved. *(BSD)*
- **R-FMT9**: Where `-t num` is given, `fmt` shall treat output tab
  stops as `num` columns wide; where `-l num` is given it shall
  treat input tab stops as `num` columns wide. *(BSD)*
- **R-FMT10**: Where `-n` is given, `fmt` shall also reformat lines
  beginning with `.` (normally passed through untouched). *(BSD)*
- **R-FMT11**: Where `-m` is given, `fmt` shall attempt to preserve
  the layout of mail-header lines. *(BSD)*
- **R-FMT12**: The `fmt` utility shall preserve, as the per-line
  prefix of every output line of a paragraph, the leading
  whitespace common to that paragraph's input.

### 4.3 User stories

- *As a writer*, I want `fmt -w 72 notes.txt` to reflow my prose to
  a consistent width.
- *As an email user*, I want `fmt -c` to rewrap a quoted reply while
  keeping the hanging indent.
- *As a script*, I want `fmt -s` to wrap over-long lines without
  joining anything that was deliberately short.

---

## 5. `diff(1)` — compare two files line by line

### 5.1 Synopsis

```
diff [-c|-C n|-e|-f|-u|-U n|-q|-n|--normal] [-abdilrstwBN]
     [-I regexp] [--label name] file1 file2
diff [options] dir1 dir2          (with -r)
```

### 5.2 EARS requirements

- **R-DIFF1**: The `diff` utility shall compute a minimal line-based
  edit script transforming `file1` into `file2`, using the Myers
  O(ND) shortest-edit-script algorithm.
- **R-DIFF2**: While no output-format option is in effect, `diff`
  shall emit the **normal** format: `<l1>[,<l2>]{a|c|d}<r1>[,<r2>]`
  command lines, `<` lines for deletions, `>` lines for additions,
  and `---` separating the two halves of a change.
- **R-DIFF3**: Where `-u` (or `-U n`) is given, `diff` shall emit
  the **unified** format with `--- `/`+++ ` headers, `@@ -a,b +c,d @@`
  hunk headers and `n` lines of context (default `3`).
- **R-DIFF4**: Where `-c` (or `-C n`) is given, `diff` shall emit
  the **context** format with `*** `/`--- ` headers, `***`/`---`
  section markers and `n` lines of context (default `3`).
- **R-DIFF5**: Where `-e` is given, `diff` shall emit an **ed
  script** that, fed to `ed`, recreates `file2` from `file1`.
- **R-DIFF6**: Where `-f` is given, `diff` shall emit a forward
  ed-style script (commands in file order, not reverse).
- **R-DIFF7**: Where `-n` is given, `diff` shall emit an RCS-style
  `a`/`d` script with line counts. *(BSD/GNU)*
- **R-DIFF8**: Where `-q` is given, `diff` shall report only
  `Files <f1> and <f2> differ` and produce no per-line output.
- **R-DIFF9**: Where `-s` is given and the inputs are identical,
  `diff` shall report `Files <f1> and <f2> are identical`. *(BSD)*
- **R-DIFF10**: Where `-r` is given and both operands are
  directories, `diff` shall compare like-named entries recursively,
  reporting `Only in <dir>: <name>` for entries present in just one.
- **R-DIFF11**: Where `-N` is given, `diff` shall treat an absent
  file (in a recursive comparison) as an empty file rather than
  skipping it. *(GNU)*
- **R-DIFF12**: Where `-b` is given, `diff` shall treat runs of
  `<blank>` as equal and ignore trailing blanks when comparing.
- **R-DIFF13**: Where `-w` is given, `diff` shall ignore all
  `<blank>` characters when comparing lines.
- **R-DIFF14**: Where `-i` is given, `diff` shall compare lines
  case-insensitively.
- **R-DIFF15**: Where `-B` is given, `diff` shall ignore changes
  whose lines are all blank. *(GNU)*
- **R-DIFF16**: Where `-I regexp` is given, `diff` shall ignore
  changes all of whose lines match `regexp`. *(GNU/BSD)*
- **R-DIFF17**: Where `-a` is given, `diff` shall treat all files as
  text; otherwise, if a file appears to be binary, `diff` shall
  report `Binary files <f1> and <f2> differ` and not diff content.
- **R-DIFF18**: Where `-t` is given, `diff` shall expand tabs to
  spaces in the output; where `-l` (`--paginate`) is requested the
  requirement degrades to a no-op absent a pager.
- **R-DIFF19**: Where `--label name` is given, `diff` shall use
  `name` in place of the file name in `-u`/`-c` headers.
- **R-DIFF20**: When the inputs are identical, `diff` shall produce
  no output (unless `-s`) and exit `0`; when they differ it shall
  exit `1`; on error it shall exit `2`.
- **R-DIFF21**: If a `-` operand is given for at most one of the two
  files, then `diff` shall read that file from standard input.
- **R-DIFF22**: When a missing final newline is present in either
  input, `diff` shall annotate the output
  (`\ No newline at end of file`).

### 5.3 User stories

- *As a developer*, I want `diff -u old new` to produce a patch I
  can feed to `patch(1)`.
- *As a reviewer*, I want `diff -r -q dir1 dir2` for a fast list of
  which files changed across two trees.
- *As a release manager*, I want `diff -rN release-a release-b` so
  added and removed files are shown as full insertions/deletions.
- *As a config auditor*, I want `diff -bwi` to ignore cosmetic
  whitespace and case noise.

---

## 6. Verification matrix

| Utility | Test driver | Key cases |
|---|---|---|
| `cmp`  | `tests/bin/cmp/test_cmp.sh`   | identical, first-diff, `-l`, `-x`, `-s`, `-i`, `-n`, `-z`, skip operands, EOF |
| `fold` | `tests/bin/fold/test_fold.sh` | width wrap, `-s`, `-b`, tab columns, `-width`, embedded newlines |
| `fmt`  | `tests/bin/fmt/test_fmt.sh`   | reflow, blank-line paragraphs, `-s`, `-c`, `-p`, goal operand, prefix |
| `diff` | `tests/bin/diff/test_diff.sh` | normal/`-u`/`-c`/`-e`/`-n`, `-q`, `-s`, `-r`, `-N`, `-b`/`-w`/`-i`, exit codes |

---

## 7. Actionable tasklist (LLM-optimized)

> Execute top to bottom. Each utility is one `bin/<tool>/` directory
> with `<tool>.c` + a 6-line `Makefile` (`PROG`/`SRCS`/`DYNAMIC=1` +
> the two `include` lines, per `bin/comm/Makefile`). After each
> tool: add its name to `bin/Makefile` `SUBDIRS` (alphabetical),
> add `tests/bin/<tool>/test_<tool>.sh`, host-compile and run.

- [x] **T1 — spec**: this document. *(done)*
- [x] **T2 — cmp**: implement R-CMP1..13. Byte loop over two
  `FILE*`; `-l`/`-x`/`-s` modes; `-i`/skip-operand seeking with
  `k`/`M`/`G` suffix parsing; `-n` limit; `-z` size pre-check;
  EOF-on-one-file diagnostic; exit `0/1/2`.
- [x] **T3 — fold**: implement R-FOLD1..8. Per-char loop with a
  column counter (tab/bs/cr aware unless `-b`); `-s` deferred-blank
  break; `-w`/`-width` parsing; newline passthrough.
- [x] **T4 — fmt**: implement R-FMT1..12. Paragraph splitter on
  blank lines (and on indentation change under `-p`); word packer
  to goal/maximum; `-s` split-only; `-c` crown margin; common-prefix
  preservation; positional `goal [max]` and `-w`/`-g`.
- [x] **T5 — diff**: implement R-DIFF1..22. Stages:
  (a) read both files into line-pointer arrays, with `-b/-w/-i`
      producing a normalised key per line for comparison;
  (b) Myers O(ND) edit script over the key arrays;
  (c) coalesce the script into hunks;
  (d) emitters: normal, unified (`-u`/`-U`), context (`-c`/`-C`),
      ed (`-e`), forward-ed (`-f`), RCS (`-n`), brief (`-q`);
  (e) `-r` directory walk with `Only in` + `-N` empty-file
      substitution; `-s` identical report; `-a`/binary detection;
  (f) exit `0/1/2`.
- [x] **T6 — wire-in**: `bin/Makefile` `SUBDIRS` gains `cmp`,
  `diff`, `fold`, `fmt` in alphabetical position.
- [x] **T7 — verify**: host-compile every `.c` with `cc -std=c2x`,
  run all four `tests/bin/*/test_*.sh`, then `make -C bin` for the
  four new directories with the target toolchain.
- [x] **T8 — strings / strip**: no new code — `/usr/bin/strings`
  and `/usr/bin/strip` are the GNU Binutils 2.46.0 builds produced
  by `contrib/binutils/` (stage-2). Confirm both are present on the
  image and are the binutils binaries.
- [x] **T9 — commit**: commit spec + the four utilities + tests.
