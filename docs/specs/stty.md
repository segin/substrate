# Specification — `stty(1)`, Substrate Base System

**Status:** Active
**Component:** `bin/stty/`
**Conformance target:** POSIX.1-2024 (IEEE Std 1003.1-2024) `stty`, plus
the GNU coreutils and 4.4BSD/FreeBSD extension sets.
**Conflict rule:** Where a GNU extension and a BSD extension specify
incompatible behaviour for the same operand or option, **the BSD
behaviour is normative**. Additive extensions from either side are
both adopted.

---

## 1. Purpose & Scope

`stty` reports and changes the line settings of a terminal device.
This document specifies the externally observable behaviour of the
Substrate `stty` utility as an INCOSE-style requirements set written
in EARS (Easy Approach to Requirements Syntax), a set of user
stories, an operand reference catalogue, a conformance/verification
matrix, and an executable task list.

### 1.1 Normative references

- IEEE Std 1003.1-2024, *stty* utility and the *General Terminal
  Interface* (`termios`).
- GNU coreutils `stty` (extension operands and `-g` save form).
- FreeBSD `stty(1)` (grouped verbose output, `gfmt1` save form,
  `-e`, `-f`).
- Substrate `<termios.h>` (`include/termios.h`) — the userspace
  `termios` surface this utility is bound to.

### 1.2 Conformance classes

Every requirement and operand is tagged with one or more class
codes:

| Code | Meaning |
|------|---------|
| `P`  | Mandated by POSIX.1-2024. |
| `G`  | GNU coreutils extension. |
| `B`  | BSD / FreeBSD extension. |

### 1.3 Substrate implementation-status codes

| Code     | Meaning |
|----------|---------|
| `IMPL`   | Implemented; the backing `termios` bit / `c_cc` index / ioctl exists in the Substrate userspace ABI. |
| `GATED`  | The backing constant exists only in the kernel `termios` header, not the userspace one; deferred until the userspace header exposes it. |
| `DEFER`  | The backing `termios` field, flag bit, or `c_cc` index does not exist in the Substrate `termios` ABI at all; deferred until the ABI and the console line discipline are extended. |

`IMPL` requirements are binding on this delivery. `GATED` and
`DEFER` requirements are recorded for completeness, are **not**
implemented by this delivery, and are enumerated in §10.

---

## 2. Actors

| Actor | Description |
|-------|-------------|
| Interactive user | A person at a shell adjusting their terminal. |
| Shell script | A script saving/restoring or constraining tty state. |
| Init / getty / login | System programs that set a known tty state. |
| Test harness | Automated verification of this utility. |

---

## 3. User Stories

- **US-01** — As an interactive user, I want `stty` with no
  arguments to show the settings that differ from normal, so that I
  can see at a glance what is unusual about my terminal.
- **US-02** — As an interactive user, I want `stty -a` to show
  every setting, so that I can inspect the complete terminal state.
- **US-03** — As a shell script, I want `stty -g` to print a
  single machine-readable token and `stty <token>` to restore it,
  so that I can save the tty state, change it, and put it back
  exactly.
- **US-04** — As an interactive user, I want `stty sane` to repair
  a terminal wedged by a crashed full-screen program.
- **US-05** — As an interactive user, I want `stty raw` / `-raw` /
  `cooked` to flip the terminal between character-at-a-time and
  line-at-a-time input.
- **US-06** — As an interactive user, I want `stty -echo` /
  `stty echo` so that I can suppress or restore input echo, e.g.
  around a password prompt.
- **US-07** — As an interactive user, I want `stty size` to print
  the terminal's row and column count for use by other programs.
- **US-08** — As an interactive user, I want to rebind control
  characters (`stty intr ^C`, `stty erase ^?`, `stty susp undef`).
- **US-09** — As a shell script, I want `stty -F <device>` /
  `stty -f <device>` to operate on a terminal other than my own
  standard input.
- **US-10** — As a system program, I want a reliable non-zero exit
  status whenever a setting could not be applied, so that I can
  detect failure.
- **US-11** — As an interactive user, I want familiar operand
  spellings from both GNU and BSD systems to be accepted, so that
  muscle memory and existing scripts keep working.
- **US-12** — As an interactive user, I want `stty cols`/`rows` to
  update the kernel's idea of the window size so that resize-aware
  programs are notified.

---

## 4. Functional Requirements (EARS)

Notation: each requirement has an ID, a class tag, and a status
tag, followed by an EARS-form sentence. "The utility" denotes the
Substrate `stty` program.

### 4.1 Invocation & argument model — `STTY-INV-*`

