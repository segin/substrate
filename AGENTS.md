# AGENTS.md

## Project Description
This is an operating system project targeting x86 32-bit architecture. The goal is to build a Unix-like system with a kernel, standard utilities, and libraries.

## Technical Constraints & Standards
- **Architecture:** x86 32-bit
- **C ABI:** Standard Intel C ABI
- **C++ ABI:** x86 version of the Itanium C++ ABI
- **Toolchain:** Modern GCC (compatible with standard BSD/Linux environments)

## Directives
1. **Architecture Maintenance:** Always read `ARCHITECTURE.md` before starting complex tasks. Update `ARCHITECTURE.md` if your changes impact the system structure or design.
2. **Code Style:** adhere to standard kernel coding styles (similar to BSD/Linux) for C and C++.
3. **Documentation:** Keep documentation close to the code.

## Directory Structure Overview
- `sys/`: Kernel source code.
- `bin/`: Fundamental Unix utilities (shell, ls, cp, etc.).
- `lib/`: Core libraries including the link-loader, C library (libc), and math library (libm).
- `usr.bin/`: Additional user-space tools.
- `usr.lib/`: Non-core and support libraries.
