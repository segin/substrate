# cat Design Notes

## Goals
- Implement a production-grade `cat` utility with two execution engines.
- Keep byte-accurate behavior for cooked visualization logic.
- Keep raw-path throughput high and robust under partial writes/signals.

## Mode Selection
- Raw mode is used when none of `-b`, `-e`, `-n`, `-s`, `-t`, `-v` are enabled.
- Cooked mode is used when any text-transform flag is enabled.

## Raw Engine
- Uses `open/read/write`.
- Buffer size resolution order:
1. `-B bsize` (decimal or hex).
2. `fstat(stdout).st_blksize` if larger than fallback.
3. Fallback `BUFSIZ` stack buffer.
- Memory policy:
1. Stack fallback is always available.
2. Heap allocation is attempted only for larger requested sizes.
3. On allocation failure, emit warning and continue with stack fallback.
- I/O policy:
1. Read retries on `EINTR`.
2. Write loops until full buffer is written.
3. Write retries on `EINTR`.
4. Write failure is fatal for the run and returns non-zero.
- `-f` open policy:
1. Try opening with `O_NONBLOCK` in raw mode.
2. Attempt to restore blocking mode using `fcntl(F_GETFL/F_SETFL)`.
3. Warn if post-open `fcntl` adjustments fail.

## Cooked Engine
- Uses `fopen/fgetc` for file input.
- Byte-oriented processing rules are implemented in `cat_cooked.c`.
- State tracked across file boundaries:
1. Line number counter.
2. Start-of-line status.
3. Previous-blank-line status for `-s`.
- Transform behavior:
1. `-n`: right-aligned 6-digit line numbers + tab.
2. `-b`: only number non-blank lines, implies numbering mode.
3. `-s`: squeeze adjacent empty lines.
4. `-e`: show `$` before `\n`, implies `-v`.
5. `-t`: show tabs as `^I`, implies `-v`.
6. `-v`: caret and `M-` notation; treats input as bytes.

## Signal and Error Policy
- Retry on `EINTR` for read/write/locking.
- Per-file open/read failures are non-fatal to the overall run; continue with next file.
- Stdout write failures are fatal and terminate processing.
- Exit status is non-zero if any file-level or stdout-level failure occurs.

## Locking Policy (`-l`)
- Acquire whole-file stdout write lock (`F_WRLCK`, `F_SETLKW`, start 0, len 0).
- Retry lock acquisition on `EINTR`.
- Unlock with `F_SETLK` and `F_UNLCK` before exit.
- If lock acquisition fails (including unsupported locking backends), fail fast and exit non-zero.

## Testability Hooks
- Host-only hook build (`CAT_TEST_HOOKS`) supports deterministic fault injection:
1. Forced short reads/writes.
2. Forced `EINTR` on read/write/lock.
3. One-shot malloc failure.
4. Forced lock failure errno.
- Hooks are used only by the Linux host test suite.