- **STTY-INV-001** *(P, IMPL)* — The utility shall accept the
  invocation forms `stty [-a|-g|-e] [-F device | -f device]` and
  `stty [-F device | -f device] operand...`.
- **STTY-INV-002** *(P, IMPL)* — The utility shall treat every
  argument up to but not including the first non-option argument as
  an option, and shall treat the argument `--` as an explicit end
  of options.
- **STTY-INV-003** *(P, G, IMPL)* — When the utility is invoked
  with no operands and no `-a`/`-g`/`-e` option, the utility shall
  report the terminal settings in abbreviated form (§4.3).
- **STTY-INV-004** *(P, G, B, IMPL)* — Where the utility encounters
  an operand beginning with `-` that is not `-a`, `-g`, `-e`, `-f`,
  `-F`, or `--`, the utility shall treat that operand as a setting
  request, not as an option.
- **STTY-INV-005** *(G, IMPL)* — When the utility is invoked with
  `--help`, the utility shall write a usage summary to standard
  output and exit with status 0.
- **STTY-INV-006** *(G, IMPL)* — If `-a` (or `-e`) and `-g` are
  both supplied, then the utility shall write a diagnostic to
  standard error and exit with status > 0.
- **STTY-INV-007** *(P, IMPL)* — If an information option (`-a`,
  `-g`, `-e`) is supplied together with one or more setting
  operands, then the utility shall write a diagnostic to standard
  error and exit with status > 0.

### 4.2 Device selection — `STTY-DEV-*`

- **STTY-DEV-001** *(P, IMPL)* — Where no device option is given,
  the utility shall operate on the terminal open on file
  descriptor 0 (standard input).
- **STTY-DEV-002** *(G, IMPL)* — When `-F device` or
  `--file device` is given, the utility shall operate on the named
  device instead of standard input.
- **STTY-DEV-003** *(B, IMPL)* — When `-f device` is given, the
  utility shall operate on the named device instead of standard
  input.
- **STTY-DEV-004** *(G, B, IMPL)* — When opening a device option
  argument, the utility shall open it without acquiring it as a
  controlling terminal and without blocking on carrier
  (`O_NONBLOCK | O_NOCTTY`).
- **STTY-DEV-005** *(P, IMPL)* — If the device cannot be opened, or
  the target file descriptor is not a terminal, then the utility
  shall write a diagnostic to standard error and exit with status
  > 0.
- **STTY-DEV-006** *(G, IMPL)* — If more than one device option is
  given, then the utility shall use the last one.

### 4.3 Reporting — abbreviated form — `STTY-RPT-*`

- **STTY-RPT-001** *(P, IMPL)* — When reporting in abbreviated
  form, the utility shall write a leading line containing the line
  speed.
- **STTY-RPT-002** *(P, IMPL)* — When reporting in abbreviated
  form, the utility shall write only those control characters and
  mode flags whose current value differs from the "sane" reference
  (§4.10).
- **STTY-RPT-003** *(P, IMPL)* — When reporting, the utility shall
  render each enabled boolean mode as its name and each disabled
  boolean mode as its name prefixed by `-`.
- **STTY-RPT-004** *(P, IMPL)* — When reporting a control
  character, the utility shall render a value in the range 1..31 as
  `^` followed by the value + 0x40, the value 127 as `^?`, a
  printable value as the literal character, the disabled value as
  `<undef>`, and any other value in hexadecimal.
- **STTY-RPT-005** *(P, IMPL)* — When reporting, the utility shall
  fold output so that no line exceeds 80 columns, breaking only
  between whitespace-separated tokens.

### 4.4 Reporting — verbose form (`-a`, `-e`) — `STTY-VRB-*`

- **STTY-VRB-001** *(G, B, IMPL)* — When `-a` or `-e` is given, the
  utility shall report every supported setting regardless of
  whether it differs from the "sane" reference.
- **STTY-VRB-002** *(B, IMPL)* — When reporting in verbose form,
  the utility shall write a header line of the form
  `speed <n> baud; <rows> rows; <cols> columns;` (BSD layout, per
  the conflict rule §1).
- **STTY-VRB-003** *(B, IMPL)* — When reporting in verbose form,
  the utility shall group mode flags under the labels `lflags:`,
  `iflags:`, `oflags:`, and `cflags:`, and control characters
  under the label `cchars:` (BSD grouped layout, per §1).
- **STTY-VRB-004** *(B, IMPL)* — When reporting in verbose form,
  the utility shall render the character size as one of `cs5`,
  `cs6`, `cs7`, `cs8` within the `cflags:` group.

### 4.5 Reporting — save form (`-g`) — `STTY-SAV-*`

