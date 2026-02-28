# chmod Design Note

## Scope
This document describes the production `bin/chmod` implementation, including mode parsing, recursive traversal policy, error handling, and test strategy.

## CLI Model
The utility supports:

- `chmod [-R [-H | -L | -P]] [-dfh] mode file ...`
- `chmod [-R [-H | -L | -P]] [-dfh] --reference=rfile file ...`

Parsing rules:

- Recognized short flags: `-R`, `-H`, `-L`, `-P`, `-d`, `-f`, `-h`.
- Long option: `--reference=RFILE` and `--reference RFILE`.
- If short-option parsing encounters an unknown flag, parsing falls back to treating that token as a mode operand (for mode strings like `-x`).
- Standalone mode fragments can be concatenated (e.g. `u` `+` `x`) into a mode expression when needed.

## Mode Parsing (`setmode/getmode` style)
A dedicated parser module lives in:

- `bin/chmod/mode_parser.h`
- `bin/chmod/mode_parser.c`

Public API:

- `chmod_setmode(mode_string, errbuf, errlen)`
- `chmod_getmode(parsed, current_mode)`
- `chmod_freemode(parsed)`

Supported syntax:

- Numeric octal modes (`0000..7777`).
- Symbolic clauses with `who`, operation (`+`, `-`, `=`), and tokens `rwxXstugo`.
- Comma-separated symbolic clauses applied left-to-right.

Behavior highlights:

- `X` sets execute bits only for directories or when execute is already present.
- Omitted `who` uses `a` semantics with umask filtering for additive/removal scope.
- Copy operators (`u`, `g`, `o`) copy rwx bits from source class to destination class.
- Special bits (`setuid`, `setgid`, sticky) are supported and unit-tested.

## Traversal Policy and `-R/-H/-L/-P`
The implementation uses an internal recursive walker (`opendir`/`readdir`) with explicit symlink policy mapping:

- `-R -H`: follow command-line symlinks only.
- `-R -L`: follow all symlinks.
- default / `-P`: physical walk (do not follow traversal symlinks).

Cycle handling:

- A visited `(dev, ino)` set is maintained for traversed directories.
- Revisited directories are skipped to prevent symlink-loop recursion.

## `-h` Semantics and Portability
`-h` selects symlink-object mode changes.

- BSD-like hosts: uses `lchmod` where available.
- Linux hosts: uses `fchmodat(AT_SYMLINK_NOFOLLOW)` as platform equivalent.
- Platforms without support: operation fails non-destructively with an explicit diagnostic:
  `-h requested but symlink mode changes are unsupported on this platform`.

The fallback never silently mutates symlink targets when direct symlink mode changes are unsupported.

## Error Model
- Per-entry failures set global return status to nonzero and traversal continues.
- `-f` suppresses textual diagnostics but does not suppress failure status.
- Traversal errors (read/stat failures) are reported per path and do not abort whole processing.

## EINTR Handling
The implementation retries on `EINTR` for:

- `stat`, `lstat`, `chmod`, `opendir`, `closedir`.

This avoids transient interruption failures in long recursive runs.

## Race-Condition Notes
The code relies on path-based operations (`lstat/stat` + `chmod`), so TOCTOU windows between metadata inspection and mode update remain possible. Mitigations:

- Decision logic uses the stat snapshot for a single path at a time.
- Physical recursion (`-P`) intentionally avoids mutating non-followed symlink targets.
- Walker paths are used directly without synthetic path rewriting.

## Test Plan Mapping
Test suite location: `tests/chmod/`.

- Unit tests: parser behavior, numeric/symbolic semantics, umask/special bits, `-d`, `--reference`, `-h` behavior.
- Integration tests: recursive policy (`-P`, `-H`, `-L`), symlink interactions, error output, `-f` suppression.
- Property tests: randomized trees with symlinks to verify traversal safety (no outside mutation under `-P`/`-H` constraints).
- Fuzz tests: seeded parser fuzz and traversal/path fuzz harnesses.

## Decision Log
1. Internal parser was implemented because libc `setmode/getmode` is unavailable in this tree.
2. Internal walker was used instead of `fts(3)` due missing `fts` in project libc/headers.
3. `-h` portability uses explicit unsupported diagnostics instead of risky fallback behavior.
4. Physical recursion intentionally avoids chmod on non-followed symlink entries to prevent out-of-scope target mutation.
