# uname Specification

## 1. Scope
This document defines the canonical semantics for the system identification fields used by `uname(1)` and `uname(3)` in the Substrate operating system, along with the required CLI behavior.

## 2. Canonical Meanings

The underlying data model consists of five base fields defined by POSIX, plus one extension field:
*   **`sysname`**: The branded OS family name visible to userspace ("Substrate").
*   **`nodename`**: Host/network node name (e.g. from `/etc/hostname`).
*   **`release`**: Stable compatibility/release line, intended for scripts.
*   **`version`**: Verbose build/version string, intended for diagnostics and humans.
*   **`machine`**: Hardware platform kernel architecture.
*   **`processor`** (extension): Userland/application execution architecture. (In v1, defaults to the same value as `machine`).

## 3. CLI Policy (v1)

### Option Support
*   **Mandatory POSIX Core:** `-a`, `-m`, `-n`, `-r`, `-s`, `-v`
*   **BSD-first Extensions:** `-p` (processor), `-o` (operating system, alias for `-s`)
*   **GNU-compatible Long Options:** `--all`, `--machine`, `--nodename`, `--processor`, `--operating-system`, `--kernel-name`, `--kernel-release`, `--kernel-version`, `--help`, `--version`
*   **Allowed omissions:** `-i` is explicitly excluded to prevent resolving the FreeBSD vs GNU conflict (hardware platform vs kernel ident).

### Default Behavior
*   Invoking `uname` with no arguments behaves exactly as `uname -s`.
*   Extra operands are treated as an error.

### Formatting Rules
*   Multi-field output (e.g., `-a` or multiple flags) prints fields in a canonical fixed order, regardless of `argv` order.
*   Canonical order: `sysname nodename release version machine processor operating_system`
    *(Only selected fields are printed. `-a` is equivalent to `-s -n -r -v -m -p`.)*
*   Selected fields are separated by exactly one ASCII space.
*   Output is terminated with exactly one newline.
*   Errors output to `stderr` and result in non-zero exit status.

## 4. Contract

The libc function `uname(3)` executes one system call and retrieves all five POSIX base fields synchronously. Localization only affects error diagnostics, not the returned field values.