- **STTY-SAV-001** *(G, B, IMPL)* — When `-g` or `--save` is given,
  the utility shall write to standard output a single
  whitespace-free token from which the complete `termios` state
  can be reconstructed.
- **STTY-SAV-002** *(B, IMPL)* — The save token shall use the BSD
  `gfmt1:` format (per the conflict rule §1):
  `gfmt1:cflag=%x:iflag=%x:lflag=%x:oflag=%x:` followed by each
  control character as `name=%x:`, then `ispeed=%d:ospeed=%d`.
- **STTY-SAV-003** *(G, B, IMPL)* — When the first operand has the
  syntactic shape of a save token, the utility shall parse it and
  apply the reconstructed `termios` state.
- **STTY-SAV-004** *(B, IMPL)* — The utility shall accept its own
  `gfmt1:` output as a save-token operand, such that
  `stty $(stty -g)` is an identity operation.
- **STTY-SAV-005** *(P, IMPL)* — If a save-token operand is
  malformed, then the utility shall write a diagnostic to standard
  error and exit with status > 0 without modifying the terminal.

### 4.6 Control modes (`c_cflag`) — `STTY-CM-*`

- **STTY-CM-001** *(P, IMPL)* — When given `parenb` / `-parenb`,
  `parodd` / `-parodd`, `cstopb` / `-cstopb`, `cread` / `-cread`,
  `clocal` / `-clocal`, or `hupcl` / `-hupcl`, the utility shall
  set or clear the corresponding `c_cflag` bit.
- **STTY-CM-002** *(P, IMPL)* — When given `cs5`, `cs6`, `cs7`, or
  `cs8`, the utility shall set the `CSIZE` field of `c_cflag` to
  the named character size.
- **STTY-CM-003** *(B, IMPL)* — The utility shall accept `hup` as
  a synonym for `hupcl`.
- **STTY-CM-004** *(G, B, GATED)* — `crtscts` / `-crtscts`
  (hardware flow control) is gated on userspace exposure of
  `CRTSCTS`.
- **STTY-CM-005** *(B, DEFER)* — `ctsflow`, `rtsflow`, `dtrflow`,
  `dsrflow`, `mdmbuf`, `cdtrcts` are deferred (no backing bits).

### 4.7 Input modes (`c_iflag`) — `STTY-IM-*`

- **STTY-IM-001** *(P, IMPL)* — When given any of `ignbrk`,
  `brkint`, `ignpar`, `parmrk`, `inpck`, `istrip`, `inlcr`,
  `igncr`, `icrnl`, `ixon`, `ixany`, `ixoff` with or without a
  leading `-`, the utility shall set or clear the corresponding
  `c_iflag` bit.
- **STTY-IM-002** *(G, IMPL)* — When given `imaxbel` / `-imaxbel`,
  `iuclc` / `-iuclc`, or `iutf8` / `-iutf8`, the utility shall set
  or clear the corresponding `c_iflag` bit.
- **STTY-IM-003** *(B, IMPL)* — The utility shall accept `tandem`
  as a synonym for `ixoff` and `decctlq` handling per §4.11.

### 4.8 Output modes (`c_oflag`) — `STTY-OM-*`

- **STTY-OM-001** *(P, IMPL)* — When given any of `opost`,
  `onlcr`, `ocrnl`, `onocr`, `onlret`, `ofill`, `ofdel` with or
  without a leading `-`, the utility shall set or clear the
  corresponding `c_oflag` bit.
- **STTY-OM-002** *(G, IMPL)* — When given `olcuc` / `-olcuc`, the
  utility shall set or clear the `OLCUC` bit of `c_oflag`.
- **STTY-OM-003** *(P, DEFER)* — The output-delay operands
  (`cr0`..`cr3`, `nl0`..`nl1`, `tab0`..`tab3`, `bs0`..`bs1`,
  `ff0`..`ff1`, `vt0`..`vt1`) are deferred (no `NLDLY`/`CRDLY`/
  `TABDLY`/`BSDLY`/`VTDLY`/`FFDLY` masks in the Substrate ABI).
- **STTY-OM-004** *(G, B, GATED)* — `oxtabs` / `tabs` / `-tabs` /
  `tab3` (tab expansion) is gated on userspace exposure of
  `OXTABS`.

### 4.9 Local modes (`c_lflag`) — `STTY-LM-*`

- **STTY-LM-001** *(P, IMPL)* — When given any of `isig`,
  `icanon`, `iexten`, `echo`, `echoe`, `echok`, `echonl`,
  `noflsh`, `tostop` with or without a leading `-`, the utility
  shall set or clear the corresponding `c_lflag` bit.
