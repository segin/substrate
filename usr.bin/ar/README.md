# ar(1) - Archive Utility

This is a clean-room implementation of the POSIX `ar` utility for Substrate OS.

## Features

*   **Standard Operations**: `r`, `c`, `t`, `x`, `d`, `m`, `q`.
*   **Format**: BSD/System V `!<arch>\n` format.
*   **Long Filenames**: Supports BSD-style `#1/length` extended filenames.
*   **Symbol Table**: Generates BSD-style `__.SYMDEF` symbol table for ELF object files (equivalent to `ranlib`).
*   **Compatibility**: Can read and write archives compatible with standard toolchains.

## Implementation Details

*   **Language**: C99 (with Substrate/BSD extensions).
*   **Dependencies**: Standard libc, `elf.h` (for symbol extraction).
*   **Self-Contained**: Implements `ranlib` functionality internally. Invoking as `ranlib` triggers `ar -s`.

## Usage

```sh
ar rcv libfoo.a foo.o bar.o
ar t libfoo.a
ar x libfoo.a
ranlib libfoo.a
```
