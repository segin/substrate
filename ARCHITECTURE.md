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

## 3. Subsystem Breakdown
- **`sys/core/`**: Central kernel logic, including the entry point (`kmain`), versioning, and kernel-wide initialization.
- **`sys/arch/`**: Architecture-specific code.
    - **`i386/`**: 32-bit x86 support.
        - **Boot**: Multiboot compliant (`boot.S`).
        - **Subsystems**: IDT, GDT, PMM, Syscalls (int 0x80), FPU Emulation (`fpu/`).
        - **GDT Segment Layout**:
            - `0x08`: Kernel Code Segment
            - `0x10`: Kernel Data Segment
            - `0x1B`: User Code Segment (0x18 | RPL 3)
            - `0x23`: User Data Segment (0x20 | RPL 3)
            - `0x28`: TSS
            - `0x33`: TLS Segment (0x30 | RPL 3) - Used for Thread-Local Storage (GS)
        - **Physical Memory Manager (PMM):**
            - **Buddy Allocator:** O(log N) allocation/free with automatic page coalescing.
            - **Orders:** 0-10 (4KB to 4MB blocks).
            - **Free Lists:** Per-order doubly-linked lists for O(1) enqueue/dequeue.
            - **Initialization:** `pmm_buddy_init_range()` populates free lists with maximum-order blocks.
            - **Watermark Allocator:** Early boot bump allocator used before buddy system is ready. Memory is never freed.
            - **Bitmap:** Kept for diagnostics only, not used for allocation decisions.
            - **No Static Limit:** Dynamic metadata sizing supports all detected RAM.
        - **Virtual Memory Manager (PMAP):**
            - **Per-Process Address Spaces:** Each process has its own `pmap_t` representing its virtual address space:
                - **User Space:** 0x00000000 - 0xBFFFFFFF (3GB, PDEs 0-767)
                - **Kernel Space:** 0xC0000000 - 0xFFFFFFFF (1GB, PDEs 768-1023, shared by reference)
                - Kernel PDEs are shared between all pmaps, not copied.
            - **Recursive Paging:** Self-reference at PDE 1023 (0xFFC00000) allows O(1) page table manipulation.
            - **Protection:** `pmap_protect` walks ranges to update R/W/U bits and invalidate TLBs.
            - **Copy-on-Write (COW):** `pmap_copy` implements fork() optimization by marking pages read-only and sharing physical frames until write fault.
            - **Global Pages:** Uses PGE (if available) for kernel mappings (0xC0000000+) to minimize TLB flushes on context switch.
            - **Fast Paths:** `pmap_kenter`/`pmap_kremove` for low-overhead kernel mappings without locking.
            - **Hardware Mapping:** Identity-maps critical I/O regions like the Local APIC (0xFEE00000) during bootstrap to support safe early-boot spinlock operations once paging is enabled.
            - **Dynamic PT Allocation:** Page tables are allocated on-demand (~4KB per 4MB mapped) to minimize per-process overhead.
- **`sys/drivers/`**: Hardware drivers.
    - **`video/`**: Unified Video Adapter driver (`vga.c`).
        - **Supported Hardware:** VGA (Standard), EGA, CGA, Hercules (HGC), BGA (Bochs).
        - **Modes:** 
            - Standard VGA: Mode 12h (640x480 16-color planar), Mode 13h (320x200 256-color linear).
            - Legacy: CGA Mode 4 (320x200 4-color), Hercules (720x348 Monochrome).
        - **Fonts:** Compiled-in CP437 fonts (`font_8x16.c`, `font_8x8.c`) covering full 256 charsets.
        - **Architecture:** Table-driven mode setting with specific CRTC register dumps (6845/VGA).
    - **`serial/`**: UART driver.
- **`console/`**: TTY core and console device driver stack.
    - **Current Design**: Monolithic TTY implementation handling canonical processing and signal generation in common paths.
    - **Planned Refactor**: Transitioning to a pluggable **Line Discipline (ldisc)** interface to support alternative disciplines (PPP, SLIP) and better separation of concerns (POSIX canonical processing vs. raw I/O).
    - **Interface**: Uses a `tty_driver` callback interface for hardware interaction.
    - **`input/`**: PS/2 Keyboard and Mouse drivers.
    - **`storage/`**: Drivers for SCSI, IDE, AHCI, NVMe.
    - **`virtio/`**: Virtualized devices (Block, 9P, Net).
- **`sys/vfs/`**: Virtual File System layer, providing an abstraction over specific file systems. Supports `unlink` and `link` for file management.
- **`sys/fs/`**: File system implementations.
    - **`ext2/`**, **`fat/`**, **`exfat/`**, **`minix/`**.
    - **`exec/`**: Binary loaders (ELF, PE).
        - **`perso/`**: Execution Personalities (Native, Linux, FreeBSD) handling syscall translation.