- **STTY-LM-002** *(G, IMPL)* — When given any of `echoctl`,
  `echoprt`, `echoke`, `flusho`, `pendin` with or without a
  leading `-`, the utility shall set or clear the corresponding
  `c_lflag` bit.
- **STTY-LM-003** *(G, B, IMPL)* — The utility shall accept the
  GNU/BSD readability aliases `crterase` (=`echoe`), `crtkill`
  (=`echoke`), `ctlecho` (=`echoctl`), `prterase` (=`echoprt`).
- **STTY-LM-004** *(P, B, GATED)* — `xcase` / `-xcase` is gated on
  userspace exposure of `XCASE`.
- **STTY-LM-005** *(B, G, DEFER)* — `extproc`, `altwerase`,
  `nokerninfo` are deferred (no backing bits).

### 4.10 Control characters (`c_cc`) — `STTY-CC-*`

- **STTY-CC-001** *(P, IMPL)* — When given a control-character
  operand (`eof`, `eol`, `erase`, `intr`, `kill`, `quit`, `susp`,
  `start`, `stop`) followed by a value argument, the utility shall
  store the parsed value into the corresponding `c_cc` index.
- **STTY-CC-002** *(G, IMPL)* — The utility shall additionally
  accept the operands `eol2`, `swtch`, `werase`, `rprnt`, `lnext`,
  and `discard`.
- **STTY-CC-003** *(B, IMPL)* — The utility shall accept the BSD
  spellings `reprint` (=`rprnt`) and `flush` (=`discard`).
- **STTY-CC-004** *(P, IMPL)* — When parsing a control-character
  value, the utility shall interpret `^X` as the control value of
  `X`, `^?` as 127, `^-` and `undef` and the empty string as the
  disabled value, a `0x`-prefixed string as hexadecimal, a
  `0`-prefixed string as octal, a decimal-digit string as decimal,
  and any single remaining character as its literal byte value.
- **STTY-CC-005** *(P, IMPL)* — When given `min <n>` or
  `time <n>`, the utility shall store `n` into `c_cc[VMIN]` or
  `c_cc[VTIME]` respectively.
- **STTY-CC-006** *(B, DEFER)* — `erase2`, `dsusp`, and `status`
  are deferred (no `VERASE2`/`VDSUSP`/`VSTATUS` indices).
- **STTY-CC-007** *(P, IMPL)* — If a control-character operand is
  the last argument with no following value, then the utility
  shall write a diagnostic to standard error and exit with status
  > 0.

### 4.11 Combination settings — `STTY-CMB-*`

- **STTY-CMB-001** *(P, IMPL)* — When given `sane`, the utility
  shall set the mode flags and control characters of the terminal
  to the "sane" reference defined in §5, leaving line speed
  unchanged.
- **STTY-CMB-002** *(P, B, IMPL)* — When given `raw` (or
  `-cooked`), the utility shall place the terminal in raw mode;
  when given `-raw` (or `cooked`), the utility shall place the
  terminal in cooked mode.
- **STTY-CMB-003** *(P, IMPL)* — When given `cbreak`, the utility
  shall clear `ICANON`; when given `-cbreak`, the utility shall set
  `ICANON`.
- **STTY-CMB-004** *(P, IMPL)* — When given `nl`, the utility
  shall clear `ICRNL` and `ONLCR`; when given `-nl`, the utility
  shall set `ICRNL` and `ONLCR`.
- **STTY-CMB-005** *(P, IMPL)* — When given `ek`, the utility
  shall reset the `erase` and `kill` control characters to their
  "sane" reference values.
- **STTY-CMB-006** *(P, IMPL)* — When given `evenp` or `parity`,
  the utility shall enable parity with 7-bit characters and even
  parity; when given `oddp`, the utility shall enable parity with
  7-bit characters and odd parity; when given `-parity`, `-evenp`,
  or `-oddp`, the utility shall disable parity and select 8-bit
  characters.
- **STTY-CMB-007** *(G, IMPL)* — When given `crt`, the utility
  shall enable `echoe`, `echoctl`, and `echoke`.
- **STTY-CMB-008** *(G, IMPL)* — When given `dec`, the utility
  shall enable `echoe`, `echoctl`, `echoke`, clear `ixany`, and
  set `intr`=`^C`, `erase`=`^?`, `kill`=`^U`.
- **STTY-CMB-009** *(G, IMPL)* — When given `pass8`, the utility
  shall clear `parenb` and `istrip` and select `cs8`; when given
  `-pass8`, the utility shall set `parenb` and `istrip` and select
  `cs7`.
- **STTY-CMB-010** *(G, IMPL)* — When given `litout`, the utility
  shall clear `parenb`, `istrip`, `opost` and select `cs8`; when
  given `-litout`, the utility shall set `parenb`, `istrip`,
  `opost` and select `cs7`.
