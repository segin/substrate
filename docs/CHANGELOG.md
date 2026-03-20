# Substrate Development Changelog

Detailed record of major implementation milestones. For current system status, see `AGENTS.md`.

## Kernel Core

### Memory Management
- **PMM Hardening:** Phase 1 boot memory detection with sanitization, total RAM reporting, and proper kernel bounds.
- **Per-Process Address Spaces:** Implemented `pmap_create()`, `pmap_destroy()`, `pmap_reference()`, `pmap_release()`, `pmap_fork()` with COW support. Global pmap list for TLB management. Full 3GB/1GB user/kernel split.
- **PMM Virtual Address API:** `pmm_alloc_block()` and `pmm_alloc_contiguous()` now return kernel virtual addresses (phys + 0xC0000000) instead of physical addresses. `pmm_free_block()` and `pmm_free_contiguous()` expect virtual addresses. Updated all callers in: `pmap.c`, `elf.c`, `sched.c`, `process.c`.
- **UMA Integration:** Integrated FreeBSD-style Universal Memory Allocator (UMA) for kernel memory allocation. `kmalloc`/`kfree` now backed by UMA zones via `vm_kmem.c`. Added `uma_startup()` before `kmem_init()`.

### Process & Scheduling
- **Process Model Refactor:** Separated Swapper (PID 0) and Init (PID 1). Enforced `PID == Main_TID` invariant. Added Process Group (`pgrp`) and Session support.
- **Scheduler Refactor (MLFQ):** Implemented Multilevel Feedback Queue with Realtime, Timeshare, and Idle classes.
- **SMP Scheduler:** Per-CPU runqueues, Work Stealing load balancing, CPU Affinity support, and IPI-based preemption.
- **Kernel Process:** Implemented Swapper/Idle (PID 0) with pageout daemon and idle loop responsibilities.
- **Context Switching:** Validated FPU Lazy Save and refined PCB for thread/process separation.
- **Init Safety:** Kernel now catches `init` process exit (e.g., from detached stdin) and enters an idle loop instead of panicking.

### Synchronization
- **Synchronization Primitives:** Implemented Turnstiles (Priority Inheritance) and Hashed Sleep Queues (O(1) lookup).
- **Synchronization Improvements:** Updated `mutex` and `semaphore` to use `sleepq` for robust thread sleeping (removed ad-hoc `sched_sleep`).
- **VFS Concurrency & Locking:** Implemented BSD-style `lockmgr()` lock manager (`sys/kern/lockmgr.c`) with `struct lock` supporting LK_SHARED, LK_EXCLUSIVE, LK_UPGRADE, LK_DOWNGRADE, LK_DRAIN, LK_NOWAIT, and priority inheritance via turnstiles. Refactored vnode locking (`vn_lock`/`vn_unlock`) to delegate to `lockmgr()`. Wired name cache rwlock and mount point rwlock.

### Signals & TTY
- **TTY Integration:** Per-process controlling terminal support.
- **TTY Signals:** Implemented signal generation from TTY (`SIGINT`, `SIGQUIT`, `SIGTSTP`) and group signal delivery (`signal_send_group`).
- **Syscall Tracing:** Enhanced `syscall_trace` with names, typed arguments (int/hex/ptr/str), return values, and Personality details.

### Time
- **Time System:** 64-bit time_t, RTC driver, gettimeofday/clock_gettime syscalls.
- **Filesystem Timestamps:** Added atime/mtime/ctime tracking with atomic updates.

## VFS & Filesystems
- **VFS Hard Link Support:** Implemented `link` in VFS and hooked up `sys_link` across native, Linux, and FreeBSD personalities. Improved ABI detection for stack-based syscalls.
- **VFS Unlink Support:** Implemented `unlink` in VFS and hooked up `sys_unlink` across native, Linux, and FreeBSD personalities.
- **UDF Filesystem Driver:** Complete read-write UDF (Universal Disk Format) driver per ECMA-167/OSTA spec. On-disk structures in `udf.h`, read-only support in `udf.c`, write support in `udf_write.c`, with unit tests and man pages (`udf.4`, `udf.5`).

