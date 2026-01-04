# AGENTS.md

## Project Description
This is an operating system project targeting x86 32-bit architecture (with x86_64 plans). The goal is to build a Unix-like system with a kernel, standard utilities, and libraries, capable of running native, Linux, and FreeBSD binaries via personality emulation.

## Technical Constraints & Standards
- **Architecture:** x86 32-bit (primary), x86 64-bit (planned/stubbed).
- **C ABI:** Standard Intel C ABI.
- **Toolchain:** Modern GCC (`-m32`, `-nostdlib`, `-fno-builtin`).
- **Userland Linker Flags:** `-m32 -nostdlib -fno-pie`.

## Recent Accomplishments
- **PMM Hardening:** Phase 1 boot memory detection with sanitization, total RAM reporting, and proper kernel bounds.
- **Per-Process Address Spaces:** Implemented `pmap_create()` and `pmap_destroy()` for full process isolation with 3GB/1GB user/kernel split
- **FPU State Tracking:** Lazy FPU context switching with FXSAVE/FXRSTOR
- **Filesystem Timestamps:** Added atime/mtime/ctime tracking with atomic updates
- **TTY Integration:** Per-process controlling terminal support
- **Time System:** 64-bit time_t, RTC driver, gettimeofday/clock_gettime syscalls
- **VirtIO Drivers:** Implemented Core VirtIO, Block Device (virtio-blk), and 9P Transport (virtio-9p) drivers.
- **PS/2 Subsystem:** Expanded PS/2 controller driver to support dual-channel (Mouse/Aux) operation.
- **Framebuffer:** Implemented native linear framebuffer driver (`fb.c`) with Multiboot support and bitmap font console. Added Bochs Graphics Adapter (BGA) native driver support via `video=bga`.
- **Build System:** Root filesystem generation in `dist/`

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
- User process foundation complete (pmap layer)
- VirtIO drivers (Block, 9P) linked and initialized
- Ready for mmap() implementation

## Directives
1.  **Architecture Maintenance:** Always read `ARCHITECTURE.md` before starting complex tasks. Update `ARCHITECTURE.md` if your changes impact the system structure or design.
2.  **Code Style:** Adhere to standard kernel coding styles (similar to BSD/Linux) for C and C++.
3.  **Documentation:** Keep documentation close to the code.
4.  **Safety:** Always verify file contents before replacing.
5.  **Build System:** Maintain the recursive Makefile structure. Ensure `make -C sys`, `make -C lib/c`, and `make -C bin` always pass.
6.  **Git Operations:** Use `git mv` and `git rm` for file operations to preserve history.

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
- Stack corruption: Occasional Page Faults (ERR 5) in `sh` being investigated.

## Next Steps
- Implement mmap() syscall with personality driver integration
- Hook Linux and FreeBSD personalities to native mmap
- Flesh out 9P filesystem logic implementation
- Create comprehensive tests