- **STTY-CMB-011** *(G, IMPL)* — When given `decctlq`, the utility
  shall clear `IXANY`; when given `-decctlq`, the utility shall set
  `IXANY`.
- **STTY-CMB-012** *(P, B, GATED)* — `lcase` / `LCASE` / `-lcase`
  is gated on `XCASE` (it manipulates `XCASE`, `IUCLC`, `OLCUC`
  together).
- **STTY-CMB-013** *(P, IMPL)* — If a combination keyword that
  cannot be negated is supplied with a leading `-`, then the
  utility shall write a diagnostic and exit with status > 0.

### 4.12 Line speed — `STTY-SPD-*`

- **STTY-SPD-001** *(P, IMPL)* — When given a bare unsigned
  decimal-integer operand, the utility shall set both the input
  and output line speed to that value.
- **STTY-SPD-002** *(G, B, IMPL)* — When given `ispeed <n>` or
  `ospeed <n>`, the utility shall set the input or output line
  speed respectively to `n`.
- **STTY-SPD-003** *(B, IMPL)* — When `speed` is the sole operand,
  the utility shall write the current output line speed to
  standard output and exit with status 0.
- **STTY-SPD-004** *(P, IMPL)* — When reporting, if input and
  output speed are equal the utility shall print a single `speed`
  field; otherwise it shall print separate `ispeed` and `ospeed`
  fields.

### 4.13 Window size — `STTY-WSZ-*`

- **STTY-WSZ-001** *(G, B, IMPL)* — When `size` is the sole
  operand, the utility shall write the terminal's row count and
  column count, space-separated, to standard output.
- **STTY-WSZ-002** *(G, IMPL)* — When given `rows <n>`,
  `cols <n>`, or `columns <n>`, the utility shall update the
  terminal's window size accordingly via `TIOCSWINSZ`.
- **STTY-WSZ-003** *(G, IMPL)* — When `-a` or `-e` is given, the
  utility shall include the row and column counts in the verbose
  report.
- **STTY-WSZ-004** *(P, IMPL)* — If the window-size query ioctl
  fails, then the utility shall continue and report a row/column
  count of 0 rather than aborting.

### 4.14 Apply semantics — `STTY-APL-*`

- **STTY-APL-001** *(P, IMPL)* — When applying setting operands,
  the utility shall process all operands in command-line order
  into a single working `termios` value and apply that value once.
- **STTY-APL-002** *(P, IMPL)* — When applying the working
  `termios` value, the utility shall use `tcsetattr` with the
  `TCSADRAIN` action.
- **STTY-APL-003** *(P, IMPL)* — If applying the `termios` value
  fails, then the utility shall write a diagnostic to standard
  error and exit with status > 0.
- **STTY-APL-004** *(G, IMPL)* — The utility shall accept `drain`
  and `-drain` operands; `drain` selects `TCSADRAIN` apply
  semantics and `-drain` selects `TCSANOW` apply semantics.

### 4.15 Errors, diagnostics, exit status — `STTY-ERR-*`

- **STTY-ERR-001** *(P, IMPL)* — The utility shall exit with
  status 0 when all requested reporting and setting operations
  succeed.
- **STTY-ERR-002** *(P, IMPL)* — The utility shall exit with a
  status > 0 when any requested operation fails.
- **STTY-ERR-003** *(P, IMPL)* — If an unrecognised operand is
  encountered, then the utility shall write a diagnostic naming
  that operand to standard error and exit with status > 0.
- **STTY-ERR-004** *(P, IMPL)* — Every diagnostic shall be written
  to standard error, prefixed with the program name `stty`.
- **STTY-ERR-005** *(P, IMPL)* — If a numeric operand value is not
  a valid integer, then the utility shall write a diagnostic to
  standard error and exit with status > 0.

---

## 5. The "sane" Reference

`STTY-DEF-001` *(P, IMPL)* — The utility shall define the "sane"
terminal state as:

- `c_iflag` = `BRKINT | ICRNL | IMAXBEL | IXON`
- `c_oflag` = `OPOST | ONLCR`
- `c_cflag` = current value with `CSIZE|PARENB|PARODD|CSTOPB`
  cleared, then `CS8 | CREAD` set (speed and `HUPCL`/`CLOCAL`
  preserved)
- `c_lflag` = `ISIG | ICANON | ECHO | ECHOE | ECHOK | ECHOCTL |
  ECHOKE | IEXTEN`
