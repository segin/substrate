# Audit of libexvi (`usr.lib/exvi`) and ex(1)/vi(1) — requirements checklist

Read-only audit of the ex/vi editor library `usr.lib/exvi` (13,755 lines:
`exvi.c`, `exvi_buffer.c`, `exvi_parse.c`, `exvi_cmd.c`, `exvi_runtime.c`,
`exvi_session.c`, `exvi_visual.c`, `exvi_internal.h`), its front ends
`bin/ex` and `bin/vi`, its tests, man pages and build, carried out
2026-11-06 at commit `aa168174b`.  Line numbers are those of that commit.

Seven auditors each read one part line by line: core and buffer I/O; address
parsing and ex commands; runtime and session; `exvi_visual.c` in three
parts; and build, tests, ABI and documentation, with a black-box sweep of
every POSIX ex command and option.  Their reports were merged here; a defect
found by several auditors is listed once.

## Form

Each finding (EXVI-nn) is stated as one or more requirements the code does
not meet (EXVI-nn.m).  Each requirement is written to the INCOSE rules
(active voice, one *shall*, no vague terms, a verifiable criterion) in one of
the EARS (Easy Approach to Requirements Syntax) patterns:

- **Ubiquitous**: *"The `<system>` shall `<response>`."*
- **Event-driven**: *"When `<trigger>`, the `<system>` shall `<response>`."*
- **State-driven**: *"While `<state>`, the `<system>` shall `<response>`."*
- **Unwanted behaviour**: *"If `<condition>`, then the `<system>` shall `<response>`."*
- **Optional feature**: *"Where `<feature>`, the `<system>` shall `<response>`."*

*The editor* means ex and vi as built from libexvi.  Each finding carries:

- **Severity**: *Critical*, *High*, *Medium* or *Low*.
- **Where**: file and line.
- **Evidence**: the input and what happened.
- **Basis**: *reproduced* (run on a host build of the library, most under
  AddressSanitizer and UBSan, vi driven through a pty and judged by the file
  it wrote) or *traced* (the code path followed, not run).

POSIX.1-2024 (the ex and vi utility pages, "Regular Expressions in ex",
"Replacement Strings in ex") is the judge of behaviour.  `vim -u NONE -N`
served only as a sanity check.

## Working this list

This file is the task list for the audit.  Each requirement has a checkbox.
Fix one requirement (or a tight cluster of one finding) per commit, with a
test that fails on the old code, and tick its box in the same commit.  When
every box is ticked, delete this file; git keeps the history.

**The host binaries.**  `bin/ex` and `bin/vi` cannot be built for the host
(EXVI-74), so they were linked by hand.  libexvi built for the host compiles
against the host's `<regex.h>`; linked with Substrate's `libregex.a`, whose
`regex_t` is larger, every regex use smashed the stack (EXVI-77).  The
binaries the findings were reproduced on were linked against the host's
regex, and every finding that touches a regex was re-run on them.

## Summary

80 findings (EXVI-06 was merged into EXVI-36), 165 requirements.

| Severity | Findings |
|---|---|
| Critical | EXVI-01, 07, 20 |
| High | EXVI-02, 03, 04, 08, 09, 10, 11, 12, 14, 15, 21, 22, 23, 25, 29, 30, 31, 32, 36, 37, 38, 39, 40, 56 |
| Medium | EXVI-05, 13, 16, 17, 24, 33, 34, 41–49, 53, 54, 57–63, 67, 71–77 |
| Low | EXVI-18, 19, 26, 27, 28, 35, 50, 51, 52, 55, 64, 65, 66, 68, 69, 70, 78–81 |

## Contents

