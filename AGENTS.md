# AGENTS.md

## Project Description
This is an operating system project targeting x86 32-bit architecture (with x86_64 plans). The goal is to build a Unix-like system with a kernel, standard utilities, and libraries, capable of running native, Linux, and FreeBSD binaries via personality emulation.

## Technical Constraints & Standards
- **Architecture:** x86 32-bit (primary), x86 64-bit (planned/stubbed).
- **C ABI:** Standard Intel C ABI.
- **Toolchain:** Modern GCC (`-m32`, `-nostdlib`, `-fno-builtin`).
- **Userland Linker Flags:** `-m32 -nostdlib -fno-pie`.

## Recent Accomplishments
- **VFS Hard Link Support:** Implemented `link` in VFS and hooked up `sys_link` across native, Linux, and FreeBSD personalities. Improved ABI detection for stack-based syscalls.
- **VFS Unlink Support:** Implemented `unlink` in VFS and hooked up `sys_unlink` across native, Linux, and FreeBSD personalities.
- **Per-Process Address Spaces:** Implemented `pmap_create()`, `pmap_destroy()`, `pmap_reference()`, `pmap_release()`, `pmap_fork()` with COW support. Global pmap list for TLB management. Full 3GB/1GB user/kernel split.
- **PMM Hardening:** Phase 1 boot memory detection with sanitization, total RAM reporting, and proper kernel bounds.
- **FPU State Tracking:** Lazy FPU context switching with FXSAVE/FXRSTOR
- **Filesystem Timestamps:** Added atime/mtime/ctime tracking with atomic updates
- **TTY Integration:** Per-process controlling terminal support
- **Time System:** 64-bit time_t, RTC driver, gettimeofday/clock_gettime syscalls
- **VirtIO Drivers:** Implemented Core VirtIO, Block Device (virtio-blk), and 9P Transport (virtio-9p) drivers.
- **PS/2 Subsystem:** Expanded PS/2 controller driver to support dual-channel (Mouse/Aux) operation.
- **Framebuffer:** Implemented native linear framebuffer driver (`fb.c`) with Multiboot support and bitmap font console. Added Bochs Graphics Adapter (BGA) native driver support via `video=bga`.
- **Build System:** Root filesystem generation in `dist/`
- **Test Framework:** Implemented comprehensive kernel test runner (`sys/tests`), integrated into build system, with initial PMAP property tests.
- **Kernel sprintf Enhancements:** Added printf flags: `-`, `+`, ` `, `#`, `0`, numeric width, and conversions: d/i/u/o/x/X/p/s/c for improved debug output
- **Process Model Refactor:** Separated Swapper (PID 0) and Init (PID 1). Enforced `PID == Main_TID` invariant. Added Process Group (`pgrp`) and Session support.
- **TTY Signals:** Implemented signal generation from TTY (`SIGINT`, `SIGQUIT`, `SIGTSTP`) and group signal delivery (`signal_send_group`).
- **Build System:** Fixed `dist` directory generation to include standard Unix hierarchy (`usr/include`, `usr/local`, etc.) and ensured `vmunix` installation.
- **Boot/Init:** Cleaned up `sbin` build process; kernel now boots correctly with `root=/dev/hda` for external root filesystems, attempting fallback to `init` search path.


## Current Status
- **PMM Refactor:** Phase 2 complete.
    - O(log N) Buddy Allocator with proper page coalescing.
    - Contiguous allocation optimized via Buddy system.
    - Bootstrap Watermark Allocator implemented.
    - Dynamic Metadata sizing (no 128MB limit) implemented.
    - Low Memory safeguards active.
    - Fixed double-free bugs and initialization issues.
    - Bootstrap Watermark Allocator implemented.
    - Dynamic Metadata sizing (no 128MB limit) implemented.
    - Low Memory safeguards active.
- **User process foundation complete (pmap layer):**
    - Recursive paging and global page support.
    - `pmap_protect` and `pmap_copy` with Copy-on-Write (COW).
    - `pmap_kenter`/`pmap_kremove` kernel fast paths.
    - Identity-mapping for Local APIC (0xFEE00000) during bootstrap.
    - Identity-mapping for Local APIC (0xFEE00000) during bootstrap.
- **Process Model Refactored:**
    - Swapper: PID 0 (TID 0).
    - Init: PID 1 (TID 1).
    - Init spawned via `sched_spawn_kernel_process` and transitions via `execve`.
- VirtIO drivers (Block, 9P) linked and initialized
- **PT_TLS Support:** ELF loader now handles PT_TLS segment, allocates TLS block, sets GDT entry 6 for GS-based TLS access
- **VGA Hardware Cursor:** Fixed to sync with software cursor position
- Debugging remaining TLS access issue (ESI pointing to PT_TLS template instead of allocated block)

## Directives
1.  **Architecture Maintenance:** Always read `ARCHITECTURE.md` before starting complex tasks. Update `ARCHITECTURE.md` if your changes impact the system structure or design.
2.  **Code Style:** Adhere to standard kernel coding styles (similar to BSD/Linux) for C and C++.
3.  **Documentation:** Keep documentation close to the code.
4.  **Safety:** Always verify file contents before replacing.
5.  **Build System:** Maintain the recursive Makefile structure. Ensure `make -C sys`, `make -C lib/c`, and `make -C bin` always pass.
6.  **Git Operations:** Use `git mv` and `git rm` for file operations to preserve history.
7.  **TASKS.md Work Methodology:** Complete ONE checkbox at a time, update docs/specs/database/unit/property/fuzzing tests as applicable, commit, push. This applies to ALL checkboxes in `TASKS.md`, not just PMAP work.
8.  **Memory Management:** Always prefer `AGENTS.md` over `GEMINI.md` if both are present. Ensure `GEMINI.md` is not merely a symbolic link to `AGENTS.md` before treating it as separate.

## Directory Structure Overview
- `sys/`: Kernel source.
    - `core/`: Main entry (`kmain`).
    - `arch/`: Architecture specific (`i386`, `x86_64`).
        - `i386/pmap.c`: Virtual memory management (pmap layer)
        - `i386/gdt.c`: GDT with verified segments (0x1B/0x23/0x33)
        - `i386/fpu/`: FPU emulation and state management
    - `drivers/`: Hardware drivers (`video`, `serial`, `input`, `storage`).
    - `fs/`: Filesystems (`ext2`, `fat`, `minix`, `exec`).
    - `kern/`: Kernel core subsystems (Scheduler, Time, Acct).
    - `exec/perso/`: Personality implementations.
    - `sys/`: System headers (`proc.h`).
- `bin/`: User-space utilities (`ls`, `sh`, `vi`, etc.).
- `lib/`:
    - `c/`: LibC implementation.
    - `pthreads/`: Threading support.
    - `dbm/`: Database library.
- `sbin/`: System binaries.

### Debugging Note
If the kernel hangs in `hlt`, check `eflags` bit 9. If `IF=1`, the IRQ may be masked at the PIC or the controller state is stuck.

## Known Issues
- Interrupt responsiveness: `Ctrl+F9` debug dump sometimes fails during idle states.
- **TLS Template Access:** After PT_TLS setup, some code accesses PT_TLS template (0x0811EE18) instead of using GS-relative addressing.
- **PMAP Memory Overhead:** 32 identity-mapped kernel PDEs (0-31) in userspace consume 128KB+ per process.

## Next Steps
- Debug remaining TLS access issue (ESI=0x0811EE18 crash)
- Refactor PMAP to dynamically allocate page tables (reduce 128KB overhead)
- Implement mmap() syscall with personality driver integration
- Flesh out 9P filesystem logic implementation
