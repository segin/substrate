# Architecture

This document defines Substrate as a full Unix operating system architecture.
It is a living technical baseline for kernel, userland, libraries, and toolchain.

## 1. System Identity

Substrate is a complete Unix OS project with four first-class pillars:
- Kernel (`sys/`)
- Userland programs (`bin/`, `sbin/`, `usr.bin/`)
- Runtime/system libraries (`lib/`, `usr.lib/`)
- Native toolchain (`usr.bin/cc`, `usr.bin/as`, `usr.bin/ld`, `usr.lib/elfobj`)

Primary target architecture is i386. x86_64 support is active and expanding.

## 2. Design Principles

- **System-First Design:** Focus on the integrated behavior of the entire OS.
- **ABI Correctness:** Prioritize stable, correct interfaces before implementation convenience.
- **Native Autonomy:** Aim to own the entire stack, including the C toolchain.
- **Testable Contracts:** Ensure kernel/userspace/toolchain boundaries are explicit and verified.

## 3. Top-Level Architecture

```text
User Programs
  -> libc/libsys/libm/... (userspace ABI)
  -> syscall boundary
  -> kernel services (proc/vm/vfs/drivers/personality)
  -> hardware

Build Toolchain
  cc/cpp -> as -> ld
      \\         /
       \\-> libelfobj
```

Substrate is developed as one integrated system where the toolchain, libraries, and kernel co-evolve to maintain ABI integrity.

## 4. Source Tree Architecture

```text
sys/         kernel
bin/         base Unix userland
sbin/        system utilities
usr.bin/     compiler/toolchain and extended user tools
lib/         target runtime libraries (libc/libsys/libm/libpthread/libusb...)
usr.lib/     shared libraries for tooling/runtime support (elfobj, demangle, ...)
include/     userspace public headers
tests/       unit/integration/regression/property/fuzz harnesses
docs/specs/  detailed subsystem specs
docs/tasks/  refactored task planning sections
dist/        target root filesystem staging
host_dist/   host install staging for native validation tools
usr.man/     manual page source tree
```

Detailed staging rules for `dist/` are defined in `docs/specs/rootfs.md`.

## 5. Kernel Architecture

The kernel remains monolithic and is organized into logical layers:
- `sys/arch/`: Architecture-specific implementation (CPU/MMU/Bootstrap).
- `sys/core/`: Early initialization and global startup.
- `sys/kern/`: Core services (Scheduler, Signals, Time, Sync).
- `sys/pm/`: Process management and lifecycle.
- `sys/vm/`: Memory management (PMM, PMAP, VM objects).
- `sys/vfs/` and `sys/fs/`: Virtual Filesystem and concrete implementations.
- `sys/drivers/`: Device driver framework and hardware drivers.
- `sys/exec/`: Executable loading and execution personalities.

### Subsystem Specifications
- **Boot and Initialization:** See `docs/specs/kmain_init.md` and `docs/specs/arch_i386_boot.md`.
- **Memory Management:** See `docs/specs/pmm.md` (Physical) and `docs/specs/pmap.md` (Virtual).
- **Process Model:** See `docs/specs/kern_process_exit.md` and `docs/specs/kern_pid1.md`.
- **VFS and Filesystems:** See `docs/specs/vm_subsystem.md` (File cache) and `docs/specs/fs_devfs.md`.
- **Buffer Cache (`bio`):** BSD-style cache in `sys/vfs/buf.h` + `sys/vfs/bio.c` with hash lookup by `(vnode, blkno)`, queueing (`BQ_LOCKED`, `BQ_CLEAN`, `BQ_DIRTY`, `BQ_EMPTY`), delayed write, `sync()` integration, and 30s syncer kthread.
- **VFS Locking (`lockmgr`):** BSD-style lock manager in `sys/kern/lockmgr.c` providing `struct lock` with shared/exclusive/upgrade/downgrade/drain modes and priority inheritance via turnstiles. Vnode locking (`vn_lock`/`vn_unlock`) delegates to `lockmgr()`. Name cache protected by `rwlock_t`. Mount points protected by `rwlock_t mnt_lock`. Buffer cache uses per-buffer `B_BUSY` flag with `spinlock_t` + `sleepq`.
- **Device Model:** See `docs/specs/driver_model.md`.
- **Console and VT:** See `docs/specs/driver_tty.md` and `docs/specs/driver_vt.md`.
- **Personalities:** See `docs/specs/personality_targets.md` and `docs/specs/personality_elks.md`.

## 6. Userland and Libraries

Userland is split by role (essential, admin, extended) and supported by a suite of target-native libraries.

### Key Components
- **`libsys`:** The canonical typed interface for system introspection and control.
- **`find(1)`:** A multi-dialect file hierarchy walker. See `docs/find/architecture.md`.
- **Native Toolchain:** Integrated compiler, assembler, and linker. See `docs/specs/as_spec.md`.

## 7. ABI and Interface Boundaries

Substrate maintains strict ABI boundaries between the kernel and userland, and between toolchain outputs and the runtime loader. Any change to these boundaries requires synchronization across all affected layers and documentation updates.

## 8. Testing Strategy

Testing is a first-class citizen, utilizing a multi-layer approach:
- **Isolated Unit Tests:** Validating individual modules.
- **System Integration Tests:** Verifying toolchain and kernel interoperability.
- **Host-Based Validation:** Using `NATIVE_BUILD=1` for rapid feedback.
- **Property and Fuzz Testing:** Ensuring robustness of parsers and ABI handlers.

For detailed testing policies, see `docs/specs/vm_page.md` (as a template) and the `tests/` directory.

---
*This document focuses on structural and system-oriented architecture. Detailed mechanics belong in the linked subsystem specifications.*
