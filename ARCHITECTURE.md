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
- **`sys/drivers/`**: Hardware drivers.
    - **`video/`**: VGA text mode driver.
    - **`serial/`**: UART driver.
    - **`input/`**: PS/2 Keyboard driver.
    - **`storage/`**: Drivers for SCSI, IDE, AHCI, NVMe.
- **`sys/vfs/`**: Virtual File System layer, providing an abstraction over specific file systems.
- **`sys/fs/`**: File system implementations.
    - **`ext2/`**, **`fat/`**, **`exfat/`**, **`minix/`**.
    - **`exec/`**: Binary loaders (ELF, PE).
        - **`perso/`**: Execution Personalities (Native, Linux, FreeBSD) handling syscall translation.
- **`sys/kern/`**: Kernel subsystems.
    - **Scheduling**: 1:1 Threading model (`sched.c`), Process/Thread management.
    - **Time**: System time and tick handling.
    - **Accounting**: Process accounting (`acct.c`).
- **`sys/sys/`**: System-wide header definitions (`proc.h`, `file.h`, `acct.h`, `thr.h`).

### Core Userland (`bin/`, `lib/`)
These components are essential for booting and basic system operation.
- **`bin/`**: Fundamental Unix utilities (`sh`, `ls`, `cp`, `mv`, `rm`, `mkdir`, `cat`, `grep`, `wc`, `ps`, `kill`, `sync`, etc.).
- **`lib/`**:
    - **`c/`**: Standard C library (libc) (C11 compliant). Includes `stdio` (buffered I/O), `stdlib`, `string`, `unistd`, `dirent`, `time`, `pwd`, `grp`.
    - **`m/`**: Math library.
    - **`dl/`**: Dynamic linker.
    - **`pthreads/`**: POSIX Threads library (wraps `thr_new`).
    - **`dbm/`**: Database Manager library.
- **`sbin/`**: System administration binaries.

## Design Patterns & Standards
- **ABIs:**
  - **C:** Standard Intel C ABI.
  - **Syscalls:** Interrupt `0x80`. Supports multiple personalities:
    - **Native (TestUnix):** Custom syscalls (`sys_thr_new`, etc.).
    - **Linux i386:** Compatibility layer (e.g. `sys_clone` mapping).
    - **FreeBSD i386:** Compatibility layer.
- **Tooling:** Built with modern GCC (`-m32`).
- **Threading Model:**
  - **1:1 Model:** Kernel threads are first-class citizens.
  - **Scheduler:** Round-Robin with support for Processes and Threads.
- **Exec:** ELF binaries are "branded" via `EI_OSABI` to select the correct personality.

## Data Storage & File System
- **VFS:** Abstraction layer handling `open`, `read`, `write`, `close`, `readdir`.
- **FD Management:** Per-process File Descriptor table.

## Integration points
- **Syscall Interface:** Defined in `sys/arch/i386/syscall.c` and `sys/exec/perso/`.
- **Boot:** Multiboot header in `sys/arch/i386/boot.S`.