# Substrate `id` Implementation Notes

This document details the behavioral choices and conflict resolution policies of the Substrate `id` utility, which implements POSIX.1-2024 requirements alongside GNU and BSD extensions.

## Conformance Policy

1.  **POSIX.1-2024 Strict Compliance**: All required POSIX behavior is implemented as the baseline. Where output structures are specified (e.g. `uid=%u(%s) gid=%u(%s)...`), they are followed exactly.
2.  **BSD Preference**: When GNU coreutils and BSD (FreeBSD/OpenBSD/NetBSD) behaviors conflict, BSD behavior takes precedence.
3.  **GNU Extensions**: GNU-specific long options and semantics are supported where they do not conflict with #1 or #2.

## Output Formatting & Conflict Resolution

### 1. Default Format (No Options)
*   **POSIX**: Specifies the base format as `uid=%u(%s) gid=%u(%s)` with conditional insertion of `euid`, `egid`, and `groups`. 
*   **GNU Coreutils**: GNU may implicitly append `context=...` to the end of the format if SELinux is actively enforcing. 
*   **Substrate Behavior (BSD-first)**: By default, `context=` is **not** appended to the output strings even if supported, mimicking BSD implementations which omit Context labels from standard default output.

### 2. Group List Deduplication (`-G`)
*   **POSIX**: The output shall be a space-separated list of all *different* group IDs (effective, real, and supplementary). The first group ID shall be the effective group ID (or the real group ID if the `-r` option is present).
*   **Substrate Behavior**: Follows POSIX exactly. The utility builds an array beginning with the EGID (or RGID with `-r`), then appends RGID (if different), and finally loops over all supplementary `getgroups()`, dropping any duplicates before printing. 

### 3. BSD Flag Interactions (`-r` masking)
*   **GNU Conflict**: In GNU `id`, combining `-r` with `-G` will output the RGID instead of the EGID in the group list. In default mode, GNU strictly forbids `-r`.
*   **Substrate Behavior**: As per BSD policy, passing `-r` when in default mode (no option flags given) is permitted but actively ignored.

### 4. Zero-Delimiter Support (`-z` / `--zero`)
*   **Constraint**: The GNU `--zero` flag replaces spaces and newlines with NUL characters. However, GNU states this is forbidden when outputting in the default format.
*   **Substrate Behavior**: Using `-z` with the default POSIX format throws an error (`id: option --zero not permitted in default format`) and exits >0. When combined with `-G`, groups are separated by NUL.

## Multi-User Execution
As a GNU extension, `id` accepts multiple user operands. When multiple users are supplied, `id` will process each independently. If one user fails to resolve via `getpwnam()`, a diagnostic is printed to `stderr`, and `id` continues to evaluate the remaining operands, deferring a non-zero exit status until the process terminates.
