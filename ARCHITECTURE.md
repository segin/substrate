# ARCHITECTURE.md

## High-Level System Overview
This project implements a 32-bit x86 operating system. It follows a traditional Unix-like monolithic kernel design with a distinct separation between kernel space and user space.

## Core Components

### Kernel (`sys/`)
The kernel is the core of the operating system, structured as follows:

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
    - **`console/`**: TTY core and console device driver stack, using a `tty_driver` callback interface (install/remove, open/close, write/put_char, buffer state queries, and flow-control hooks).
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
- **`sys/sys/`**: System-wide header definitions (`proc.h`, `file.h`, `acct.h`, `thr.h`, `termios.h`, `signal.h`).

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
- `sbin/`: System binaries (Currently empty/stubbed as we rely on external rootfs/busybox for init).

## Personality Emulation
- **Linux:** Emulates Linux 2.6.x i386 syscalls. Handles `rt_sigaction` (174) and `rt_sigprocmask` (175) by mapping to internal signal infrastructure.
- **FreeBSD:** Planned support for FreeBSD 8/10+ i386 binaries.

## Build System & Host Tools
The project supports generating a complete set of native tools for the host operating system (Linux/BSD) to facilitate testing and cross-compilation independent of the target environment.

### Host Distribution (`host_dist`)
Running `make host_dist` builds and installs the core utilities into a local `host_dist/` directory. This includes:
- **`bin/`**: `sh`, `ls`, `cp`, `mv`, `rm`, `mkdir`, `cat`, `grep`, `wc`, `ps`, etc.
- **`usr/bin/`**: `yacc`, `brandelf`.
- **`sbin/`**: `mkfs`, `fsck`.

These tools are compiled using the host's compiler (`cc`) and C library, but strictly adhering to the project's own Makefiles and source code, allowing verification of logic and behavior on a stable host.

> [!CAUTION]
> **Host Builds NEVER use Substrate's libc.** When `NATIVE_BUILD=1` is set, programs link against the host OS's standard C library (glibc, musl, etc.), not `lib/c/`. The Substrate libc (`lib/c/`, `lib/sys/`) is exclusively for the Substrate kernel and target binaries. Never modify these libraries to support Linux or other host operating systems.

## Recent Progress (as of Jan 2026)
- Implemented `sys_brk` for dynamic heap allocation.
- Stabilized BusyBox TLS (GS segment and Variant II offsets).
- Resolved shell input race conditions via atomic sleep in `console_read`.
- Upgraded syscall handler to 6-register passing.

## Design Patterns & Standards
- **ABIs:**
  - **C:** Standard Intel C ABI.
  - **Syscalls:** Interrupt `0x80`. Supports multiple personalities with distinct ABIs:
    - **Native (Substrate):** BSD-style calling convention. Arguments are passed on the stack. Syscall number in `EAX`.
    - **Linux i386:** Linux-style calling convention. Arguments in registers (`EBX`, `ECX`, `EDX`, `ESI`, `EDI`, `EBP`). Syscall number in `EAX`.
    - **FreeBSD i386:** BSD-style calling convention (Stack-based).
- **Tooling:** Built with modern GCC (`-m32`).
- **Threading Model:**
  - **BSD-style:** 1:1 Kernel threading model using `kthread` infrastructure.
  - **Userspace:** POSIX Threads (pthreads) implemented via `libthr` wrapping kernel primitives.
  - **Scheduler:** Round-Robin with support for Processes and Threads.
- **Exec:** ELF binaries are "branded" via `EI_OSABI` to select the correct personality.

## Naming Conventions & Namespaces
- **Network Interfaces:** Naming follows the `driver`+`instance` pattern (BSD-style).
  - Examples: `em0` (Intel PRO/1000), `re0` (Realtek 8139/8169), `bge0` (Broadcom), `lo0` (Loopback).
- **Storage Devices:** Naming follows the `/dev/storage/`+`type`+`instance` pattern.
  - **Types:**
    - `sata`: SATA devices (AHCI).
    - `ide`: Legacy IDE devices.
    - `scsi`: SCSI devices.
        - `/dev/storage/scsi/B:T:L`: Generic SCSI access (Bus:Target:LUN).
        - `/dev/storage/scsi/B`: Bus controller (ioctl enumeration).
        - `/dev/storage/scsiN`: High-level block device alias (e.g., `scsi0` -> first disk).
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

## Framebuffer Interface
- **Device Node:** `/dev/fb0` (Character Device).
- **Access Method:**
  - **MMAP:** Direct access to video memory.
    - Uses standard `mmap()` syscall.
    - Kernel interprets offset as page offset (4096-byte units) to support large framebuffers on 32-bit systems (similar to Linux `mmap2`).
    - Libc wrapper handles `int64_t` byte offset -> page index conversion.
  - **IOCTL:** Screen information retrieval.
    - `FBIOGET_VSCREENINFO`: Fills `struct fb_var_screeninfo` with width, height, bpp, etc.
- **Header:** `sys/fb.h` defines `struct fb_var_screeninfo`.
- **Native Initialization:**
  - **Multiboot:** Default method, relies on bootloader video setup.
  - **BGA (Bochs Graphics Adapter):** Activated via kernel command line `video=bga`. Sets 1024x768x32 resolution directly via I/O ports `0x1CE`/`0x1CF`.

## System Calls & ABI
- **Mechanism:** Interrupt `0x80`.
- **Arguments:** Passed in registers `EBX`, `ECX`, `EDX`, `ESI`, `EDI`, `EBP`.
- **Return Value:** `EAX` (low 32-bits), `EDX` (high 32-bits).
- **64-bit Support (on 32-bit):**
  - **Off_t / Time_t:** 64-bit signed integers.
  - **Stat:** `struct stat` uses 64-bit fields for size, blocks, and timestamps (nanosecond precision).
  - **Lseek:** `sys_lseek` accepts `off_lo` and `off_hi` to form 64-bit offset.
  - **Mmap:** `sys_mmap` accepts `uint32_t` page_offset (offset / 4096) as the 6th argument using `_syscall6`.
- **Boot:** Multiboot header in `sys/arch/i386/boot.S`.
- **System Calls**: Supports `unlink` and `link` (native/Linux/FreeBSD) for file management.
- **System Stability**: Features identity syscall stubs for BusyBox and a kernel-stack safety check in the syscall dispatcher.
- **Lost Wakeup Protection**: The console driver uses interrupt masking (`cli`/`sti`) to prevent race conditions during blocking reads.
