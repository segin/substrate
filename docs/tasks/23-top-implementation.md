# 23. `top(1)` — Comprehensive Implementation

> Greenfield task.  No prior `bin/top` exists.  Builds on the
> sys_proc_* / sys_vm_* / sysinfo stack landed in
> [`07-6a-system-call-wrapper-library.md`](07-6a-system-call-wrapper-library.md)
> §6a.1, plus the libsys ABI and error-contract guarantees from
> the same audit.

## Overview

A native Substrate `top(1)` patterned on procps-ng `top(1)` (the
canonical Linux behavior) and BSD `top(1)` (column conventions),
running on the libsys process / VM introspection syscalls and the
existing libedit / termios stack.  Targets feature parity with
procps `top` for the columns and key bindings most users actually
use, while staying portable across the four supported terminals
(VT100, ANSI, xterm, linux).

The work is split into seven independently-shippable phases.  Each
phase ends in a working, testable binary; later phases enrich it.

## Reimplemented Checklist (All Open)

### 23.1 Foundation & Data Plane (Phase 1)

- [ ] **Source Tree:** (REQ: REQ-23-0001)
    - [ ] Create `bin/top/` with `top.c`, `top.h`, `Makefile`. (REQ: REQ-23-0002)
    - [ ] Add `bin/top` to `bin/Makefile` SUBDIRS. (REQ: REQ-23-0003)
    - [ ] Stage `top` into `dist/usr/bin/top` via the existing
          rootfs build pipeline. (REQ: REQ-23-0004)
- [ ] **Process Snapshot Model:** (REQ: REQ-23-0005)
    - [ ] Define `top_proc_t` aggregating `sys_procinfo_t` plus
          derived fields (cpu_pct, mem_pct, time_total, sort key
          slot). (REQ: REQ-23-0006)
    - [ ] Define `top_snapshot_t` holding two consecutive snapshots
          (current, previous) plus a tick timestamp from
          `clock_gettime(CLOCK_MONOTONIC)`. (REQ: REQ-23-0007)
    - [ ] Implement `top_snapshot_take(top_snapshot_t *s)` that
          calls `sys_proc_list` for capacity, then
          `sys_proc_info` per pid, retrying with a bigger buffer
          on `-ENOMEM` per the probe-and-fill convention. (REQ: REQ-23-0008)
- [ ] **System Snapshot Model:** (REQ: REQ-23-0009)
    - [ ] Wrap `sysinfo(2)` for uptime, loadavg[3], procs count,
          totalram/freeram. (REQ: REQ-23-0010)
    - [ ] Wrap `sys_vm_stats(2)` for free/used/buffered/cached/
          swap total/free. (REQ: REQ-23-0011)
    - [ ] Wrap `sys_vm_swap(2)` for per-device swap (currently the
          single synthetic `swap0` entry; forward-compatible with
          per-device enumeration). (REQ: REQ-23-0012)
- [ ] **CPU Time Delta Math:** (REQ: REQ-23-0013)
    - [ ] Sample per-process `user_time` + `sys_time` (jiffies)
          from `sys_procinfo_t` and store both samples in
          `top_proc_t`. (REQ: REQ-23-0014)
    - [ ] Compute `cpu_pct = 100.0 * delta_jiffies /
          (system_jiffies_delta * ncpu)` with proper handling of
          processes that disappeared, restarted, or wrapped pid
          slots between samples. (REQ: REQ-23-0015)
    - [ ] Use jiffies-per-second from `_SC_CLK_TCK` (HZ); fall
          back to 100 if the syscall is unavailable. (REQ: REQ-23-0016)

### 23.2 Display Engine (Phase 2)

- [ ] **Terminal Capabilities:** (REQ: REQ-23-0017)
    - [ ] Detect `$TERM`; map to a small capability struct
          (clear, home, up N, color N, alt-screen on/off). (REQ: REQ-23-0018)
    - [ ] Use VT100 escape sequences as the lowest common
          denominator; layer on xterm / linux features when
          detected. (REQ: REQ-23-0019)
    - [ ] Honor `tcgetattr` / `tcsetattr` to enter raw mode (no
          echo, no canonical, VMIN=0, VTIME=1) on startup;
          restore the prior termios on exit and on every signal
          path (SIGINT, SIGTERM, SIGHUP, SIGQUIT). (REQ: REQ-23-0020)
