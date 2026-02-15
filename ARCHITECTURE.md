# Architecture Overview
This document serves as a critical, living template designed to equip agents with a rapid and comprehensive understanding of the codebase's architecture, enabling efficient navigation and effective contribution from day one. Update this document as the codebase evolves.

## 1. Project Structure
This section provides a high-level overview of the project's directory and file structure, categorised by architectural layer or major functional area. It is essential for quickly navigating the codebase, locating relevant files, and understanding the overall organization and separation of concerns.


```text
[Project Root]/
├── sys/                  # Kernel source code
│   ├── arch/             # Architecture-specific code (e.g., i386)
│   ├── core/             # Central kernel logic (entry point, initialization)
│   ├── drivers/          # Hardware drivers (video, serial, input, storage)
│   ├── fs/               # Filesystem implementations (ext2, fat, minix, exec)
│   ├── kern/             # Kernel subsystems (scheduler, time, signals, ipc)
│   ├── pm/               # Process management
│   ├── vfs/              # Virtual File System layer
│   └── vm/               # Virtual Memory Manager (PMM, PMAP)
├── bin/                  # Fundamental Userland Utilities (sh, ls, cp, etc.)
├── usr.bin/              # User Tools (yacc, brandelf, etc.)
├── lib/                  # Userspace Libraries
│   ├── c/                # Standard C Library (libc)
│   ├── sys/              # System Call Wrappers (libsys)
│   ├── m/                # Math Library (libm)
│   └── pthreads/         # POSIX Threads Library
├── include/              # Userspace C Library Headers
├── sbin/                 # System Binaries (mkfs, fsck)
├── dist/                 # Build Artifacts (RootFS staging area)
├── host_dist/            # Host Tools for cross-compilation/testing
├── tests/                # Test Suite (Unit, Integration, Property, Fuzz)
├── Makefile              # Main Build System Entry Point
├── AGENTS.md             # Instructions for AI Agents
└── ARCHITECTURE.md       # This document
```

## 2. High-Level System Diagram
The system follows a monolithic kernel architecture with a strict separation between Kernel Space and User Space.

```text
[User] <--> [Shell / Applications] <--> [LibC / LibSys] <--> [System Calls (int 0x80)]
                                                                    |
                                        +---------------------------v---------------------------+
                                        |                      KERNEL SPACE                     |
                                        |                                                       |
                                        |  [Syscall Handler] --> [VFS] --> [FS Drivers]         |
                                        |          |               |             |              |
                                        |          v               v             v              |
                                        |    [Process Mgr]    [Block/Char Devs] [Storage Drv]   |
                                        |          |                                            |
                                        |          v                                            |
                                        |    [Scheduler] --> [Hardware (CPU, RAM, I/O)]         |
                                        +-------------------------------------------------------+
```


## 3. Core Components

### 3.1. Userland (Frontend)

Name: Userland Utilities & Shell

Description: The user-facing interface of the OS, comprising the shell (`bin/sh`) and standard Unix utilities. It interacts with the kernel via system calls to perform file operations, process management, and I/O.

Technologies: C11 (LibC), Assembly (Startup), Makefiles

Deployment: Staged in `dist/` and bundled into the final OS image.

### 3.2. Kernel Services (Backend)

#### 3.2.1. Process Management

Name: Process & Thread Manager

Description: Manages the lifecycle of processes and threads, including creation (`fork`, `exec`), scheduling (MLFQ), and termination. Handles signals and process groups.

Technologies: C, Inline Assembly (Context Switching)

Location: `sys/pm/`, `sys/kern/`, `sys/arch/i386/sched.c`

#### 3.2.2. Memory Management

Name: Virtual & Physical Memory Manager

Description: Manages physical RAM (PMM) and virtual address spaces (PMAP). Implements paging, demand allocation, copy-on-write, and kernel memory pools.

Technologies: C, x86 Paging Structures

