# Substrate `tail` Implementation Notes

This document details the behavioral choices, conflict resolution policies, and extended features of the Substrate `tail` utility, which implements POSIX.1-2024 requirements alongside GNU and BSD extensions.

## Conformance Policy

1.  **POSIX.1-2024 Strict Compliance**: All required POSIX behavior is implemented as the baseline.
2.  **BSD Preference**: When GNU coreutils and BSD (FreeBSD/OpenBSD/NetBSD) behaviors conflict, BSD behavior takes precedence.
3.  **GNU Extensions**: GNU-specific long options and legacy syntax are supported where they do not violate #1 or #2.

## Conflict Resolution & Edge Cases

### 1. Reverse Mode (`-r`) vs. Follow Mode (`-f`, `-F`, `--follow`)

*   **Policy**: These modes are mutually exclusive.
*   **Behavior**: Attempting to combine `-r` with any follow option results in an immediate fatal error: `tail: cannot combine -r with -f or -F`. It is logically inconsistent to follow an actively appending file in reverse.

### 2. Reverse Mode (`-r`) Defaults

*   **POSIX/BSD/GNU Differences**: POSIX.1-2024 standardizes `-r`. BSD `tail -r` prints the *entire* file in reverse by default, whereas without `-r`, it prints the last 10 lines. Some implementations limit `-r` to the size of an internal buffer.
*   **Substrate Behavior**: `tail -r` without a count specified (`-n`, `-c`, `-b`) will print the **entire file** in reverse order. If a count is given (e.g., `tail -r -n 5`), it will print only those last 5 items, in reverse order. This operates correctly on both seekable files and pipes.

### 3. Numeric Suffix Parsers (`-c`, `-n`, `-b`)

*   **Conflict**: GNU coreutils defines `kB` = 1000, `K` = 1024, `MB` = 1000000, `M` = 1048576. BSD `expand_number(3)` treats `k`/`K` as 1024, and ignores a trailing `B` or `b` (meaning `kB` = 1024).
*   **Substrate Behavior**: We follow the **BSD precedence rule**. Suffixes `kB`, `MB`, `GB`, etc., are treated as base-2 multipliers (1024, 1048576, 1073741824). The GNU standard of treating "B" suffixes as base-10 (SI prefixes) is **not** adopted to maintain strict BSD compatibility. However, the GNU `b` suffix (meaning ×512) is supported.

### 4. Overlapping Options (`-q` vs `-v`)

*   **Conflict**: If a user specifies both `--quiet` and `--verbose`.
*   **Substrate Behavior**: Following BSD precedence, `-q` (quiet) strictly overrides `-v` (verbose), regardless of parse order or environment.

### 5. Multi-file Error Continuation

*   **Policy**: If `tail` is given multiple files, and one file fails to open, it must print an error to `stderr` and **continue** processing the remaining files.
*   **Substrate Behavior**: The tool stores the error state. If file 1 fails and file 2 succeeds, file 2's header is printed normally. The utility exits with a non-zero status at the end if *any* errors occurred during execution.

## Legacy and Obsolete Syntax

### 1. BSD Historic `-NUMBER`

*   **Syntax**: `tail -50`
*   **Behavior**: Implemented as exactly equivalent to `tail -n 50`.

### 2. GNU Obsolete Packed Syntax

*   **Syntax**: `tail -[NUM][bkm][cqlv][f]`
*   **Behavior**: 
    *   `tail -20c` -> `tail -c 20`
    *   `tail -1m` -> `tail -c 1048576`
    *   `tail -2kq` -> `tail -c 2048 -q`
    *   `tail -10f` -> `tail -n 10 -f`
*   **Pre-pass Interception**: To allow `getopt_long` to function correctly without violating POSIX standard flag parsing, this syntax is intercepted in a pre-pass before `getopt` runs.

## Architecture & Implementation Notes

*   **Seekable Output**: When tailing fixed quantities (`-n`, `-c`, `-b`) relative to the EOF of a regular file (e.g. `tail -n 100 big.log`), Substrate `tail` uses a highly efficient block-based backward scanning algorithm. It issues block reads backward from the EOF to count delimiters, then issues a single forward `write` directly to stdout. It does not buffer the entire file or read from the beginning.
*   **Pipes & Non-Seekables**: For pipes, Substrate maintains a dynamically expanding ring-buffer equivalent context to hold the final N items.
*   **Follow By Name (`-F`)**: Uses a reliable polling mechanism combined with `fstat()` and `stat()` to detect unlinking, truncation, or inode replacement (log rotation).
