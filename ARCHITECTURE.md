# ARCHITECTURE.md

## High-Level System Overview
This project implements a 32-bit x86 operating system. It follows a traditional Unix-like monolithic kernel design with a distinct separation between kernel space and user space.

## Core Components

### Kernel (`sys/`)
The kernel is the core of the operating system, responsible for:
- Hardware abstraction (x86 32-bit specific).
- Memory management.
- Process scheduling.
- Device drivers.

### Core Userland (`bin/`, `lib/`)
These components are essential for booting and basic system operation.
- **`bin/`**: Contains the shell and critical utilities necessary for system recovery and basic operation.
- **`lib/`**: Contains the dynamic linker/loader and fundamental shared libraries (libc, libm) required by binaries in `/bin` and `/usr/bin`.

### Extended Userland (`usr.bin/`, `usr.lib/`)
These components provide a fuller user environment.
- **`usr.bin/`**: General purpose user tools and applications.
- **`usr.lib/`**: Additional libraries not required for the immediate boot process.

## Design Patterns & Standards
- **ABIs:**
  - **C:** Standard Intel C ABI.
  - **C++:** x86 version of the Itanium C++ ABI.
- **Tooling:** Designed to be built with modern GCC.

## Data Storage & File System
*To be defined.* (e.g., ext2, FAT, or custom FS).

## Integration points
*To be defined.* (e.g., syscall interface).
