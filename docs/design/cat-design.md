# cat Design Notes

## Goals
- Implement a production-grade `cat` utility with clear raw and cooked execution paths.
- Preserve exact byte semantics for raw mode and deterministic byte-visualization rules for cooked mode.
- Keep behavior robust under partial writes, `EINTR`, `EPIPE`, and per-file failures.

## Mode Selection
- Raw mode is used when no text-processing options are active.
- Cooked mode is selected when any of `-A`, `-b`, `-e`, `-E`, `-n`, `-s`, `-t`, `-T`, `-v` is active.

## CLI Coverage
Implemented options:
- POSIX: `-u`, `-` operand.
- Common extensions: `-n`, `-b`, `-s`, `-E`, `-T`, `-v`, `-A`, `-e`, `-t`.
- Long options: `--number`, `--number-nonblank`, `--squeeze-blank`, `--show-ends`, `--show-tabs`, `--show-nonprinting`, `--show-all`, `--help`, `--version`.
- Additional robustness options: `-B` (raw buffer size), `-f` (non-blocking open first), `-l` (stdout write lock).

## Raw Engine
- Uses `open/read/write` directly.
- Buffer size resolution:
1. `-B bsize` (decimal or hex).
2. `fstat(stdout).st_blksize` if larger than fallback.
3. Fallback stack buffer (`64 KiB`).
- Allocation policy:
1. Stack fallback is always available.
2. Heap buffer is used only when larger than fallback.
3. On allocation failure, warning is emitted and execution continues with fallback.
- I/O policy:
1. Retry reads/writes on `EINTR`.
2. Handle partial writes with a completion loop.
3. Treat `EPIPE` as graceful early termination.

## Cooked Engine
- Uses `fopen/fgetc` input path for text transformations.
- Core logic is in `cat_cooked.c` and reused by runtime + tests + fuzz target.
- State persists across files:
1. Line number counter.
2. Start-of-line state.
3. Prior blank-line emission state.
- Transform rules:
1. `-n`: six-wide right-aligned line number and tab.
2. `-b`: number only non-blank lines.
3. `-s`: squeeze repeated blank lines.
4. `-E`/`-e`: append `$` before `\n`.
5. `-T`/`-t`: render tab as `^I`.
6. `-v`: caret + `M-` notation on bytes.
7. `-A`: equivalent to `-vET`.

## Signals, Errors, and Exit Codes
- `SIGPIPE` is ignored to convert broken pipes into handled `EPIPE` returns.
- Per-file failures are reported and processing continues.
- Stdout hard failures are fatal for further processing.
- Exit status is `0` on complete success, `1` if any failure occurred.

## Locking Policy (`-l`)
- Acquire whole-file stdout write lock (`F_WRLCK`, `F_SETLKW`, start=0, len=0).
- Retry acquisition/unlock on `EINTR`.
- If lock acquisition fails (including unsupported backends), fail and return non-zero.

## Testability Hooks
Host-only hook build (`CAT_TEST_HOOKS`) provides deterministic fault injection for:
1. Short reads/writes.
2. `EINTR` on read/write/lock.
3. One-shot malloc failure.
4. Forced lock failure errno.
5. One-shot forced `EPIPE` on write.