1. [Security](#1-security) — EXVI-01 .. 05
2. [Writing and reading files](#2-writing-and-reading-files) — EXVI-07 .. 19
3. [Memory safety, signals and the terminal](#3-memory-safety-signals-and-the-terminal) — EXVI-20 .. 28
4. [Regular expressions and substitute](#4-regular-expressions-and-substitute) — EXVI-29 .. 35
5. [ex commands](#5-ex-commands) — EXVI-36 .. 52
6. [Options](#6-options) — EXVI-53 .. 55
7. [vi](#7-vi) — EXVI-56 .. 70
8. [Performance](#8-performance) — EXVI-71 .. 73
9. [Build, tests, ABI and documentation](#9-build-tests-abi-and-documentation) — EXVI-74 .. 81

---

## 1. Security

### EXVI-01 — `./.exrc` runs without the `exrc` option, and `-s` reads startup files — *Critical*

- [x] **EXVI-01.1** (Optional feature) Where the `exrc` option is unset, the
  editor shall not read `./.exrc`.
- [x] **EXVI-01.2** (Ubiquitous) The editor shall provide the boolean option
  `exrc`, off by default.
- [x] **EXVI-01.3** (Optional feature) Where `-s` is given, the editor shall
  not read `EXINIT`, `$HOME/.exrc` or `./.exrc`.

- **Where**: `exvi_runtime.c:456-464` (`load_startup_commands`); there is no
  `exrc` option (`:set exrc` → "Unknown option").
- **Evidence**: in a directory holding `.exrc` = `!touch PWNED` (mode 644,
  owned by the user, as in any cloned repository),
  `printf 'q\n' | ex -s` creates `PWNED`.  `EXINIT='set nu' ex -s f` with
  `1p` prints `      1 1`.
- **Basis**: reproduced.

### EXVI-02 — `:tag` runs the tags file's address field as an ex command — *High*

- [x] **EXVI-02.1** (Event-driven) When `:tag` reads a tags entry, the editor
  shall interpret the entry's third field only as a line number or a search
  pattern.

- **Where**: `exvi_session.c:422` passes the field to `do_command`.  vi's own
  path, `vi_visual_apply_tag_target` (`exvi_visual.c:7369`), took it as an
  address only when it was nothing else, and otherwise ran it too (found
  while fixing: `^]` on a word with such a tag ran the shell command).
- **Evidence**: tags line `foo<TAB>f.c<TAB>!touch TAGPWNED`;
  `printf 'tag foo\nq!\n' | ex -s` creates `TAGPWNED`.
- **Basis**: reproduced.

### EXVI-03 — recover files: predictable names beside the file, symlinks followed, umask modes, any owner — *High*

- [ ] **EXVI-03.1** (Ubiquitous) The editor shall create recover files in the
  directory named by the `directory` option.
- [ ] **EXVI-03.2** (Ubiquitous) The editor shall create each recover file
  with `O_CREAT|O_EXCL|O_NOFOLLOW` and mode 0600.
- [ ] **EXVI-03.3** (Unwanted behaviour) If a recover file is not owned by the
  user, then the editor shall refuse to recover from it.

- **Where**: `exvi_runtime.c:237`, `:329` (`:preserve`, `fopen(path, "w")`),
  `:593` (save on a signal); `load_recover_into_buffer` `:253` and `ex -r`
  accept a file of any owner.  The `directory` option does not exist
  (EXVI-53).
- **Evidence**: `ln -s victim v.recover`, then
  `printf 's/vv/OVER/\npreserve\nq!\n' | ex -s v` writes `OVER` into
  `victim`; the same through `kill -TERM`.  Preserving a mode-0600 file
  creates a 0644 `.recover` copy.
- **Basis**: reproduced (loading a foreign `.recover`: traced).

### EXVI-04 — `:w` deletes a `<file>.recover` the session did not create — *High*

- [ ] **EXVI-04.1** (Event-driven) When the current file is written, the
  editor shall remove only a recover file that the same session created.

- **Where**: `exvi_runtime.c:401`, called from `exvi_buffer.c:557`.
- **Evidence**: with a user file `notes.recover`,
  `printf 's/n/m/\nw\nq\n' | ex -s notes` deletes it.
- **Basis**: reproduced.

### EXVI-05 — file contents can send escape sequences to the terminal — *Medium*

- [ ] **EXVI-05.1** (Ubiquitous) The editor shall display each byte that is not
  a printable character as a sequence of printable characters.

- **Where**: `exvi_visual.c:1443-1449` (`vi_draw_line`) draws an undecodable
  high byte `c` as `'^', c + 64`, which for 0xC0–0xDF is a control byte
  (0xDB → ESC, 0xC7 → BEL).
- **Evidence**: a file holding `A\xdb]2;PWNED-TITLE\xc7B\xdb[31mRED` sets the
  window title and turns the text red when opened in vi.
- **Basis**: reproduced.

### EXVI-06 — merged into EXVI-36

The number is not reused.

---

## 2. Writing and reading files

### EXVI-07 — a failed write replaces the file with a truncated copy and reports success — *Critical*

- [x] **EXVI-07.1** (Unwanted behaviour) If any write, flush or close of the
  output file fails, then the editor shall report the error.
- [x] **EXVI-07.2** (Unwanted behaviour) If any write, flush or close of the
  output file fails, then the editor shall leave the original file unchanged.
- [x] **EXVI-07.3** (Unwanted behaviour) If any write, flush or close of the
  output file fails, then the editor shall keep the buffer marked modified.

- **Where**: `exvi_buffer.c:430-432`, `539-553`: `fputs`/`fputc`/`fclose`
  unchecked, then the temporary file is renamed over the original.
- **Evidence**:
  `( trap '' XFSZ; ulimit -f 4; printf '1s/1/X/\nw\nq\n' | ex -s big )`
  on an 8,893-byte file: status 0, no message, the file is now 4,096 bytes
  ending mid-line.
- **Basis**: reproduced.

### EXVI-08 — an unreadable file loads as an empty buffer, and `:wq` empties it — *High*

- [ ] **EXVI-08.1** (Unwanted behaviour) If an existing file cannot be opened
  for reading, then the editor shall report the error.
- [ ] **EXVI-08.2** (Unwanted behaviour) If reading a file fails before its
  end, then the editor shall report the error.
- [ ] **EXVI-08.3** (Unwanted behaviour) If a file could not be read in full,
  then the editor shall refuse to write the buffer to that file without `!`.

- **Where**: `exvi_buffer.c:356-367` ignores an `fopen` failure; `:368`
  treats a `getline` error as end of file.
- **Evidence**: a mode-0200 file: `printf 'wq\n' | ex -s ur` prints nothing
  and leaves it 0 bytes.
- **Basis**: reproduced.

### EXVI-09 — `:wq!`, `:x!` and `ZZ` quit after a failed write — *High*

- [ ] **EXVI-09.1** (Unwanted behaviour) If the write performed by `wq`, `xit`
  or `ZZ` fails, then the editor shall not exit.

- **Where**: `buf_write_file` returns `void`; `exvi.c:322-336` takes `!` to
  mean "quit anyway"; `exvi_visual.c:7752-7764` (ZZ) quits regardless.
- **Evidence**: in a mode-555 directory, `s/r/R/` then `wq!` prints
  "Permission denied" and exits 0; the edit is lost.  `xZZ` in vi: the same.
- **Basis**: reproduced.

### EXVI-10 — `:wq file` and `:x file` ignore the file — *High*

- [ ] **EXVI-10.1** (Event-driven) When `wq` or `xit` is given a file name, the
  editor shall write the addressed lines to that file.
- [ ] **EXVI-10.2** (State-driven) While the buffer is unmodified, the editor
  shall exit on `xit` without writing.

- **Where**: `exvi.c:322-336`.  `:x` rewrites an unmodified buffer (the
  mtime changes), and with no file name errors instead of quitting.
- **Evidence**: `1d`, `wq f9`: `f9` is never created and the current file loses
  line 1.
- **Basis**: reproduced.

### EXVI-11 — `:w` overwrites an existing file other than the current one without `!` — *High*

- [ ] **EXVI-11.1** (State-driven) While the `writeany` option is unset, if
  `write` without `!` names an existing file other than the current file,
  then the editor shall refuse the write.
- [ ] **EXVI-11.2** (Ubiquitous) The editor shall provide the boolean option
  `writeany`, off by default.

- **Where**: `exvi_buffer.c` write path; `writeany` does not exist.
- **Evidence**: `w f3` replaces `f3`'s contents without a word.
- **Basis**: reproduced.

### EXVI-12 — saving by temp file and `rename` breaks links, ownership and odd directories — *High*

- [ ] **EXVI-12.1** (Event-driven) When the editor writes an existing file, it
  shall write the file's own inode.
- [ ] **EXVI-12.2** (Event-driven) When the file named is a symbolic link, the
  editor shall write the link's target.
- [ ] **EXVI-12.3** (Ubiquitous) The editor shall preserve a written file's
  owner and group.
- [ ] **EXVI-12.4** (Ubiquitous) The editor shall write any file the user can
  write, whatever the permissions of its directory and the length of its
  name.
- [ ] **EXVI-12.5** (Event-driven) When a write completes, the editor shall
  have the data on stable storage before it reports success.

- **Where**: `exvi_buffer.c:496-553`.
- **Evidence**: (a) a symlink becomes a regular file and its target is
  unchanged; (b) a hard link keeps the old text; (c) a writable file in a
  read-only directory cannot be saved ("f.tmp.XXXXXX: Permission denied");
  (d) a name over about 244 bytes cannot be saved; (e) a group-writable file
  owned by another user ends up owned by the editor, since `fchown` fails
  silently; (f) no `fsync` before the rename.
- **Basis**: reproduced; (e) traced.

### EXVI-13 — `:w` clears set-user-ID and set-group-ID — *Medium*

- [ ] **EXVI-13.1** (Ubiquitous) The editor shall preserve a written file's
  permission bits, set-user-ID and set-group-ID included.

- **Where**: `exvi_buffer.c:518-523`: `fchmod` before `fchown`, and `fchown`
  clears the bits.
- **Evidence**: a mode-4755 file is 755 after `:w`; 2755 the same.
- **Basis**: reproduced.

### EXVI-14 — `w !cmd` dies of SIGPIPE when the command stops reading — *High*

- [ ] **EXVI-14.1** (Unwanted behaviour) If a command that the buffer is
  written to exits before reading all of it, then the editor shall report the
  error and continue the session.

- **Where**: no SIGPIPE handling anywhere; `exvi_popen`, `exvi_cmd.c:830`.
- **Evidence**: on a 50,000-line file, `w !true` kills ex with status 141; the
  commands after it never run and the buffer is lost.
- **Basis**: reproduced.

### EXVI-15 — in batch mode an error neither stops ex nor sets its exit status — *High*

- [ ] **EXVI-15.1** (State-driven) While standard input is not a terminal, if a
  command fails, then the editor shall exit with a non-zero status without
  writing any file.
- [ ] **EXVI-15.2** (Unwanted behaviour) If input ends while the buffer is
  modified, then the editor shall exit with a non-zero status.

- **Where**: `exvi.c` main loop.
- **Evidence**: `bogus`, `1d`, `w`, `q`: "Unknown command", the file is still
  written, status 0.  `q` on a modified buffer then end of input: status 0.
- **Basis**: reproduced.  POSIX ex, "Consequences of Errors".

### EXVI-16 — a NUL byte cuts the line short, and `:w` writes it short — *Medium*

- [ ] **EXVI-16.1** (Ubiquitous) The editor shall keep every byte of a line
  read from a file, NUL included.
- [ ] **EXVI-16.2** (Event-driven) When a NUL is entered with `^V^@` in vi, the
  editor shall keep the text after the insertion point.

- **Where**: `exvi_buffer.c:171,176` (from `buf_read_file`);
  `exvi_visual.c:471-488` (`strdup`/`strlen`).
- **Evidence**: `ab\0cd` is written back as `ab`; `ifoo^V^@bar<ESC>` on `x`
  writes `foobar`.
- **Basis**: reproduced.

### EXVI-17 — `:recover` discards changes, and `:recover!` does not exist — *Medium*

- [ ] **EXVI-17.1** (State-driven) While the buffer is modified, if `recover`
  is given without `!`, then the editor shall refuse it.
- [ ] **EXVI-17.2** (Ubiquitous) The editor shall accept `recover!`.

- **Where**: `exvi_session.c:1023`; `exvi.c:306` passes no force flag.
- **Evidence**: `s/a/CHANGED/`, `recover` → "recovered", the change is gone.
- **Basis**: reproduced.

### EXVI-18 — an incomplete last line is written back without its newline — *Low*

- [ ] **EXVI-18.1** (Event-driven) When a file's last line has no newline, the
  editor shall write that line with a newline.

- **Where**: `exvi_buffer.c:426-433`.
- **Evidence**: a 1-byte file `a` is still 1 byte after `w`.
- **Basis**: reproduced.  POSIX ex, INPUT FILES.

### EXVI-19 — `:w ./f` while editing `f` leaves the buffer modified — *Low*

- [ ] **EXVI-19.1** (Event-driven) When a write names the current file by any
  path, the editor shall mark the buffer unmodified.

- **Where**: `exvi_buffer.c:471-473` compares names with `strcmp`.
- **Basis**: traced.

---

## 3. Memory safety, signals and the terminal

### EXVI-20 — use after free: backspace in `R` mode after a `^O` command that freed lines — *Critical*

- [x] **EXVI-20.1** (Event-driven) When a `^O` command from replace mode
  changes the buffer, the editor shall discard the replace-edit history.

- **Where**: `exvi_visual.c:7484-7492` (`^O`) keeps the stack of `line_t *`;
  only `vi_reset_replace_mode` clears it, and `u` (`:8251`) only while
  `replace_mode` is set, which `^O` cleared.  `vi_undo_replace_edit` (`:587`)
  then writes through the freed line.
- **Evidence**: `jRxy^Odd<BS><BS>` → ASan heap-use-after-free in
  `vi_undo_replace_edit`, freed by `buf_delete`; `Rxy^Ou<BS>` → the same,
  freed by `buf_free`.
- **Basis**: reproduced.

### EXVI-21 — every vi exit reads a dead stack frame — *High*

- [x] **EXVI-21.1** (Ubiquitous) The editor shall not access the frame of
  `exvi_visual_main` after that function returns.
- [x] **EXVI-21.2** (Ubiquitous) The editor shall register its terminal-restore
  exit handler at most once per process.

- **Where**: `exvi_visual.c:114`, `797-807`, `848-871`: `active_visual`
  points to the local `vis` and is never cleared; the `atexit` handler
  `vi_restore_terminal` reads it and may call `tcsetattr` with garbage.  Each
  `:vi` registers the handler again.
- **Evidence**: `:q!`, `ZZ`, `:wq` → ASan stack-use-after-return in
  `vi_restore_terminal` (`:802`).
- **Basis**: reproduced.

### EXVI-22 — SIGINT jumps through an uninitialised `jmp_buf` — *High*

- [x] **EXVI-22.1** (Event-driven) When SIGINT arrives during an ex command,
  the editor shall stop that command and return to the ex command prompt.
- [x] **EXVI-22.2** (Event-driven) When SIGINT arrives during vi text input,
  the editor shall end text input as `<ESC>` does.
- [x] **EXVI-22.3** (Event-driven) When SIGINT arrives during a vi command, the
  editor shall stop that command and remain in vi with the terminal in vi's
  mode.

- **Where**: `exvi_runtime.c:561-566` (`handle_sigint`), installed at
  `exvi.c:618`; `setjmp(main_loop_jmp)` runs at `exvi.c:645`, after vi's
  first visual session and after `-c` commands.  After `Q` then `:vi`, the
  jump leaves visual mode with the terminal raw.
- **Evidence**: `kill -INT` on vi → "Interrupt", SIGSEGV, terminal raw, no
  recover file.  `echo q | ex -s -c vi f` → SEGV.
- **Basis**: reproduced.  POSIX ex, ASYNCHRONOUS EVENTS.

### EXVI-23 — the SIGINT handler `longjmp`s, so SIGINT stays blocked and state is left half-changed — *High*

- [ ] **EXVI-23.1** (Ubiquitous) The editor's signal handlers shall call only
  async-signal-safe functions.  (SIGINT's handler only sets a flag now;
  SIGTERM/SIGHUP's still walks the buffer — EXVI-03, EXVI-24.)
- [x] **EXVI-23.2** (Event-driven) When an interrupted command has stopped, the
  editor shall accept further SIGINTs.
- [x] **EXVI-23.3** (Event-driven) When a command is interrupted, the editor
  shall leave its undo history and `global` marks as they were before the
  command.

- **Where**: `exvi_runtime.c:561-566`.
- **Evidence**: (a) after one interrupt `SigBlk` shows SIGINT blocked and
  later ^C are ignored (host glibc; Substrate libc's `setjmp` mask untested);
  (b) interrupting `save_undo` leaves `exvi_history_suspended` raised
  (`exvi_buffer.c:78,326`) and undo stops working; (c) interrupting `:g`
  leaves `global_mark` set (`exvi.c:230-255`): the next `1g/^1x$/p` prints
  3,711 lines.  The handler can also land inside `malloc` or
  `buf_insert_after` (traced).
- **Basis**: reproduced.

### EXVI-24 — SIGTERM and SIGHUP leave the terminal raw; `^Z` does nothing — *Medium*

- [ ] **EXVI-24.1** (Event-driven) When the editor exits on SIGTERM or SIGHUP,
  it shall restore the terminal's settings and screen.
- [ ] **EXVI-24.2** (Event-driven) When `^Z` is typed in vi command mode, the
  editor shall restore the terminal and stop itself with SIGTSTP.
- [ ] **EXVI-24.3** (Event-driven) When the editor receives SIGCONT, it shall
  restore vi's terminal mode and redraw the screen.

- **Where**: `exvi_runtime.c:609-614` calls `_exit(1)`, skipping the
  restore; there is no SIGTSTP/SIGCONT handling, and with ISIG off 0x1a falls
  to the `default:` of the command switch.
- **Evidence**: `x`, `kill -TERM`: status 1, `.recover` written, terminal
  left raw on the alternate screen.
- **Basis**: reproduced; `^Z` traced.

### EXVI-25 — a stray UTF-8 continuation byte hangs vi — *High*

- [ ] **EXVI-25.1** (Ubiquitous) The editor shall treat each byte that is not
  part of a valid UTF-8 sequence as one character.

- **Where**: `exvi_visual.c:1002-1054` (`vi_normalize_char_index`,
  `vi_next_char_index`) returns the same index at a stray 0x80–0xBF byte, so
  the loops at `:1242`, `:1257`, `:1280`, `:1300` never end.  Raw mode is on,
  so ^C cannot stop it.  Deleting by byte (EXVI-56) creates such bytes from
  valid text.
- **Evidence**: `printf 'temp 20\xb0C\n' > f; vi f`, then `$` or `Ax<ESC>`:
  100% CPU forever.
- **Basis**: reproduced.

### EXVI-26 — a long `|` chain overflows the stack — *Low*

- [ ] **EXVI-26.1** (Ubiquitous) The editor shall run a `|`-separated command
  list in stack space independent of its length.

- **Where**: `exvi.c:399-415` recurses once per `|`.
- **Evidence**: one line of 300,000 `p|` → SIGSEGV.
- **Basis**: reproduced.

### EXVI-27 — signed overflow in addresses and `%` counts — *Low*

- [ ] **EXVI-27.1** (Unwanted behaviour) If an address or count does not fit in
  the editor's line-number type, then the editor shall report an error.
- [ ] **EXVI-27.2** (Unwanted behaviour) If a count given to `%` exceeds 100,
  then the editor shall report an error.

- **Where**: `exvi_parse.c:397`, `476-478` (`strtol` truncated to `int`;
  UBSan: `1 + 2147483647`, `-1 * -2147483648`); `exvi_visual.c:6028`
  (`999999999 * 3`).
- **Evidence**: `4294967297p` prints line 1; `999999999d%` deletes line 1.
- **Basis**: reproduced.

### EXVI-28 — leaks — *Low*

- [ ] **EXVI-28.1** (Ubiquitous) The editor shall free every allocation it
  makes before the allocation becomes unreachable.

- **Where**: `exvi_session.c:386-395` (`tags_path` on two error returns;
  ASan: 5 bytes from `open_tags_file` `:216`); `exvi_visual.c:8616-8624`
  (`last_insert_text` on the `Q` path).
- **Basis**: reproduced; the `Q` leak traced.

---

## 4. Regular expressions and substitute

### EXVI-29 — patterns are EREs, not BREs — *High*

- [ ] **EXVI-29.1** (Ubiquitous) The editor shall compile ex and vi patterns as
  POSIX basic regular expressions.
- [ ] **EXVI-29.2** (Ubiquitous) The editor shall support `\<`, `\>` and `~` in
  patterns.
- [ ] **EXVI-29.3** (Ubiquitous) The editor shall provide the boolean option
  `magic`, on by default.

- **Where**: `exvi_runtime.c:63` (`exvi_regex_flags`) sets `REG_EXTENDED`.
- **Evidence**: `1s/a+/X/` on `aa+` gives `X+` (BRE: `aX`); `/a|b/` matches
  as alternation; a ctags entry `/^int foo(void)$/` is not found ("Bad
  address").
- **Basis**: reproduced.

### EXVI-30 — every backslash is stripped from a pattern — *High*

- [ ] **EXVI-30.1** (Ubiquitous) The editor shall remove a backslash from a
  delimited pattern only where it precedes the delimiter.
- [ ] **EXVI-30.2** (Ubiquitous) The editor shall end a `global` pattern at the
  first unescaped delimiter.

- **Where**: `exvi_parse.c:275-278` (`parse_delimited_text`),
  `exvi_cmd.c:249-253`; `exvi.c:194`.
- **Evidence**: `s/\./X/g` on `a.b axb` → `XXXXXXX`; `\(ab\)\1`, `\<foo\>`,
  `\\` match nothing; `g/a\/b/p` finds nothing.
- **Basis**: reproduced.

### EXVI-31 — the replacement is copied literally — *High*

- [ ] **EXVI-31.1** (Ubiquitous) The editor shall replace `&` in a replacement
  with the matched text.
- [ ] **EXVI-31.2** (Ubiquitous) The editor shall replace `\1` through `\9` in a
  replacement with the matched subexpressions.
- [ ] **EXVI-31.3** (Ubiquitous) The editor shall replace `~` in a replacement
  with the previous replacement.
- [ ] **EXVI-31.4** (Ubiquitous) The editor shall apply `\u`, `\l`, `\U`, `\L`
  and `\E` in a replacement.
- [ ] **EXVI-31.5** (Ubiquitous) The editor shall split the line at each escaped
  newline in a replacement.

- **Where**: `exvi_cmd.c:139-152` (`memcpy`).
- **Evidence**: `s/b/[&]/` on `abc` → `a[&]c`; `s/\(foo\) \(bar\)/\2 \1/` →
  `2 1`; `~` and `\U&` are literal.
- **Basis**: reproduced.

### EXVI-32 — `^` matches in the middle of a line — *High*

- [ ] **EXVI-32.1** (Event-driven) When the editor matches a pattern from a
  position after the start of a line, it shall not match `^` at that
  position.

- **Where**: `exvi_cmd.c:138` (`s///g` after the first match);
  `exvi_visual.c:4675`, `4728` (vi's in-line search): no `REG_NOTBOL`.
- **Evidence**: `%s/^/>/g` on `ab` → `>a>b>`; in vi `/^b` on `abb` lands on
  the second `b`, so `d/^pat`, `n`, `N` act on the wrong text.
- **Basis**: reproduced.

### EXVI-33 — an empty pattern does not mean the last pattern — *Medium*

- [ ] **EXVI-33.1** (Event-driven) When a pattern is empty, the editor shall use
  the last pattern used by any command.
- [ ] **EXVI-33.2** (Ubiquitous) The editor shall record the pattern of every
  command that uses one, `global` and `v` included, as the last pattern.

- **Where**: `exvi_cmd.c:704-710` (`s//` uses only the last substitute
  pattern); `exvi.c:209` (`g//` matches every line, and `:g` does not set the
  last pattern).
- **Evidence**: `/a/` then `s//c/` does nothing; `/c/` then `g//p` prints every
  line.
- **Basis**: reproduced.

### EXVI-34 — substitute's `c` flag, count and `&&` missing — *Medium*

- [ ] **EXVI-34.1** (Ubiquitous) The editor shall accept substitute's `c` flag
  and ask for confirmation of each replacement.
- [ ] **EXVI-34.2** (Ubiquitous) The editor shall accept a count after
  substitute's flags.
- [ ] **EXVI-34.3** (Ubiquitous) The editor shall accept the `&` command with
  the `&` option.

- **Evidence**: `s/x/y/c`, `s/x/y/ 3`, `&&` → "Bad substitute flags".
- **Basis**: reproduced.

### EXVI-35 — a bad pattern gives no diagnostic — *Low*

- [ ] **EXVI-35.1** (Unwanted behaviour) If a pattern fails to compile, then the
  editor shall report the error.

- **Where**: `exvi_cmd.c:117`, `exvi.c:209`, `exvi_parse.c:300`.
- **Evidence**: `s/[/x/` and `g/[/d` do nothing silently; `/[/` says only
  "Bad address".
- **Basis**: reproduced.

---

## 5. ex commands

### EXVI-36 — `!` runs no shell, expands nothing, and ignores its range — *High*

- [ ] **EXVI-36.1** (Ubiquitous) The editor shall run the commands of `!`,
  `r !` and `w !` as the `shell` option's program with `-c` and the command
  as one argument.
- [ ] **EXVI-36.2** (Ubiquitous) The editor shall expand `%`, `#` and `!` in
  those commands before running them.
- [ ] **EXVI-36.3** (Event-driven) When `!` is given a range, the editor shall
  replace those lines with the command's output for them as input.
- [ ] **EXVI-36.4** (Ubiquitous) The editor shall provide the string option
  `shell`, defaulting to `$SHELL`.

- **Where**: `exvi_cmd.c:855`, `940-1000` (`exvi_tokenize_command` +
  `execvp`); `exvi.c:392` never passes the range.
- **Evidence**: `!echo a | tr a b` → `a | tr a b`; `!echo %` → `%`; `!!` →
  `execvp: No such file or directory`; `2,3!sort -r` changes nothing.  Five of
  178 cases in `tests/bin/ex/test_ex.sh` fail on this (EXVI-76).
- **Basis**: reproduced.

### EXVI-37 — `g!` acts as `g`, and `j!` inserts spaces — *High*

- [ ] **EXVI-37.1** (Ubiquitous) The editor shall treat `global!` as `v`.
- [ ] **EXVI-37.2** (Ubiquitous) The editor shall join lines given to `join!`
  without adding or removing white space.

- **Where**: `exvi.c:174` inverts only on `cmd[0] == 'v'`; `exvi.c:381`,
  `389-390` pass `forcep = NULL`.
- **Evidence**: `g!/3/d` on 1..5 leaves `1 2 4 5`.
- **Basis**: reproduced.

### EXVI-38 — `u` after `:g` undoes only its last change — *High*

- [ ] **EXVI-38.1** (Ubiquitous) The editor shall undo a `global` or `v`
  command, with every command it ran, as one change.

- **Where**: `exvi.c:230-250`; each sub-command calls `save_undo`.
- **Evidence**: `g/[24]/d`, `u` → `1 3 4 5`.
- **Basis**: reproduced.

### EXVI-39 — undo restores the file name and modified flag, drops marks, and does not toggle — *High*

- [ ] **EXVI-39.1** (Ubiquitous) The editor's `undo` shall leave the current
  file name unchanged.
- [ ] **EXVI-39.2** (Event-driven) When `undo` changes the buffer, the editor
  shall mark the buffer modified relative to the last write.
- [ ] **EXVI-39.3** (Ubiquitous) The editor's `undo` shall keep the marks of
  lines it does not remove.
- [ ] **EXVI-39.4** (Event-driven) When `undo` follows `undo`, the editor shall
  reverse the first `undo`.

- **Where**: `exvi_buffer.c:288-322`, `buf_copy` `:328-343`.
- **Evidence**: (a) `s/orig/changed/`, `w`, `u`, `q` quits with status 0 and the
  reverted buffer is lost; (b) `f fb`, `u` brings back the old file name;
  (c) `1d`, `2d`, `u`, `u` gives back both lines; (d) `2ka`, `3d`, `u`, `'ap`
  → "Bad address".
- **Basis**: reproduced.

### EXVI-40 — `t`/`co` into its own range copies lines it has just inserted — *High*

- [ ] **EXVI-40.1** (Ubiquitous) The editor's `copy` shall copy the addressed
  lines as they were before the command.

- **Where**: `exvi_cmd.c:504-510` (`src = src->next` walks onto new lines).
- **Evidence**: `2,4t3` on 1..10 → `1 2 3 2 3 2 4 …` (expected
  `1 2 3 2 3 4 4 …`).
- **Basis**: reproduced.

### EXVI-41 — counts and print flags after a command are ignored — *Medium*

- [ ] **EXVI-41.1** (Event-driven) When a command is given a count, the editor
  shall apply it as POSIX "Command Line Parsing in ex" describes.
- [ ] **EXVI-41.2** (Event-driven) When a command is given the flags `#`, `p`
  or `l`, the editor shall print the resulting current line in that form.

- **Evidence**: `1,3d 2` deletes 1–3 (expected 3–4); `3j 3` joins two lines;
  `2p#` → "Unknown command"; `2pl` prints without list mode.
- **Basis**: reproduced.

### EXVI-42 — named buffers: `d x` ignores `x`, `A`–`Z` do not append — *Medium*

- [ ] **EXVI-42.1** (Event-driven) When `delete` names a buffer, the editor
  shall store the deleted lines in it.
- [ ] **EXVI-42.2** (Event-driven) When a buffer is named by an uppercase
  letter, the editor shall append to the buffer of the lowercase letter.

- **Where**: `exvi_cmd.c:273` passes `""`; `:638` maps `A` to the unnamed
  buffer.
- **Evidence**: `2d x`, `$pu x` puts nothing; `1y a`, `2y A`, `$pu a` puts only
  line 1.
- **Basis**: reproduced.

### EXVI-43 — join's white-space rules — *Medium*

- [ ] **EXVI-43.1** (Ubiquitous) The editor's `join` shall discard the leading
  blanks of each line it appends.
- [ ] **EXVI-43.2** (Event-driven) When the line before the join point ends in
  `.`, the editor shall insert two spaces.

- **Where**: `exvi_cmd.c:607-611`.
- **Evidence**: `a` + `   b` → `a   b`.
- **Basis**: reproduced.

### EXVI-44 — the current line after commands is wrong — *Medium*

- [ ] **EXVI-44.1** (Event-driven) When a command completes, the editor shall
  set the current line as POSIX specifies for that command.
- [ ] **EXVI-44.2** (Event-driven) When a file is loaded at startup or by
  `edit`, the editor shall set the current line to the last line.

- **Where**: `exvi_cmd.c:267-285`, `482-561`; startup; `edit`; `read`.
- **Evidence**: `2,4d|.=` → 1 (expected 2); `8,10d` → 1 (7); `2,3m$` → 1
  (10); `2,4t0` → 4 (3); at startup and after `e f`, `.=` → 1; `2r` of two
  lines → 1 (4).
- **Basis**: reproduced.

### EXVI-45 — backward ranges and address 0 are silently ignored — *Medium*

- [ ] **EXVI-45.1** (Unwanted behaviour) If the first address of a range is
  greater than the second, then the editor shall report an error.
- [ ] **EXVI-45.2** (Unwanted behaviour) If address 0 is given to a command that
  does not accept it, then the editor shall report an error.

- **Where**: `exvi_parse.c:504-578`.
- **Evidence**: `3,1d`, `0d`, `1,0d` do nothing and say nothing.
- **Basis**: reproduced.

### EXVI-46 — omitted and extra addresses — *Medium*

- [ ] **EXVI-46.1** (Event-driven) When an address beside `,` or `;` is omitted,
  the editor shall use the current line.
- [ ] **EXVI-46.2** (Event-driven) When more addresses are given than the
  command takes, the editor shall use the last ones.
- [ ] **EXVI-46.3** (Event-driven) When addresses are separated by `;`, the
  editor shall set the current line to the first address before evaluating
  the second.

- **Where**: `exvi_parse.c:532-570`, `:573`.
- **Evidence**: `,5p` → "Unknown command"; `5,p` → `5,$`; `1,3,5p` →
  "Unknown command".
- **Basis**: reproduced.

### EXVI-47 — `:g` command lists and nesting — *Medium*

- [ ] **EXVI-47.1** (Ubiquitous) The editor shall continue a `global` command
  list over each escaped newline.
- [ ] **EXVI-47.2** (Event-driven) When text input in a `global` command list
  has no terminating `.`, the editor shall end it at the end of the list.
- [ ] **EXVI-47.3** (Unwanted behaviour) If a `global` command list contains
  `global`, `v` or `undo`, then the editor shall report an error.

- **Evidence**: `g/2/s/2/X/\` + `s/X/Y/` → "Bad substitute flags";
  `g/3/a\` + `NEW` swallows the rest of the input; `g/5/g/./p` prints the
  whole file.
- **Basis**: reproduced.

### EXVI-48 — `edit +command` is taken as part of the file name — *Medium*

- [ ] **EXVI-48.1** (Event-driven) When `edit` is given `+command`, the editor
  shall run the command after loading the file.

- **Where**: `exvi_session.c:1138`.
- **Evidence**: `e +2 three` edits a file named `+2 three`.
- **Basis**: reproduced.

### EXVI-49 — missing POSIX commands — *Medium*

- [ ] **EXVI-49.1** (Ubiquitous) The editor shall implement `abbreviate`,
  `unabbreviate`, `map` and `unmap`.
- [ ] **EXVI-49.2** (Ubiquitous) The editor shall implement `chdir`, `shell`,
  `source` and `suspend`/`stop`.
- [ ] **EXVI-49.3** (Ubiquitous) The editor shall implement `open`, `z`, `<`,
  `>`, `^D`, `~`, `@`, `*` and `ex`.
- [ ] **EXVI-49.4** (Ubiquitous) The editor shall accept every command
  abbreviation POSIX lists, `nu` and `ve` included.
- [ ] **EXVI-49.5** (Ubiquitous) The editor shall accept the `\/`, `\?` and
  `\&` addresses.

- **Evidence** ("Unknown command"): each of the above; bare `su` does
  nothing.
- **Basis**: reproduced.

### EXVI-50 — ex command-line options — *Low*

- [ ] **EXVI-50.1** (Ubiquitous) The editor shall accept the `-` and
  `-w size` options.
- [ ] **EXVI-50.2** (Event-driven) When `-r` is given with no file, the editor
  shall list the files it can recover.

- **Evidence**: `-w` → "invalid option"; `-r` → "-r requires a file operand"
  (`exvi.c:572-580`).
- **Basis**: reproduced.

### EXVI-51 — `:vi` from ex — *Low*

- [ ] **EXVI-51.1** (Event-driven) When ex enters visual mode, the editor shall
  keep the same buffer, undo history, marks and named buffers.
- [ ] **EXVI-51.2** (Ubiquitous) The installed ex shall enter Substrate's own
  visual mode.

- **Where**: `exvi.c:278-285` refuses on a modified buffer and otherwise
  hands over by file name; `bin/ex/ex.c:56-64` looks for `../vi/vi`, which
  exists only in the source tree, then runs whatever `vi` is on `PATH`.
- **Basis**: reproduced (the refusal; on the build host `:vi` started Vim).

### EXVI-52 — some commands fail silently — *Low*

- [ ] **EXVI-52.1** (Unwanted behaviour) If a command cannot be carried out,
  then the editor shall report an error.

- **Evidence**: `$j`, and `s` with no previous substitute, do nothing and say
  nothing.
- **Basis**: reproduced.

---

## 6. Options

### EXVI-53 — 26 of POSIX's 37 options are missing — *Medium*

- [ ] **EXVI-53.1** (Ubiquitous) The editor shall implement every option POSIX
  lists for ex and vi.

- **Missing**: autoprint, autowrite, beautify, directory, edcompatible,
  errorbells, exrc, magic, mesg, paragraphs, prompt, redraw, remap, report,
  sections, shell, shiftwidth, showmatch, slowopen, taglength, term, terse,
  warn, window, wrapmargin, writeany.  Some are required by other findings:
  EXVI-01 (`exrc`), EXVI-03 (`directory`), EXVI-11 (`writeany`), EXVI-29
  (`magic`), EXVI-36 (`shell`), EXVI-67 (`shiftwidth`).  No autoprint after
  `1d`; `ta fooxx` finds no `foo` (no taglength).
- **Basis**: reproduced.

### EXVI-54 — options accepted but without effect, and wrong defaults — *Medium*

- [ ] **EXVI-54.1** (Optional feature) Where `autoindent` is set, the editor
  shall indent ex text input as POSIX describes.
- [ ] **EXVI-54.2** (Ubiquitous) The editor's `append!`, `change!` and
  `insert!` shall toggle `autoindent` for that command.
- [ ] **EXVI-54.3** (Optional feature) Where `scroll` is set, the editor shall
  use it for ex's `^D` and `z`.
- [ ] **EXVI-54.4** (Ubiquitous) The editor shall default `showmode` to off.

- **Evidence**: `autoindent` does nothing in ex (`ex.1` says it applies only
  to vi); `a!` does not toggle it; `scroll` has no effect in ex (no `^D`, no
  `z`); `showmode` defaults to on (`exvi_runtime.c:25`).
- **Basis**: reproduced.

### EXVI-55 — `:set` reports — *Low*

- [ ] **EXVI-55.1** (Event-driven) When `set` is given no argument, the editor
  shall list the options whose values differ from their defaults.
- [ ] **EXVI-55.2** (Event-driven) When `set all` is given, the editor shall
  list every option and its value.
- [ ] **EXVI-55.3** (Event-driven) When `set noX?` is given, the editor shall
  report option X's current value.

- **Where**: `exvi_session.c:437-470`, `542`, `552`.
- **Evidence**: `:set` lists `showmode` and `wrapscan` at their defaults;
  `:set all` omits off booleans; `set noai?` with ai off prints `autoindent`.
- **Basis**: reproduced.

---

## 7. vi

### EXVI-56 — operators count bytes, and write broken UTF-8 into the file — *High*

- [ ] **EXVI-56.1** (Ubiquitous) The editor shall measure vi motions and operator
  regions in characters.
- [ ] **EXVI-56.2** (Ubiquitous) The editor's `f`, `F`, `t` and `T` shall take a
  whole character as their argument.

- **Where**: `dl` `exvi_visual.c:5058`, `dh` `:5074`, the inclusive `+1` in
  `vi_apply_charwise_motion` `:4451`, `:4456`, `f`/`t` reading one byte
  `:3497`, `:5164`, `:5453`; `x` and Delete through `vi_delete_char`
  `:6397`.
- **Evidence**: `dl` on `éa` writes `\xa9a`; `dfé` writes `\xa9b` and runs the
  second byte as a command; `y2$P` yanks `cd\xc3`; Delete on `日x` leaves
  `\x97\xa5x` (which then hangs vi, EXVI-25).
- **Basis**: reproduced.

### EXVI-57 — insert and replace drop every byte ≥ 0x80 — *Medium*

- [ ] **EXVI-57.1** (Ubiquitous) The editor shall insert each character typed in
  text input, UTF-8 included.
- [ ] **EXVI-57.2** (Ubiquitous) The editor's `r` shall accept any character,
  UTF-8 included.

- **Where**: `exvi_visual.c:7549`, `7636` (`isprint(key) || key == '\t'`
  under `setlocale(LC_CTYPE, "C")`, `exvi.c:566`); `r` at `:8211`.
- **Evidence**: `aé<ESC>` on `ab` leaves `ab`.
- **Basis**: reproduced.

### EXVI-58 — ESC and the next key within 50 ms are both lost — *Medium*

- [ ] **EXVI-58.1** (Event-driven) When ESC is not followed by a known key
  sequence, the editor shall act on ESC and then on each following key.

- **Where**: `exvi_visual.c:1777-1803` (`vi_read_key`).
- **Evidence**: `ifoo<ESC>Obar<ESC>:wq\r` in one write leaves `fooarwq`;
  `Ay<ESC>:wq\r` never leaves insert mode.  Fast typing, pastes and slow
  links all hit it.
- **Basis**: reproduced.

### EXVI-59 — counts on `i a I A R o O r s S` are ignored, and carry over — *Medium*

- [ ] **EXVI-59.1** (Event-driven) When `i`, `a`, `I`, `A`, `o`, `O` or `R` is
  given a count, the editor shall enter the input text count times.
- [ ] **EXVI-59.2** (Event-driven) When `r`, `s` or `S` is given a count, the
  editor shall apply the command to count characters or lines.
- [ ] **EXVI-59.3** (Event-driven) When a command has used its count, the editor
  shall clear the count.

- **Where**: `exvi_visual.c:8067-8164`, `8204-8215`, `8225-8245` never call
  `vi_take_count`.
- **Evidence**: `3ix<ESC>` inserts one `x`; `3rx` replaces one;
  `3ix<ESC>0x` becomes `30x` and deletes the line.
- **Basis**: reproduced.

### EXVI-60 — `^W`, `^U` and backspace erase text that was there before the insert — *Medium*

- [ ] **EXVI-60.1** (Ubiquitous) In text input, the editor's `^H`, `^W` and `^U`
  shall erase only characters entered on the current line in the current
  insert.
- [ ] **EXVI-60.2** (Ubiquitous) In text input, the editor's `^H`, `^W` and `^U`
  shall leave the named and unnamed buffers unchanged.
- [ ] **EXVI-60.3** (Ubiquitous) The editor shall undo one text input, erasures
  included, as one change.
- [ ] **EXVI-60.4** (Ubiquitous) The editor's `^H` shall erase one character.

- **Where**: `exvi_visual.c:7590-7598` with `6280-6372` (normal-mode word
  motion and `vi_delete_range`); backspace `6225-6277` (by byte, and joins
  lines at column 0).
- **Evidence**: `A^W<ESC>jp` on `foo bar` erases `bar` and `p` puts it back;
  `A baz^Wqq<ESC>u` undoes only part of the insert; `A<BS>` on `ab` erases
  `b`.
- **Basis**: reproduced.

### EXVI-61 — word motions — *Medium*

- [ ] **EXVI-61.1** (Ubiquitous) The editor shall treat a maximal run of
  characters other than letters, digits, underscores and blanks as a word.
- [ ] **EXVI-61.2** (Event-driven) When a counted word motion reaches the end of
  a line, the editor shall continue on the next line.
- [ ] **EXVI-61.3** (Event-driven) When `e` is used with an operator on the last
  character of a word, the editor shall extend the region to the end of the
  next word.
- [ ] **EXVI-61.4** (Event-driven) When `cw` with count 1 starts on a blank, the
  editor shall change only that character.

- **Where**: `vi_is_word_char` `exvi_visual.c:2893-2896`; `:2905-2943`,
  `3461-3486`, `3835-3897`, `5088-5111`, `5315-5373`, `5430-5446`;
  `vi_find_change_word_target` `:3878-3886`.
- **Evidence**: `dw` on `foo.bar baz` → `bar baz` (expected `.bar baz`);
  `3dw` on `a b` / `c d` → an empty first line (expected `d`); `c3wX` fails;
  `lde` on `ab cd` → `a cd` (expected `a`); `lcwX` on `a   b` → `aXb`
  (POSIX: `aX  b`).
- **Basis**: reproduced.

### EXVI-62 — `.` cannot repeat most deletes and shifts — *Medium*

- [ ] **EXVI-62.1** (Ubiquitous) The editor shall record every `d`, `c`, `<`,
  `>` and `!` command, with its count and motion, for `.`.

- **Where**: no `vi_set_last_change` for `dl`, `dh`, `de`/`dE`, `db`/`dB`,
  `dW`, `d)`, `d}`, `d(`, `d{`, `d/`, `d?`, and every `<`/`>` motion
  (`exvi_visual.c:5067-5730`).
- **Evidence**: `de.` deletes one word; `>j.` shifts the wrong lines.
- **Basis**: reproduced.

### EXVI-63 — `U` is not implemented — *Medium*

- [ ] **EXVI-63.1** (Ubiquitous) The editor's `U` shall restore the current line
  to its state when it last became the current line.

- **Where**: falls to `default:` (`exvi_visual.c:8583`).
- **Evidence**: `xxU` on `abc` leaves `c`.
- **Basis**: reproduced.

### EXVI-64 — `%` from a non-bracket, across lines — *Low*

- [ ] **EXVI-64.1** (Event-driven) When `%` starts on a character that is not a
  bracket, the editor shall move to the match of the first bracket after the
  cursor on the line.
- [ ] **EXVI-64.2** (Ubiquitous) The editor's `d%` shall delete from the cursor
  to the matching bracket, inclusive.

- **Where**: `exvi_visual.c:3655-3691`, `3705-3728`.
- **Evidence**: `%x` on `x (a` / `b) y` deletes the `(`; `d%` on `f(a,` /
  ` b) x` leaves `f x`.
- **Basis**: reproduced.

### EXVI-65 — a `dw` that deletes nothing still changes state — *Low*

- [ ] **EXVI-65.1** (Event-driven) When an operator's region is empty, the editor
  shall leave the buffer, the named buffers and the modified flag unchanged.

- **Where**: `exvi_visual.c:5321-5325`, `vi_delete_span` `4010-4041`.
- **Evidence**: `dw` on an empty line marks the buffer modified and empties the
  unnamed buffer.
- **Basis**: reproduced.

### EXVI-66 — `cc` with a count — *Low*

- [ ] **EXVI-66.1** (Optional feature) Where `autoindent` is set, the editor's
  `ncc` shall start the new line with the first changed line's indent.
- [ ] **EXVI-66.2** (Unwanted behaviour) If fewer than n−1 lines follow the
  current line, then the editor's `ncc` shall report an error.

- **Where**: `exvi_visual.c:4999-5010`.
- **Basis**: reproduced.

### EXVI-67 — `^D`, `0^D`, `^^D` and `^T` — *Medium*

- [ ] **EXVI-67.1** (Ubiquitous) The editor's `^D` and `^T` in text input shall
  shift by `shiftwidth`.
- [ ] **EXVI-67.2** (Event-driven) When `^D` follows `0` typed at the start of
  the indent, the editor shall remove the indent of the current line only.
- [ ] **EXVI-67.3** (Event-driven) When `^D` follows `^` typed at the start of
  the indent, the editor shall remove the indent of the current line and
  restore it on the next line.
- [ ] **EXVI-67.4** (Unwanted behaviour) If `^D` is typed after text other than
  the autoindent, then the editor shall not change the indent.

- **Where**: `exvi_visual.c:7531`, `7618` → `vi_reindent_current_line`
  (`:1111`), which uses `tabstop`.
- **Evidence**: `o0^Dx` with ai on `\tfoo` gives `0x`.
- **Basis**: reproduced.

### EXVI-68 — an exclusive region ending at column 0 leaves an empty line — *Low*

- [ ] **EXVI-68.1** (Event-driven) When a characterwise region ends at column 0
  of a later line, the editor shall exclude that line's first character and
  include the newline before it.

- **Where**: `exvi_visual.c:4459-4465`.
- **Evidence**: ``jjmakkd`a`` on `a b c d` (one per line) → `\nc\nd`
  (expected `c\nd`).
- **Basis**: reproduced.

### EXVI-69 — `^@` does not replay the last input — *Low*

- [ ] **EXVI-69.1** (Event-driven) When `^@` is the first character of text
  input, the editor shall insert the last text input and end text input.

- **Where**: `exvi_visual.c:7565-7566`, `7606` (`^A` does it instead).
- **Basis**: reproduced.

### EXVI-70 — screen: wide and combining characters, `^Y` at the bottom row — *Low*

- [ ] **EXVI-70.1** (Ubiquitous) The editor shall give each character its
  display width (0, 1 or 2 columns).
- [ ] **EXVI-70.2** (Event-driven) When `^Y` scrolls the cursor's line off the
  bottom of the screen, the editor shall make the last line shown the
  current line.

- **Where**: `exvi_visual.c:1083-1085`, `1393-1395`, `1411-1423` (width 1
  for all); `vi_line_scroll` `2223-2243`.
- **Evidence**: `$` on `日本語x` puts the cursor at column 4 (7); in a
  100-line file, `50Gz<CR>L^Y^Ydd` deletes line 72 (70).
- **Basis**: reproduced.

---

## 8. Performance

### EXVI-71 — undo keeps a full copy of the buffer per change, forever — *Medium*

- [ ] **EXVI-71.1** (Ubiquitous) The editor shall store each undo record in
  space proportional to the change it reverses.
- [ ] **EXVI-71.2** (Event-driven) When a command does not change the buffer,
  the editor shall store no undo record.

- **Where**: `exvi_buffer.c:72-83`, `128-140`, `348`.
- **Evidence**: a 1 MB, 20,000-line file: 11 MB at start, 462 MB after 200
  one-character edits.
- **Basis**: reproduced.

### EXVI-72 — loading is quadratic, and `:g` worse — *Medium*

- [ ] **EXVI-72.1** (Ubiquitous) The editor shall load a file in time linear in
  its size.
- [ ] **EXVI-72.2** (Ubiquitous) The editor's `global` shall run in time linear
  in the number of lines times the cost of its commands.

- **Where**: `buf_line_index` scans per insert/delete
  (`exvi_buffer.c:32-43`, `198-202`, `238`); `:g` restarts from the top after
  each sub-command (`exvi.c:247`, `251`) with a full snapshot each time.
- **Evidence**: load 25k lines 0.15 s, 100k 1.9 s; `g/./s/$/x/` on 1,000 lines
  0.39 s, 4,000 14.4 s; a 25,000-line `g/./d` still running after 3 minutes.
- **Basis**: reproduced.

### EXVI-73 — large counts hang vi, and ^C cannot stop them — *Medium*

- [ ] **EXVI-73.1** (Ubiquitous) The editor shall run a counted `p`, `P` or `.`
  as one change, in time linear in the text it inserts.
- [ ] **EXVI-73.2** (Ubiquitous) The editor's `^E` and `^Y` shall stop at the
  ends of the buffer, whatever the count.
- [ ] **EXVI-73.3** (Event-driven) When `^C` is typed during a vi command, the
  editor shall stop the command.

- **Where**: `p`/`P` and `.` (`exvi_visual.c:4220-4232`, `8291-8310`,
  `6843`) snapshot the buffer per repeat; `^E`/`^Y` (`:2223`) loop the whole
  count; ISIG is off.
- **Evidence**: `yy8000p` 38.7 s, `yy16000p` over 100 s; `999999999^E` on a
  3-line file 4.5 s.
- **Basis**: reproduced.

---

## 9. Build, tests, ABI and documentation

### EXVI-74 — `bin/ex` and `bin/vi` cannot be built for the host — *Medium*

- [ ] **EXVI-74.1** (Optional feature) Where `NATIVE_BUILD` is set, the ex and
  vi Makefiles shall link the host-built libexvi and libregex archives.

- **Where**: `bin/ex/Makefile:6`, `bin/vi/Makefile:6` link
  `-l:libexvi.so.0`, which a host build of usr.lib/exvi does not make.
- **Basis**: reproduced.

### EXVI-75 — `tests/usr.lib/exvi` does not link — *Medium*

- [ ] **EXVI-75.1** (Ubiquitous) The libexvi tests shall build and pass.

- **Where**: `test_main.c` includes `exvi_visual.c` and also links
  `libexvi.a`: every symbol twice.
- **Basis**: reproduced.

### EXVI-76 — the ex and vi test Makefiles hide failures and clobber the tree — *Medium*

- [ ] **EXVI-76.1** (Unwanted behaviour) If a step of the ex or vi test target
  fails, then the target shall fail.
- [ ] **EXVI-76.2** (Ubiquitous) The ex and vi test targets shall build nothing
  outside their own directories except through the libraries' own host
  builds.
- [ ] **EXVI-76.3** (Ubiquitous) The tree's test suite shall run
  `tests/bin/ex`, `tests/bin/vi` and `tests/usr.lib/exvi`.
- [ ] **EXVI-76.4** (Ubiquitous) The ex and vi test targets shall also run
  their tests on an AddressSanitizer build of ex and vi.  (Added while
  fixing: `test_vi_exit_paths.py` (EXVI-21) fails on the old code only under
  ASan, and `test_vi_replace_ctrl_o.py` (EXVI-20) on a plain build only
  because glibc's heap checks happen to abort.)

- **Where**: `tests/bin/ex/Makefile:11-20`, `tests/bin/vi/Makefile:12-18`:
  one `;`-joined command without `set -e`, so only the last status counts
  (`test_ex.sh` has 5 failures today); they run `make -C usr.lib clean all`,
  rebuilding every library for the host over the target's.  None of the
  three is run by `tests/Makefile`, `build.sh` or any workflow.
- **Basis**: reproduced.

### EXVI-77 — a host libexvi has no regex of its own to agree with — *Medium*

- [ ] **EXVI-77.1** (Optional feature) Where `NATIVE_BUILD` is set, libexvi
  shall be compiled and linked against the same regex implementation.

- **Where**: libexvi compiles against the host's `<regex.h>` (the
  `-idirafter` include path), while `usr.lib/regex/libregex.a` exports
  `regcomp`/`regexec` with a larger `regex_t` (`include/regex.h:37`); any
  host link with both smashes the stack on the first pattern.
- **Basis**: reproduced.

### EXVI-78 — libexvi exports its internals — *Low*

- [ ] **EXVI-78.1** (Ubiquitous) libexvi shall export only the interface
  declared in `include/exvi.h`.
- [ ] **EXVI-78.2** (Ubiquitous) `include/exvi.h` shall declare its interface
  with C linkage for C++ callers.

- **Evidence**: 135 defined dynamic symbols, 3 of them in `exvi.h`, among
  them `buf_init`, `regs`, `handle_sigint`, `main_loop_jmp`, `parse_range`;
  `exvi.h` has no `extern "C"` guard.
- **Basis**: reproduced (`nm -D`).

### EXVI-79 — libexvi is not branded for Substrate — *Low*

- [ ] **EXVI-79.1** (Ubiquitous) Each shared object the tree builds for
  Substrate shall carry ELFOSABI_SUBSTRATE.

- **Where**: `usr.lib/exvi/Makefile` never applies `BRAND_SUBSTRATE`;
  `readelf -h` shows "UNIX - System V" for i386 and x86_64.  The other
  `usr.lib` libraries are the same.
- **Basis**: reproduced.

### EXVI-80 — `view`, `rex` and `rvi` are recognised but not installed — *Low*

- [ ] **EXVI-80.1** (Ubiquitous) The build shall install each name the editor
  answers to that its manual pages document.

- **Where**: `exvi.c:484-488`; no Makefile or `build-rootfs.sh` creates the
  links; `usr.man/man1/view.1` documents `view`.
- **Basis**: reproduced.

### EXVI-81 — the documentation claims more than the code does — *Low*

- [ ] **EXVI-81.1** (Ubiquitous) `ex.1`, `vi.1` and
  `docs/specs/exvi_conformance.md` shall describe only behaviour the editor
  has.

- **Evidence**: `ex.1` documents `-s` as suppressing only interactive
  diagnostics and lists 11 options; `exvi_conformance.md:30` marks `-s`, and
  `:42` substitute, as supported.
- **Basis**: reproduced.