Location: `sys/vm/`, `sys/arch/i386/pmm.c`, `sys/arch/i386/pmap.c`

#### 3.2.3. Virtual File System (VFS)

Name: VFS Layer

Description: Abstracts specific filesystem implementations. Provides a uniform API (`open`, `read`, `write`) for file access and handles path resolution, mounting, and file descriptors.

Technologies: C

Location: `sys/vfs/`, `sys/fs/`

#### 3.2.4. Hardware Drivers

Name: Device Drivers

Description: Interfaces with physical hardware. Includes Video (VGA/BGA), Input (PS/2), Storage (IDE/AHCI/NVMe/VirtIO), and Serial (UART).

Technologies: C, x86 I/O Ports, MMIO

Location: `sys/drivers/`

## 4. Data Stores

### 4.1. Filesystems

Name: Persistent Storage

Type: Ext2, FAT, Minix, UDF

Purpose: Stores user data, system configuration, and binaries on disk.

Key Structures: Inodes, Superblocks, Directory Entries.

### 4.2. Memory Structures

Name: Kernel Data Structures

Type: In-Memory Linked Lists, Radix Trees, Bitmaps

Purpose: Manages runtime state such as the Process Table, Open File Table, and Page Frame Database.

## 5. External Integrations / APIs

Service Name: Host System (for Testing)

Purpose: The build system supports a "Host Build" mode (`make host_dist`) to compile core utilities using the host's LibC. This allows logic verification on Linux/BSD before running on the target OS.

Integration Method: `NATIVE_BUILD=1` flag in Makefiles.

## 6. Deployment & Infrastructure

Cloud Provider: N/A (Runs on bare metal or Virtual Machines like QEMU, Bochs, VirtualBox)

Key Services Used: QEMU (Emulation), Bochs (Debugging), GCC Cross-Compiler

CI/CD Pipeline: GitHub Actions (builds kernel, runs tests)

Monitoring & Logging: Serial Console (COM1), VGA Console, `sys/kern/debug.c`

## 7. Security Considerations

Authentication: Basic Unix permissions (UID/GID). `login` and `su` utilities (planned/stubbed).

Authorization: File permission bits (rwx) enforced by VFS. Ring 0 (Kernel) vs Ring 3 (User) isolation enforced by CPU segmentation/paging.

Data Encryption: None currently implemented.

Key Security Tools/Practices:
- Kernel Stack Safety Checks
- Argument validation in System Calls (`copyin`/`copyout`)
- User/Kernel Address Space separation

## 8. Development & Testing Environment

Local Setup Instructions: `make` to build everything. `make debug` to run in QEMU.

Testing Frameworks:
- **Unit Tests:** `tests/unit/` (Kernel subsystems)
- **Integration Tests:** `tests/sys/` (System calls)
- **LibC Tests:** `tests/lib/c/` (Standard library compliance)

Code Quality Tools: `-Wall -Werror` compiler flags, strict strict typing in kernel.

## 9. Future Considerations / Roadmap

- **x86_64 Port:** Expand `sys/arch/x86_64` stub to full support.
- **Networking:** Implement TCP/IP stack and Network Interface Card (NIC) drivers.
- **SMP:** Complete Symmetric Multi-Processing support (currently in Beta).
- **Dynamic Linking:** Full `ld.so` implementation for shared libraries.

## 10. Project Identification

Project Name: Substrate OS

Repository URL: [Internal]

Primary Contact/Team: [Internal]

Date of Last Update: 2026-01-01

## 11. Glossary / Acronyms

**PMM:** Physical Memory Manager

**PMAP:** Physical Map (Virtual Memory Manager layer)

**VFS:** Virtual File System

**GDT:** Global Descriptor Table

**IDT:** Interrupt Descriptor Table

**ISR:** Interrupt Service Routine

**COW:** Copy-on-Write

**MLFQ:** Multilevel Feedback Queue (Scheduler)