- **`sys/kern/`**: Kernel subsystems.
    - **Scheduling**:
      - **Algorithm:** Multilevel Feedback Queue (MLFQ) with Realtime, Timeshare, and Idle priority classes.
      - **SMP Support:** Per-CPU Runqueues, Work Stealing load balancing, CPU Affinity, and IPI preemption.
      - **Synchronization:**
        - **Spinlocks:** SMP-safe locking.
        - **Turnstiles:** Priority Inheritance for Mutexes.
        - **Sleep Queues:** Hashed O(1) wait queues.
    - **Process Model**:
      - **PID 0 (TID 0):** Swapper/Idle task.
      - **PID 1 (TID 1):** Init process (spawned by kernel).
      - **PID = MainTID:** Invariant enforced for all new processes.
      - **Process Groups (pgrp):** Used for job control and signal delivery. Inherited on fork.
      - **Sessions:** High-level grouping for terminal control.
    - **Time**: System time and tick handling.
    - **Accounting**: Process accounting (`acct.c`).
    - **Signals**:
      - **Implementation:** `signal.c` handles delivery and state.
      - **Group Signaling:** `signal_send_group` allows targeting all processes in a `pgrp`.
      - **TTY Integration:** Key presses (`^C`, `^\`, `^Z`) trigger signals (`SIGINT`, `SIGQUIT`, `SIGTSTP`) to the foreground process group.
    - **Process Management (`pm/`)**:
      - **`proc_find(pid)`:** Kernel API to look up a process by PID. Returns `process_t*` or NULL. See `proc_find(9)`.
      - **`proc_create(pers)`:** Create a new process with given personality.
      - **`proc_fork(parent, stack)`:** Fork a process with COW address space.
      - **`proctree_lock`:** Mutex protecting process hierarchy modifications.

### Core Userland (`bin/`, `lib/`)
These components are essential for booting and basic system operation.
- **`bin/`**: Fundamental Unix utilities (`sh`, `ls`, `cp`, `mv`, `rm`, `mkdir`, `cat`, `grep`, `wc`, `ps`, `kill`, `sync`, etc.).
- **`usr.bin/`**: User tools (`compress`, `uncompress`, `zcat`, `yacc`, `brandelf`).
- **`include/`**: Userspace C library headers (shared by all userspace libraries).
- **`lib/`**:
    - **`c/`**: Standard C library (libc) (C11 compliant). Includes `stdio` (buffered I/O), `stdlib`, `string`, `unistd`, `dirent`, `time`, `pwd`, `grp`.
    - **`sys/`**: System call wrapper library (libsys). Provides raw `syscall()` function and typed wrappers for kernel syscalls. See `libsys(7)`.
    - **`m/`**: Math library (libm). C99 compliant IEEE 754 floating-point support:
        - **Classification:** `fpclassify()`, `isfinite()`, `isinf()`, `isnan()`, `isnormal()`, `signbit()`.
        - **Basic Arithmetic:** `fabs()`, `fmod()`, `remainder()`, `fmax()`, `fmin()`.
        - **Rounding:** `ceil()`, `floor()`, `trunc()`, `round()`.
        - **Trigonometric:** `sin()`, `cos()`, `tan()`, `asin()`, `acos()`, `atan()`, `atan2()` (stubs).
        - **Exponential:** `exp()`, `log()`, `log10()`, `pow()`, `sqrt()` (stubs).
        - **Type Variants:** Float (`f` suffix) and long double (`l` suffix) versions.
        - **Error Handling:** `math_errhandling` set to `MATH_ERRNO`.
    - **`dl/`**: Dynamic linker.
    - **`pthreads/`**: POSIX Threads library (wraps `thr_new`).
    - **`dbm/`**: Database Manager library.
- **`libexec/`**:
    - **`ld.so`**: Dynamic linker/loader for ELF shared objects.
        - **Features**: PT_TLS support, GNU hash lookups, lazy/eager binding, and secure-exec handling for setuid binaries.
        - **Policy**: Follows BSD-style search paths (`/lib`, `/usr/lib`, `/usr/local/lib`) and System V ELF ABI.
- `sbin/`: System binaries (Currently empty/stubbed as we rely on external rootfs/busybox for init).


- **Naming Conventions & Namespaces:**
    - **Network Interfaces:** Naming follows the `driver`+`instance` pattern (BSD-style).
      - Examples: `em0` (Intel PRO/1000), `re0` (Realtek 8139/8169), `bge0` (Broadcom), `lo0` (Loopback).
    - **Storage Devices:** Naming follows the `/dev/storage/`+`type`+`instance` pattern.
      - **Types:**
        - `ide`: Legacy IDE devices (e.g., `/dev/storage/ide0`).
        - `sata`: SATA devices (e.g., `/dev/storage/sata0`).
        - `scs` / `scsi`: SCSI devices (e.g., `/dev/storage/scsi0`).
        - `usb`: USB Mass Storage.
        - `nvme`: NVMe Namespaces (e.g., `nvme0`).
        - `floppy`: Floppy Disk.
        - `optical`: CD-ROM/DVD (ATAPI/SCSI).
      - **Partitions:**
        - **MBR/BSD Slices:** `s1`, `s2` (e.g., `/dev/storage/sata0s1`).
        - **BSD Labels:** `a`-`h` suffix inside a slice (e.g., `/dev/storage/sata0s1a`).
        - **GPT:** `p1`, `p2` (e.g., `/dev/storage/nvme0p1`).
- **Audio API:**
    - **Native:** Sun AudioIO (`/dev/audio`, `ioctl` based) for simplicity and POSIX-like design.
    - **Compatibility:** OSS v3/v4 emulation provided via `ossp` personality or userland wrapper.
- **Kernel Object Namespace (KObject):**
    - All kernel subsystems (Drivers, Buses, Classes) are registered in a hierarchical object tree.
    - Rooted at `/sys` (exported via SysFS).
    - Provides reference counting (`kref`) and unified lifecycle management.

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