## Drivers

### Video & Framebuffer
- **Framebuffer:** Implemented native linear framebuffer driver (`fb.c`) with Multiboot support and bitmap font console. Added Bochs Graphics Adapter (BGA) native driver support via `video=bga`.
- **Framebuffer Mode Selection (`vga=`):** Added `vga=WxH@BPP` kernel command line parameter for framebuffer mode selection across all video drivers. Supports legacy CGA/EGA/Hercules/VGA modes, BGA set_mode, multi-framebuffer device registry (`/dev/fb0`..`/dev/fb7`), and GRUB framebuffer inheritance.
- **Framebuffer Rendering Subsystem:** Full rendering pipeline in `sys/drivers/video/`. PSF1/PSF2 font parsers (`psf.c`) with auto-detection and Unicode table extraction. BDF/PCF bitmap font parsers (`bdf_pcf.c`) with hex-to-binary glyph conversion and PCF TOC navigation. Font glyph cache (`font_cache.c`) with FNV-1a hash table (256 buckets) and UTF-8 Unicode mapping from PSF1/PSF2 tables. Blitting operations (`fb_ops.c`): `fb_fillrect()` with ROP_COPY/ROP_XOR, `fb_copyarea()` with overlap-safe memmove, `fb_imageblit()` for mono/color images — all with 32bpp fast paths and generic putpixel fallback, plus viewport clipping. Character rendering attributes (`fb_console.c`): `fb_putc_attr()` supports bold (shift-and-OR double-strike), italic (quarter-height shear transform), underline, strikethrough, and reverse video.

### Input & Storage
- **PS/2 Subsystem:** Expanded PS/2 controller driver to support dual-channel (Mouse/Aux) operation.
- **VirtIO Drivers:** Implemented Core VirtIO, Block Device (virtio-blk), and 9P Transport (virtio-9p) drivers.

## Architecture & Boot
- **FPU State Tracking:** Lazy FPU context switching with FXSAVE/FXRSTOR.
- **Early Boot Debugging:** Added early GDT+IDT handler in `main.c` using `early_uart_print()` for exception debugging before full console is available.
- **LAPIC Early Mapping:** Added LAPIC identity-mapping (0xFEC00000-0xFFFFFFFF) in `boot.S` page tables with PCD flag for MMIO.
- **GRUB Boot Fix:** Fixed multiboot header video mode field offsets (were at 12-24, spec requires 32-44). Kernel now boots through GRUB for the first time.
- **PT_TLS Support:** ELF loader now handles PT_TLS segment, allocates TLS block, sets GDT entry 6 for GS-based TLS access.

## Exec & Personality
- **Shebang Script Execution:** Implemented `#!` (shebang) handler in exec subsystem (`sys/exec/formats/script.c`). Scripts with `#!/path/to/interpreter` are now properly executed by extracting the interpreter and re-dispatching. Supports optional interpreter argument, recursion depth limit (4), and DOS line endings.

## Libraries & Userland
- **libsys Library:** Created `lib/sys/` syscall wrapper library with `syscall.S` (raw i386 int 0x80), `syscall.h` (SYS_* constants), and typed wrappers (`vm86()`). Supports mmap, munmap, mprotect, brk syscalls.
- **Kernel Library Refactor:** Modularized `sys/kern/lib.c` into `sys/lib/string.c`, `printf.c`, and `div64.c`.
- **Kernel sprintf Enhancements:** Added printf flags: `-`, `+`, ` `, `#`, `0`, numeric width, and conversions: d/i/u/o/x/X/p/s/c.

## Build & Testing
- **Build System:** Root filesystem generation in `dist/`. Fixed `dist` directory generation to include standard Unix hierarchy.
- **Test Framework:** Comprehensive kernel test runner (`tests/sys/`). Tests are **not** compiled into the kernel by default; use `make -C sys KERNEL_TESTS=1` for test builds. Host-runnable tests (`host_test_*`) are built separately with `make -C tests/sys`.