- `c_cc`: `intr`=`^C`(3), `quit`=`^\`(28), `erase`=`^?`(127),
  `kill`=`^U`(21), `eof`=`^D`(4), `eol`=`<undef>`(0),
  `eol2`=`<undef>`(0), `swtch`=`<undef>`(0), `start`=`^Q`(17),
  `stop`=`^S`(19), `susp`=`^Z`(26), `rprnt`=`^R`(18),
  `werase`=`^W`(23), `lnext`=`^V`(22), `discard`=`^O`(15),
  `min`=1, `time`=0
- line speed: unchanged from the current value.

The abbreviated report (§4.3) and the `sane` combination keyword
(§4.11) both derive from this single reference definition.

---

## 6. Operand Reference Catalogue

Class: `P`/`G`/`B`. Status: `IMPL`/`GATED`/`DEFER`.

### 6.1 Options

| Option | Class | Status | Effect |
|--------|-------|--------|--------|
| `-a`, `--all` | G,B | IMPL | Verbose report (grouped, BSD layout). |
| `-e` | B | IMPL | Verbose report (synonym of `-a`). |
| `-g`, `--save` | G,B | IMPL | `gfmt1:` save token. |
| `-F dev`, `--file dev` | G | IMPL | Operate on `dev`. |
| `-f dev` | B | IMPL | Operate on `dev`. |
| `--help` | G | IMPL | Usage to stdout, exit 0. |
| `--` | P | IMPL | End of options. |

### 6.2 Control-mode operands (`c_cflag`)

| Operand | Class | Status |
|---------|-------|--------|
| `[-]parenb` `[-]parodd` `[-]cstopb` `[-]cread` `[-]clocal` `[-]hupcl` `[-]hup` | P/B | IMPL |
| `cs5` `cs6` `cs7` `cs8` | P | IMPL |
| `[-]crtscts` | G,B | GATED |
| `[-]ctsflow` `[-]rtsflow` `[-]dtrflow` `[-]dsrflow` `[-]mdmbuf` `[-]cdtrcts` | B | DEFER |

### 6.3 Input-mode operands (`c_iflag`)

| Operand | Class | Status |
|---------|-------|--------|
| `[-]ignbrk` `[-]brkint` `[-]ignpar` `[-]parmrk` `[-]inpck` `[-]istrip` `[-]inlcr` `[-]igncr` `[-]icrnl` `[-]ixon` `[-]ixany` `[-]ixoff` | P | IMPL |
| `[-]imaxbel` `[-]iuclc` `[-]iutf8` | G | IMPL |
| `[-]tandem` (=`ixoff`) | B | IMPL |

### 6.4 Output-mode operands (`c_oflag`)

| Operand | Class | Status |
|---------|-------|--------|
| `[-]opost` `[-]onlcr` `[-]ocrnl` `[-]onocr` `[-]onlret` `[-]ofill` `[-]ofdel` | P | IMPL |
| `[-]olcuc` | G | IMPL |
| `[-]oxtabs` `[-]tabs` `tab3` | G,B | GATED |
| `cr0..cr3` `nl0..nl1` `tab0..tab3` `bs0..bs1` `ff0..ff1` `vt0..vt1` | P | DEFER |

### 6.5 Local-mode operands (`c_lflag`)

| Operand | Class | Status |
|---------|-------|--------|
| `[-]isig` `[-]icanon` `[-]iexten` `[-]echo` `[-]echoe` `[-]echok` `[-]echonl` `[-]noflsh` `[-]tostop` | P | IMPL |
| `[-]echoctl` `[-]echoprt` `[-]echoke` `[-]flusho` `[-]pendin` | G | IMPL |
| `crterase` `crtkill` `ctlecho` `prterase` (aliases) | G,B | IMPL |
| `[-]xcase` | P,B | GATED |
| `[-]extproc` `[-]altwerase` `[-]nokerninfo` | G,B | DEFER |

### 6.6 Control-character operands (`c_cc`)

| Operand | Class | Status |
|---------|-------|--------|
| `eof` `eol` `erase` `intr` `kill` `quit` `susp` `start` `stop` | P | IMPL |
| `eol2` `swtch` `werase` `rprnt` `lnext` `discard` | G | IMPL |
| `reprint` (=`rprnt`) `flush` (=`discard`) | B | IMPL |
| `min` `time` | P | IMPL |
| `erase2` `dsusp` `status` | B | DEFER |

### 6.7 Special / numeric operands

| Operand | Class | Status |
|---------|-------|--------|
| `<number>` (bare speed) | P | IMPL |
| `ispeed <n>` `ospeed <n>` | G,B | IMPL |
| `speed` (query) | B | IMPL |
| `size` (query) | G,B | IMPL |
| `rows <n>` `cols <n>` `columns <n>` | G | IMPL |
| `line <n>` | G | IMPL |
| `[-]drain` | G | IMPL |

### 6.8 Combination operands

| Operand | Class | Status |
|---------|-------|--------|
| `sane` `[-]raw` `[-]cooked` `[-]cbreak` `[-]nl` `ek` | P | IMPL |
| `evenp` `parity` `oddp` `[-]parity` `[-]evenp` `[-]oddp` | P | IMPL |
| `crt` `dec` `[-]pass8` `[-]litout` `[-]decctlq` | G | IMPL |
| `[-]lcase` `[-]LCASE` | P,B | GATED |

---

## 7. Non-Functional Requirements — `STTY-NFR-*`

- **STTY-NFR-001** — The implementation shall be a single C
  translation unit, `bin/stty/stty.c`, built by `bin/stty/Makefile`
  through `Makefile.bin.inc`.
- **STTY-NFR-002** — The implementation shall compile cleanly under
  the Substrate base flags including `-Wall -Wextra -Werror
  -std=c2x` for the `i386` target.
- **STTY-NFR-003** — The implementation shall use only the
  Substrate userspace `termios` interface (`<termios.h>`,
  `<sys/ioctl.h>`); it shall not reference kernel-only headers and
  shall not declare external symbols manually.
- **STTY-NFR-004** — The utility shall be registered in
  `bin/Makefile`'s `SUBDIRS` so that `make -C bin` builds it.
- **STTY-NFR-005** — The utility shall be documented by a
  section 1 manual page, `usr.man/man1/stty.1`, following the
  project man-page standard (LIBRARY not required for a utility;
  SEE ALSO required; EXIT STATUS and DIAGNOSTICS documented).
- **STTY-NFR-006** — The utility shall install to `/bin/stty`.
- **STTY-NFR-007** — Diagnostics shall be concise, single-line,
  and shall not leak `errno` text where `errno` is not meaningful.

---

## 8. Conformance / Verification Matrix

| Verification method | Applies to |
|---------------------|------------|
| `T` — automated test (`tests/bin/stty/`) | parsing, save/restore, error paths, exit status |
| `D` — demonstration (manual on a live tty) | reporting layout, mode application |
| `I` — inspection (code/spec review) | NFRs, deferred-item accounting |

Every `IMPL` requirement in §4–§6 shall be covered by at least one
of `T`, `D`, or `I`. `GATED`/`DEFER` requirements are verified by
`I` only (their presence in §10).

---

## 9. Test Requirements — `STTY-TST-*`

- **STTY-TST-001** — A test shall confirm `stty -g` output, fed
  back as a `stty` operand, is an identity operation.
- **STTY-TST-002** — A test shall confirm that each boolean mode
  operand and its negation set/clear the expected bit.
- **STTY-TST-003** — A test shall confirm control-character value
  parsing for `^X`, `^?`, `undef`, hex, octal, decimal, literal.
- **STTY-TST-004** — A test shall confirm that an unrecognised
  operand yields a non-zero exit status and a diagnostic.
- **STTY-TST-005** — A test shall confirm `-a`/`-g` mutual
  exclusivity and info-option/setting mutual exclusivity error
  paths.
- **STTY-TST-006** — A test shall confirm `size` and `speed`
  query output shapes.

---

## 10. Conformance Limitations (Deferred Items)

The following standardised operands are **not** implemented by this
delivery because the Substrate userspace `termios` ABI lacks the
backing constant. Each is recorded here so the "full POSIX/GNU/BSD"
claim is bounded and auditable. Lifting any of them requires (a)
adding the constant to `include/termios.h` **and**
`sys/include/sys/termios.h`, and (b) teaching the console line
discipline (`sys/drivers/console/`) to honour it.

| Item | Backing constant needed | Class |
|------|-------------------------|-------|
| Output delays `cr*/nl*/tab*/bs*/ff*/vt*` | `NLDLY CRDLY TABDLY BSDLY VTDLY FFDLY` | P |
| `oxtabs`/`tabs`/`tab3` | `OXTABS` (userspace) | G,B |
| `[-]crtscts` | `CRTSCTS` (userspace) | G,B |
| `[-]xcase`, `lcase`, `LCASE` | `XCASE` | P,B |
| `[-]extproc` | `EXTPROC` | G,B |
| `[-]altwerase`, `[-]nokerninfo` | `ALTWERASE`, `NOKERNINFO` | B |
| BSD hardware-flow flags | `CCTS_OFLOW` etc. | B |
| `erase2`, `dsusp`, `status` | `VERASE2`, `VDSUSP`, `VSTATUS` | B |

The Substrate console line discipline currently honours: `OPOST`,
`ONLCR`, `ICRNL`, `INLCR`, `IGNCR`, `ISTRIP`, `IXON`, `IXOFF`,
`ECHO`, `ECHOE`, `ECHOK`, `ECHOCTL`, `ECHONL`, `ICANON`, `ISIG`,
`IEXTEN`, `TOSTOP`, `VMIN`, `VTIME`. Operands marked `IMPL` for
flags outside that set are still correct: `stty` writes the bit
into the `termios` value and the kernel stores it; honouring the
stored bit is the line discipline's responsibility, tracked
separately from this utility.

---

## 11. Traceability

| User story | Primary requirements |
|------------|----------------------|
| US-01 | STTY-INV-003, STTY-RPT-001..005 |
| US-02 | STTY-VRB-001..004 |
| US-03 | STTY-SAV-001..005 |
| US-04 | STTY-CMB-001, STTY-DEF-001 |
| US-05 | STTY-CMB-002 |
| US-06 | STTY-LM-001 |
| US-07 | STTY-WSZ-001 |
| US-08 | STTY-CC-001..007 |
| US-09 | STTY-DEV-001..006 |
| US-10 | STTY-ERR-001..005, STTY-APL-003 |
| US-11 | STTY-CM-003, STTY-IM-003, STTY-CC-003, STTY-LM-003 |
| US-12 | STTY-WSZ-002 |

---

## 12. Execution Tasklist

LLM-optimised, ordered, each task independently checkable. Status
keys: `[ ]` pending, `[x]` done, `[~]` deferred (documented in §10,
intentionally not executed).

```
[x] T-01  docs/specs/stty.md authored (this document).
[x] T-02  bin/stty/Makefile created (PROG=stty, SRCS=stty.c,
          DYNAMIC=1, includes Makefile.inc + Makefile.bin.inc).