- [ ] **Resize Handling:** (REQ: REQ-23-0021)
    - [ ] Install a SIGWINCH handler that re-reads `TIOCGWINSZ`
          and triggers a full re-render on the next loop tick. (REQ: REQ-23-0022)
    - [ ] Recompute column widths from the current terminal
          width whenever the row count or column width changes. (REQ: REQ-23-0023)
- [ ] **Frame Renderer:** (REQ: REQ-23-0024)
    - [ ] Implement `top_render(top_snapshot_t *s, top_view_t *v)`
          that emits the full frame to a single
          `write(STDOUT_FILENO, buf, len)`, avoiding partial
          flushes that flicker. (REQ: REQ-23-0025)
    - [ ] Use the alternate-screen buffer (`\e[?1049h` /
          `\e[?1049l`) when supported so the user's scrollback
          is preserved across runs. (REQ: REQ-23-0026)
- [ ] **Header Block:** (REQ: REQ-23-0027)
    - [ ] Line 1: `top - HH:MM:SS up D days, HH:MM, N user(s),
          load average: 0.00, 0.00, 0.00`. (REQ: REQ-23-0028)
    - [ ] Line 2: `Tasks: N total, R running, S sleeping, T
          stopped, Z zombie`. (REQ: REQ-23-0029)
    - [ ] Line 3: `%Cpu(s): U.U us, S.S sy, N.N ni, I.I id, W.W
          wa, H.H hi, S.S si, T.T st` (zero the categories we
          don't track yet, document which). (REQ: REQ-23-0030)
    - [ ] Line 4: `KiB Mem: T total, F free, U used, BC buff/cache`. (REQ: REQ-23-0031)
    - [ ] Line 5: `KiB Swap: T total, F free, U used. A avail Mem`. (REQ: REQ-23-0032)
- [ ] **Process Table:** (REQ: REQ-23-0033)
    - [ ] Default columns: PID, USER, PR, NI, VIRT, RES, SHR, S,
          %CPU, %MEM, TIME+, COMMAND. (REQ: REQ-23-0034)
    - [ ] Right-align numeric columns, left-align text columns. (REQ: REQ-23-0035)
    - [ ] Truncate COMMAND to remaining terminal width, NEVER
          wrap (wrapping breaks the constant-row-count
          invariant). (REQ: REQ-23-0036)
    - [ ] Highlight the column the table is currently sorted by
          (reverse video on the heading). (REQ: REQ-23-0037)

### 23.3 Sorting, Filtering, Field Selection (Phase 3)

- [ ] **Sort Keys:** (REQ: REQ-23-0038)
    - [ ] Implement comparators for PID (int), USER (string),
          PR (int), NI (int), VIRT (uint), RES (uint), %CPU
          (double), %MEM (double), TIME+ (uint64), COMMAND
          (string). (REQ: REQ-23-0039)
    - [ ] Stable sort (preserve relative order of equal keys
          across refreshes — keeps the screen from "jumping"). (REQ: REQ-23-0040)
    - [ ] Bind `Shift+P` / `Shift+M` / `Shift+T` / `Shift+N` to
          the four most common sort keys (CPU / MEM / TIME / PID)
          per procps convention. (REQ: REQ-23-0041)
    - [ ] Bind `R` to reverse sort order; default is descending. (REQ: REQ-23-0042)
- [ ] **Filtering:** (REQ: REQ-23-0043)
    - [ ] `u <username>` — show only processes whose `USER`
          matches; empty string clears the filter. (REQ: REQ-23-0044)
    - [ ] `o <field>=<value>` — generic filter (procps `o` key);
          supports `=`, `!=`, `<`, `>` on numeric fields and
          substring match on string fields. (REQ: REQ-23-0045)
    - [ ] `i` — toggle hiding of idle processes (CPU% < 0.05). (REQ: REQ-23-0046)
- [ ] **Field Selection:** (REQ: REQ-23-0047)
    - [ ] `f` — enter field-management screen; arrow keys to
          select, space to toggle visibility, `s` to make the
          highlighted field the sort key, `q` to leave. (REQ: REQ-23-0048)
    - [ ] `F` — same screen but rooted on column ordering;
          `+`/`-` shifts the selected column left or right. (REQ: REQ-23-0049)

### 23.4 Interactive Commands (Phase 4)

- [ ] **Loop & Input:** (REQ: REQ-23-0050)
    - [ ] Main loop: render → wait up to `delay` seconds for a
          keystroke (using `poll(STDIN_FILENO, POLLIN, delay_ms)`)
          → process keystroke → loop. (REQ: REQ-23-0051)
    - [ ] Decode arrow keys and function keys via the standard
          ANSI escape sequences (`\eOA`, `\e[A`, etc.). (REQ: REQ-23-0052)
- [ ] **Refresh Control:** (REQ: REQ-23-0053)
    - [ ] `Space` / `Enter` — refresh immediately, reset the
          delay timer. (REQ: REQ-23-0054)
    - [ ] `d` / `s` — prompt for a new delay in seconds (accepts
          fractional like `1.5`); validate and apply. (REQ: REQ-23-0055)
    - [ ] `H` — toggle threads vs processes view (uses
          `sys_proc_threads` to expand each process). (REQ: REQ-23-0056)
- [ ] **Process Management:** (REQ: REQ-23-0057)
    - [ ] `k` — prompt for PID, then for signal number (default
          15 / SIGTERM); send via `kill(2)`; report failures
          in the message line. (REQ: REQ-23-0058)
    - [ ] `r` — prompt for PID, then for nice value; apply via
          `setpriority(2)`; report failures. (REQ: REQ-23-0059)
- [ ] **Display Toggles:** (REQ: REQ-23-0060)
    - [ ] `1` — toggle aggregate vs per-CPU stats line in the
          header. (REQ: REQ-23-0061)
    - [ ] `t` — cycle CPU summary mode: aggregate, per-CPU
          numeric, per-CPU bar graph, off. (REQ: REQ-23-0062)
    - [ ] `m` — cycle memory summary mode: numeric, bar graph,
          percent, off. (REQ: REQ-23-0063)
    - [ ] `c` — toggle COMMAND column between argv[0] and full
          cmdline (uses `sys_proc_cmdline`). (REQ: REQ-23-0064)
    - [ ] `n` / `#` — prompt for max process row count. (REQ: REQ-23-0065)
    - [ ] `b` — toggle bold highlighting for the running task. (REQ: REQ-23-0066)
- [ ] **Help & Config:** (REQ: REQ-23-0067)
    - [ ] `h` / `?` — full-screen help listing every binding,
          dismissed on any keystroke. (REQ: REQ-23-0068)
    - [ ] `W` — write current view (delay, sort, columns,
          filters) to `~/.toprc`. (REQ: REQ-23-0069)
    - [ ] `q` — clean exit: restore termios, leave alt-screen,
          show cursor, return 0. (REQ: REQ-23-0070)

### 23.5 Modes, CLI Flags, Configuration (Phase 5)

- [ ] **Command-Line Flags:** (REQ: REQ-23-0071)
    - [ ] `-d <secs>` — initial refresh delay (default 3.0). (REQ: REQ-23-0072)
    - [ ] `-n <count>` — exit after `count` iterations (default
          infinite). (REQ: REQ-23-0073)
    - [ ] `-p <pid>[,<pid>]*` — restrict to listed PIDs; up to
          20 entries. (REQ: REQ-23-0074)
    - [ ] `-u <user>` — restrict to processes owned by `<user>`;
          accepts numeric uid or username. (REQ: REQ-23-0075)
    - [ ] `-b` — batch mode (no curses, no input handling, plain
          text per iteration), suitable for `top -b -n 1 > log`. (REQ: REQ-23-0076)
    - [ ] `-s` — secure mode: disable `k`, `r`, `W`, all
          settings-changing keys. (REQ: REQ-23-0077)
    - [ ] `-H` — start in threads view. (REQ: REQ-23-0078)
    - [ ] `-c` — start with full-cmdline COMMAND column. (REQ: REQ-23-0079)
    - [ ] `-h` / `--help` — usage to stderr, exit 0. (REQ: REQ-23-0080)
    - [ ] `-v` / `--version` — version + libsys version + exit 0. (REQ: REQ-23-0081)
- [ ] **Config File:** (REQ: REQ-23-0082)
    - [ ] On startup read `~/.toprc` if present; ignore unknown
          keys (forward-compatible). (REQ: REQ-23-0083)
    - [ ] File format: simple `key = value` lines, `#` comments;
          keys: `delay`, `sort`, `sort_dir`, `columns`,
          `filter_user`, `filter_cmd`, `idle_hidden`,
          `cpu_mode`, `mem_mode`. (REQ: REQ-23-0084)
    - [ ] `W` writes the current state in the same format,
          atomically (`mkstemp` + `rename`) to avoid truncation
          on crash. (REQ: REQ-23-0085)

### 23.6 Robustness & Performance (Phase 6)

- [ ] **Error Recovery:** (REQ: REQ-23-0086)
    - [ ] If `sys_proc_list` returns -ENOMEM, retry once with
          double the buffer; if it fails again surface "proc
          enumeration failed: <errno>" in the message line and
          continue with the previous snapshot. (REQ: REQ-23-0087)
    - [ ] If a per-process `sys_proc_info` returns -ESRCH (the
          PID died between list and info), drop that entry
          silently (race is expected). (REQ: REQ-23-0088)
    - [ ] If `sys_vm_stats` is unavailable, render `?` for the
          memory/swap fields rather than crashing. (REQ: REQ-23-0089)
- [ ] **Resource Bounds:** (REQ: REQ-23-0090)
    - [ ] Cap process snapshot at 4096 entries; report
          "(truncated to 4096)" if the system has more. (REQ: REQ-23-0091)
    - [ ] Cap render buffer at 64 KiB; truncate the table if a
          single frame would overflow. (REQ: REQ-23-0092)
    - [ ] Allocate snapshot buffers once and resize on demand;
          do NOT malloc/free per refresh tick. (REQ: REQ-23-0093)
- [ ] **Profiling Hooks:** (REQ: REQ-23-0094)
    - [ ] Compile-time `-DTOP_PROFILE` enables a `P` key that
          shows the per-tick wall-clock time spent in
          snapshot, sort, and render phases. (REQ: REQ-23-0095)

### 23.7 Tests, Docs, and Polish (Phase 7)

- [ ] **Unit Tests:** (REQ: REQ-23-0096)
    - [ ] `tests/bin/top/test_delta.c` — deterministic CPU%
          delta math across (snapshot, snapshot') pairs with
          known jiffies. (REQ: REQ-23-0097)
    - [ ] `tests/bin/top/test_sort.c` — every comparator is
          stable and total-order over its key. (REQ: REQ-23-0098)
    - [ ] `tests/bin/top/test_filter.c` — `o`-key predicate
          parser accepts every documented form and rejects
          ill-formed input with a useful message. (REQ: REQ-23-0099)
- [ ] **Property Tests:** (REQ: REQ-23-0100)
    - [ ] `tests/bin/top/prop_render.c` — for every random
          snapshot, the render output is exactly N+5 lines
          (header + table) and has no trailing whitespace. (REQ: REQ-23-0101)
    - [ ] Render is idempotent on identical input. (REQ: REQ-23-0102)
- [ ] **Fuzz Tests:** (REQ: REQ-23-0103)
    - [ ] `tests/bin/top/fuzz_input.c` — feed random bytes to
          the input decoder; must never assert, dereference NULL,
          or read past the input buffer. (REQ: REQ-23-0104)
    - [ ] `tests/bin/top/fuzz_toprc.c` — feed random bytes to
          the config parser; same invariants. (REQ: REQ-23-0105)
- [ ] **Integration Tests:** (REQ: REQ-23-0106)
    - [ ] `tests/sys/integration_top_batch.sh` — boot the
          kernel, run `top -b -n 3 > /tmp/top.out`, assert the
          file has exactly 3 frames and parses cleanly. (REQ: REQ-23-0107)
    - [ ] `tests/sys/integration_top_kill.sh` — interactive
          via `expect`-style scripted input, send `k`,
          terminate a sleep child, assert it exits with the
          requested signal. (REQ: REQ-23-0108)
- [ ] **Documentation:** (REQ: REQ-23-0109)
    - [ ] `usr.man/man1/top.1` — full key binding reference,
          field glossary, examples for batch mode and config. (REQ: REQ-23-0110)
    - [ ] `usr.man/man5/toprc.5` — config file format. (REQ: REQ-23-0111)
    - [ ] Update `docs/syscalls/sys-proc-family.md` to note
          which fields top consumes from each syscall (forces
          us to keep them in sync when the kernel changes). (REQ: REQ-23-0112)
- [ ] **Release Polish:** (REQ: REQ-23-0113)
    - [ ] `top --help` text fits in 24 lines. (REQ: REQ-23-0114)
    - [ ] No memory leaks under valgrind on `top -b -n 5`. (REQ: REQ-23-0115)
    - [ ] No file descriptor leaks across 1000 refreshes
          (verified via `sys_proc_fds(getpid(), ...)` before/after). (REQ: REQ-23-0116)

## User Stories

### Phase 1 — Foundation & Data Plane
- **US-23-0001**: As a sysadmin investigating a runaway process, I want
  `top` to enumerate every process on the system without dropping
  entries, so I can trust its picture of the load.
- **US-23-0002**: As a Substrate userspace developer, I want
  `top_snapshot_take()` to be a single function call that handles
  capacity probing internally, so I don't have to re-derive the
  probe-and-fill convention in every consumer.
- **US-23-0003**: As an operator measuring CPU usage, I want
  per-process %CPU computed from snapshot deltas (not instantaneous
  state), so the values match what `time(1)` reports for the same
  workload.

### Phase 2 — Display Engine
- **US-23-0004**: As a user on a serial console, I want `top` to work
  with VT100 escapes only, so I can run it over a 9600-baud link
  without garbled output.
- **US-23-0005**: As an `xterm` user, I want `top` to use the
  alternate-screen buffer, so my scrollback is intact when I quit.
- **US-23-0006**: As a user resizing my window mid-run, I want `top`
  to redraw at the new dimensions on the next tick, so the output
  doesn't run off the side or wrap.
- **US-23-0007**: As an operator at a glance, I want the header to
  match the procps layout I already know, so I don't have to learn
  a new column order.

### Phase 3 — Sorting, Filtering, Field Selection
- **US-23-0008**: As a user hunting CPU hogs, I want to press `Shift+P`
  to sort by %CPU descending, just like procps `top`, so my muscle
  memory transfers.
- **US-23-0009**: As an admin debugging memory pressure, I want stable
  sort, so processes near each other in the table don't shuffle on
  every refresh and break my eye.
- **US-23-0010**: As a user with a single misbehaving service, I want
  `u myservice` to filter the view to just its processes, so I can
  watch it without the noise of the rest of the system.

### Phase 4 — Interactive Commands
- **US-23-0011**: As an operator faced with a stuck process, I want
  to press `k`, type the PID, and send SIGKILL without leaving
  `top`, so I don't have to copy/paste into another shell.
- **US-23-0012**: As a user lowering the priority of a backup job, I
  want `r` + PID + nice value, so I don't have to remember
  `renice(8)` syntax.
- **US-23-0013**: As an SRE running `top` for hours, I want `q` to
  always restore my terminal cleanly, so my next command isn't
  invisible because echo is still off.

### Phase 5 — Modes, CLI Flags, Configuration
- **US-23-0014**: As an automation engineer, I want `top -b -n 1` to
  emit a single plain-text frame to stdout, so I can capture system
  state from a cron job into a log file.
- **US-23-0015**: As a kiosk operator, I want `-s` (secure mode), so a
  curious user can see system load but can't kill or renice anything.
- **US-23-0016**: As a long-time procps user, I want my preferred
  delay, sort, and column set persisted in `~/.toprc`, so I don't
  have to re-configure every invocation.

### Phase 6 — Robustness & Performance
- **US-23-0017**: As a user on a busy server, I want `top` itself to
  consume single-digit CPU%, so observing the system doesn't change
  what's being observed.
- **US-23-0018**: As a developer, I want `top` to survive process
  table churn between snapshots, so a `fork()` storm doesn't crash
  the monitor.

### Phase 7 — Tests, Docs, and Polish
- **US-23-0019**: As a reviewer, I want unit + property + fuzz tests
  for the input decoder and config parser, so I can be confident
  random terminal junk won't crash a tool that runs as root.
- **US-23-0020**: As a Substrate user, I want a complete `top(1)` man
  page that lists every key binding, so I can find a feature without
  reading source.

## INCOSE/EARS Requirements

> EARS notation:
> - **Ubiquitous**: "The system shall <response>."
> - **Event-Driven**: "When <trigger>, the system shall <response>."
> - **State-Driven**: "While <state>, the system shall <response>."
> - **Optional**: "Where <feature included>, the system shall <response>."
> - **Unwanted**: "If <unwanted condition>, then the system shall
>   <response>."

### Foundation & Data Plane

- **REQ-23-0001** (Ubiquitous): The Substrate `bin/top` binary shall
  reside under `bin/top/` and be installed at `/usr/bin/top` by the
  rootfs build pipeline.
  - Verification: `find dist -name top` returns exactly one match
    after a clean build.
- **REQ-23-0005** (Ubiquitous): The Substrate `top` binary shall
  represent each enumerated process as a single `top_proc_t` record
  containing both the kernel-supplied `sys_procinfo_t` and all
  per-process derived metrics top renders.
  - Verification: type review and unit test asserting every render
    column has a backing field.
- **REQ-23-0008** (Event-Driven): When `top_snapshot_take()` runs and
  the per-process buffer is too small to receive every entry, the
  Substrate `top` binary shall reallocate to at least double its
  current capacity and retry, up to a documented maximum.
  - Verification: unit test forcing -ENOMEM on the first call and
    asserting a successful second call with grown capacity.
- **REQ-23-0013** (Ubiquitous): The Substrate `top` binary shall
  compute per-process CPU percentage from the difference between two
  consecutive `sys_procinfo_t` samples, never from an instantaneous
  reading.
  - Verification: unit test feeding two synthetic snapshots with
    known deltas and asserting the rendered %CPU.
- **REQ-23-0015** (Unwanted): If a process disappears between two
  snapshots, then the Substrate `top` binary shall omit it from the
  newer frame without raising an error.
  - Verification: unit test with snapshot N containing PIDs {1,2,3}
    and snapshot N+1 containing {1,3}; assert PID 2 is absent and
    no error is logged.

### Display Engine

- **REQ-23-0017** (Ubiquitous): The Substrate `top` binary shall
  produce its terminal output using VT100-compatible escape
  sequences and shall layer xterm / linux-terminfo enhancements
  only when `$TERM` advertises them.
  - Verification: integration test running with `TERM=vt100` and
    asserting no `\e[?` private-mode sequences are emitted.
- **REQ-23-0020** (Event-Driven): When the Substrate `top` binary
  enters its main loop, it shall save the prior termios and switch
  the controlling tty into raw / no-echo / VMIN=0 / VTIME=1 mode.
  When the binary exits — through `q`, EOF, or any signal in
  {SIGINT, SIGTERM, SIGHUP, SIGQUIT} — it shall restore the saved
  termios.
  - Verification: integration test asserting `stty` output before
    and after `top` invocation are byte-identical.
- **REQ-23-0021** (Event-Driven): When the controlling terminal
  delivers SIGWINCH, the Substrate `top` binary shall re-query
  `TIOCGWINSZ` and re-render the next frame at the new dimensions.
  - Verification: scripted integration test sending SIGWINCH and
    asserting the next frame's column widths.
- **REQ-23-0024** (Ubiquitous): The Substrate `top` binary shall
  emit each frame in a single `write(2)` of the assembled buffer.
  - Verification: strace-style trace under host-mode test asserting
    one write per frame.

### Sorting, Filtering, Field Selection

- **REQ-23-0038** (Ubiquitous): The Substrate `top` binary shall
  provide a stable comparator for each renderable column.
  - Verification: property test asserting that two equal-key entries
    preserve their relative order across `qsort`.
- **REQ-23-0041** (Event-Driven): When the user presses `Shift+P`,
  `Shift+M`, `Shift+T`, or `Shift+N`, the Substrate `top` binary
  shall set the active sort key to %CPU, %MEM, TIME+, or PID
  respectively, in descending order, on the next render.
  - Verification: scripted integration test asserting the column
    highlight moves and the row order changes accordingly.
- **REQ-23-0046** (State-Driven): While the idle-hidden toggle is on,
  the Substrate `top` binary shall omit any process whose computed
  %CPU is strictly less than 0.05.
  - Verification: unit test with synthetic processes at 0.04 and
    0.06 and assertions on which appear.

### Interactive Commands

- **REQ-23-0050** (Ubiquitous): The Substrate `top` binary's main
  loop shall block on `poll(STDIN_FILENO, POLLIN, delay_ms)` and
  shall not busy-spin between refreshes.
  - Verification: host-mode test inspecting `getrusage` after a 30 s
    run shows < 1 s of accumulated CPU time at the default delay.
- **REQ-23-0057** (Event-Driven): When the user presses `k` and
  supplies a valid PID followed by a valid signal number, the
  Substrate `top` binary shall invoke `kill(pid, signo)` and
  display the result in the message line.
  - Verification: scripted integration test that spawns a sleep
    child, sends `k <pid> <SIGTERM>` via top, and asserts the
    child's exit status.
- **REQ-23-0067** (Event-Driven): When the user presses `h` or `?`,
  the Substrate `top` binary shall display a full-screen help page
  enumerating every active key binding and its current state.
  - Verification: snapshot test capturing the help frame and
    asserting every binding documented in the man page is listed.
- **REQ-23-0070** (Event-Driven): When the user presses `q`, the
  Substrate `top` binary shall restore the terminal (termios,
  alt-screen, cursor visibility) and exit with status 0 within
  100 ms.
  - Verification: scripted timing test.

### Modes, CLI Flags, Configuration

- **REQ-23-0071** (Ubiquitous): The Substrate `top` binary shall
  accept the documented set of command-line flags
  (`-d`/`-n`/`-p`/`-u`/`-b`/`-s`/`-H`/`-c`/`-h`/`-v`).
  - Verification: cli golden test running each flag and asserting
    exit code and stderr/stdout contents.
- **REQ-23-0076** (State-Driven): While running with `-b` (batch
  mode), the Substrate `top` binary shall not enter raw termios
  mode, shall not consume stdin, and shall emit each frame as
  plain text suitable for shell redirection.
  - Verification: integration test running `top -b -n 1 < /dev/null`
    and asserting termios on the controlling tty is unchanged.
- **REQ-23-0077** (State-Driven): While running with `-s` (secure
  mode), the Substrate `top` binary shall reject any keystroke that
  would change process state (`k`, `r`, `W`, `f` settings) with
  the message `"command disabled in secure mode"`.
  - Verification: scripted integration test pressing each
    disabled key and asserting the message line.
- **REQ-23-0082** (Optional): Where a `~/.toprc` file is present
  and readable, the Substrate `top` binary shall apply its
  documented settings on startup, ignoring unknown keys.
  - Verification: integration test seeding `~/.toprc` and asserting
    the initial render reflects the values.

### Robustness & Performance

- **REQ-23-0086** (Unwanted): If the per-tick `top_snapshot_take`
  fails permanently after one retry, then the Substrate `top` binary
  shall reuse the previous snapshot, surface
  `"snapshot failed: <errno>"` in the message line, and continue.
  - Verification: unit test forcing two consecutive failures and
    asserting top does not exit.
- **REQ-23-0090** (Ubiquitous): The Substrate `top` binary shall cap
  its per-tick allocations at the buffers established at startup,
  with one documented growth path on -ENOMEM.
  - Verification: host-mode test running 1000 refreshes and
    asserting heap usage is bounded.
- **REQ-23-0094** (Optional): Where `top` is built with
  `-DTOP_PROFILE`, the binary shall expose a `P` key that emits
  per-phase wall-clock timings to the message line.
  - Verification: build with `-DTOP_PROFILE`, run, press `P`,
    assert message-line content.

### Tests, Docs, Polish

- **REQ-23-0096** (Ubiquitous): The Substrate `top` test suite shall
  include unit tests for delta math, sort comparators, and the
  filter predicate parser.
  - Verification: `make -C tests/bin/top` runs all three suites,
    each exits 0.
- **REQ-23-0103** (Ubiquitous): The Substrate `top` test suite shall
  include fuzz tests for the input decoder and the config parser.
  - Verification: `make -C tests/bin/top fuzz` runs the fuzz suites
    against a documented corpus and exits 0 with no crashes.
- **REQ-23-0106** (Ubiquitous): The Substrate `top` test suite shall
  include kernel-in-the-loop integration tests covering batch-mode
  output and interactive `k` (kill) flow.
  - Verification: `make -C tests/sys integration_top_batch
    integration_top_kill` runs both under QEMU and exits 0.
- **REQ-23-0109** (Ubiquitous): The Substrate `top` distribution
  shall ship `usr.man/man1/top.1` and `usr.man/man5/toprc.5`,
  documenting every command-line flag, key binding, and config
  key the binary supports.
  - Verification: a documentation linter walks `top --help`,
    `bin/top/top.c` key-binding table, and the man page sources;
    asserts no entry is missing from any side.

## Verification Matrix

| Phase | Coverage | Owners |
|-------|----------|--------|
| 1 — Data plane | unit (delta math, snapshot retry) | bin/top |
| 2 — Display | unit (renderer line counts) + integration (TERM=vt100, alt-screen, SIGWINCH) | bin/top + tests/sys |
| 3 — Sort/filter | unit + property (stability) | bin/top |
| 4 — Interactive | scripted integration (k, r, h, q) | tests/sys |
| 5 — CLI/config | golden CLI tests + integration (batch, secure, ~/.toprc) | bin/top + tests/sys |
| 6 — Robustness | host-mode (heap bounds, getrusage cap) + unit (failure paths) | bin/top |
| 7 — Docs/tests | doc linter + valgrind + fd-leak check | tests/sys |

## Risks & Mitigations

- **R1: SMP CPU% accuracy.**  Substrate is approaching SMP; per-CPU
  jiffy accounting may diverge from a single global counter.
  *Mitigation:* compute `%CPU` against `ncpu * delta_ticks` from
  the start; expose `1` to flip between aggregate and per-CPU view
  without touching the math.
- **R2: Snapshot consistency under fork storm.**  A `fork()` loop
  can churn the process table faster than `top` can sample.
  *Mitigation:* snapshot is best-effort; -ESRCH between list and
  info is normal and dropped silently.
- **R3: Terminal compatibility regressions.**  The four supported
  terminals don't all decode the same escape sequences identically.
  *Mitigation:* lowest-common-denominator default (VT100), explicit
  capability probe, integration tests for each `$TERM`.
- **R4: Config file as a shipping vector.**  A poorly-vetted
  `~/.toprc` parser is a privilege-escalation hazard if `top` is
  ever setuid.  *Mitigation:* `top` is NOT setuid; parser is
  fuzz-tested; config file ignores unknown keys.

## Cross-References

- [`07-6a-system-call-wrapper-library.md`](07-6a-system-call-wrapper-library.md)
  — `sys_proc_*` and `sys_vm_*` syscall surface this depends on.
- [`docs/syscalls/error-contract.md`](../syscalls/error-contract.md)
  — error normalization that lets top distinguish "race" (-ESRCH)
  from "real failure" (other -errno).
- [`docs/syscalls/sys-proc-family.md`](../syscalls/sys-proc-family.md)
  — schema documentation for the records top consumes.
