# TestUnix Toolchain Configuration

This directory contains the necessary components to build a TestUnix cross-compiler.

## Targets
- `i386-unknown-testunix`
- `x86_64-unknown-testunix`

## Components
- **LLVM/Clang:** Used as the primary compiler.
- **Binutils:** (or LLVM equivalents like `llvm-objdump`) for binary manipulation.

## Configuration (Draft)
The TestUnix target is defined as a derivative of the Generic Unix target, using System V ABI for x86 and i386.

### ELF OSABI
`ELFOSABI_TESTUNIX` = 64

### Syscall Interface
Software interrupt `0x80`.
Calling convention: Standard i386 Linux-style for 32-bit, custom System V for 64-bit.