[x] T-03  bin/Makefile SUBDIRS: insert `stty` between `split`
          and `su` (alphabetical).
[x] T-04  bin/stty/stty.c: invocation layer — options -a/-e/-g/
          --save/--file/-F/-f/--/--help; first-non-option cutoff;
          info-option exclusivity (STTY-INV-*, STTY-ERR-004).
[x] T-05  stty.c: device layer — default fd 0; -F/-f/--file open
          O_RDONLY|O_NONBLOCK|O_NOCTTY; isatty/tcgetattr guard
          (STTY-DEV-*).
[x] T-06  stty.c: mode tables — c_iflag/c_oflag/c_cflag/c_lflag
          flag table; c_cc table with POSIX+GNU+BSD names and
          aliases; readability aliases (STTY-CM/IM/OM/LM/CC).
[x] T-07  stty.c: "sane" reference + abbreviated report
          (STTY-DEF-001, STTY-RPT-*).
[x] T-08  stty.c: BSD grouped verbose report for -a/-e
          (STTY-VRB-*, STTY-WSZ-003).
[x] T-09  stty.c: gfmt1 save emit (-g) and gfmt1 parse/restore
          (STTY-SAV-*).
[x] T-10  stty.c: control-character operands + value parser
          ^X/^?/undef/hex/octal/dec/literal (STTY-CC-*).
[x] T-11  stty.c: numeric/special operands — bare speed, ispeed,
          ospeed, speed?, size?, rows, cols, columns, line, min,
          time, [-]drain (STTY-SPD-*, STTY-WSZ-*, STTY-CC-005,
          STTY-APL-004).
[x] T-12  stty.c: combination keywords — sane, [-]raw, [-]cooked,
          [-]cbreak, [-]nl, ek, evenp/parity/oddp/[-]parity,
          crt, dec, [-]pass8, [-]litout, [-]decctlq, tandem
          (STTY-CMB-*).
[x] T-13  stty.c: window-size get/set via TIOCGWINSZ/TIOCSWINSZ
          (STTY-WSZ-*).
[x] T-14  stty.c: error handling, exit status, single-apply via
          tcsetattr(TCSADRAIN/TCSANOW) (STTY-APL-*, STTY-ERR-*).
[x] T-15  usr.man/man1/stty.1 authored (STTY-NFR-005).
[x] T-16  tests/bin/stty/ test(s) authored (STTY-TST-*).
[x] T-17  Build: `make -C bin/stty` cross-compiles clean under
          -Werror; host smoke-compile + functional run
          (STTY-NFR-002).
[x] T-18  Commit bin/stty/, bin/Makefile, usr.man/man1/stty.1,
          tests/bin/stty/, docs/specs/stty.md.
[~] T-D1  termios.h delay masks / OXTABS / CRTSCTS / XCASE /
          EXTPROC / BSD flow flags / VERASE2 / VDSUSP / VSTATUS
          — deferred, see §10.
```
