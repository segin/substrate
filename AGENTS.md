# AGENTS.md

## Project Description
This is the Substrate operating system project targeting x86 32-bit architecture (with x86_64 plans). The goal is to build a Unix-like system with a kernel, standard utilities, and libraries, capable of running native, Linux, and FreeBSD binaries via personality emulation.

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
- **Syscall Tracing:** Enhanced `syscall_trace` with names, typed arguments (int/hex/ptr/str), return values, and Personality details.
- **Init Safety:** Kernel now catches `init` process exit (e.g., from detached stdin) and enters an idle loop instead of panicking.
- **Build System:** Fixed `dist` directory generation to include standard Unix hierarchy (`usr/include`, `usr/local`, etc.) and ensured `vmunix` installation.
- **Boot/Init:** Cleaned up `sbin` build process; kernel now boots correctly with `root=/dev/hda` for external root filesystems, attempting fallback to `init` search path.
- **UDF Filesystem Driver:** Complete read-write UDF (Universal Disk Format) driver per ECMA-167/OSTA spec. On-disk structures in `udf.h`, read-only support in `udf.c`, write support in `udf_write.c`, with unit tests and man pages (`udf.4`, `udf.5`).
- **UMA Integration:** Integrated FreeBSD-style Universal Memory Allocator (UMA) for kernel memory allocation. `kmalloc`/`kfree` now backed by UMA zones via `vm_kmem.c`. Added `uma_startup()` before `kmem_init()`.
- **Early Boot Debugging:** Added early GDT+IDT handler in `main.c` using `early_uart_print()` for exception debugging before full console is available. Prints exception number and halts on any early fault.
- **LAPIC Early Mapping:** Added LAPIC identity-mapping (0xFEC00000-0xFFFFFFFF) in `boot.S` page tables with PCD flag for MMIO. Entry 1019 (offset 4076) covers LAPIC at 0xFEE00000.
- **PMM Virtual Address API:** `pmm_alloc_block()` and `pmm_alloc_contiguous()` now return kernel virtual addresses (phys + 0xC0000000) instead of physical addresses. `pmm_free_block()` and `pmm_free_contiguous()` expect virtual addresses. Updated all callers in: `pmap.c`, `elf.c`, `sched.c`, `process.c`.
- **Scheduler Refactor (MLFQ):** Implemented Multilevel Feedback Queue with Realtime, Timeshare, and Idle classes.
- **SMP Scheduler:** Per-CPU runqueues, Work Stealing load balancing, CPU Affinity support, and IPI-based preemption.
- **Synchronization Primitives:** Implemented Turnstiles (Priority Inheritance) and Hashed Sleep Queues (O(1) lookup).
- **Context Switching:** Validated FPU Lazy Save and refined PCB for thread/process separation.
- **Kernel Process:** Implemented Swapper/Idle (PID 0) with pageout daemon and idle loop responsibilities.
- **libsys Library:** Created `lib/sys/` syscall wrapper library with `syscall.S` (raw i386 int 0x80), `syscall.h` (SYS_* constants), and typed wrappers (`vm86()`). Supports mmap, munmap, mprotect, brk syscalls.
- **Kernel Library Refactor:** Modularized `sys/kern/lib.c` into `sys/lib/string.c`, `printf.c`, and `div64.c`.
- **Synchronization Improvements:** Updated `mutex` and `semaphore` to use `sleepq` for robust thread sleeping (removed ad-hoc `sched_sleep`).
- **SMP Scheduler Fixes:** Updated `sched_smp.c` to use `percpu_get_cpu_id()` instead of assumption.
- **Code Quality:** Defined `kernel_process` explicitly for kthreads (fixing PID assumption), improved `panic()` messaging, and cleaned up `random.c` duplicates.


## Current Status
- **PMM Refactor:** Phase 2 complete.
    - O(log N) Buddy Allocator with proper page coalescing (Orders 0-10).
    - Contiguous allocation optimized via Buddy system.
    - Bootstrap Watermark Allocator for early boot.
    - Dynamic Metadata sizing (no static limit).
    - Low Memory safeguards active.
    - **API:** `pmm_alloc_block()` returns kernel virtual address (0xC0000000+), NOT physical.
    
### PMM API Usage (CRITICAL)
**`pmm_alloc_block()` returns VIRTUAL address (kernel direct-mapped):**
```c
void *page_virt = pmm_alloc_block();  // Returns 0xC0000000+

// For memory access: use directly
memset(page_virt, 0, 4096);           // CORRECT
*((uint32_t*)page_virt) = 0xDEADBEEF; // CORRECT

// For pmap_enter/hardware: convert to physical
uint32_t page_phys = (uint32_t)page_virt - 0xC0000000;  // REQUIRED
pmap_enter(pmap, user_va, page_phys, flags);

// WRONG patterns:
memset((void*)((uint32_t)page + 0xC0000000), ...);  // Double offset!
pmap_enter(pmap, va, (uint32_t)page_virt, ...);     // Virtual to pmap!
```

**`pmm_free_block()` expects VIRTUAL address:**
```c
pmm_free_block(page_virt);  // CORRECT - pass virtual

// If you have physical (e.g., from pmap_extract):
uint32_t phys = pmap_extract(pmap, va);
void *virt = (void*)(phys + 0xC0000000);
pmm_free_block(virt);  // CORRECT - convert first
```
- **User process foundation complete (pmap layer):**
    - Recursive paging and global page support.
    - `pmap_protect` and `pmap_copy` with Copy-on-Write (COW).
    - `pmap_kenter`/`pmap_kremove` kernel fast paths.
    - Page reference/modification tracking (`pmap_is_referenced`, `pmap_is_modified_range`).
    - Identity-mapping for Local APIC (0xFEE00000) during bootstrap.
- **Process Model Refactored:**
    - Swapper: PID 0 (TID 0).
    - Init: PID 1 (TID 1).
    - Init spawned via `sched_spawn_kernel_process` and transitions via `execve`.
- VirtIO drivers (Block, 9P) linked and initialized
- **PT_TLS Support:** ELF loader now handles PT_TLS segment, allocates TLS block, sets GDT entry 6 for GS-based TLS access
- **VGA Hardware Cursor:** Fixed to sync with software cursor position
- Debugging remaining TLS access issue (ESI pointing to PT_TLS template instead of allocated block)

## Substrate System Patterns (CRITICAL)
- **Device Naming:** Storage devices reside in `/dev/storage/` and use a `type`+`instance` pattern:
    - `ide0`, `ide1`, ... (IDE)
    - `sata0`, `sata1`, ... (SATA)
    - `scs0`, `scsi0`, ... (SCSI)
- **Mounting:**
    - The `mount` utility usage is: `mount <device> <mount_point> <filesystem_type>`.
    - `fstab` entries follow the same logic as the `mount` command.
    - The kernel automatically mounts `/proc`, `/sys`, and `/dev` after the root filesystem is established.
- **Initialization:**
    - `sbin/init` (or `etc/init.sh` copied to `/sbin/init`) should focus on starting system services and getty, as basic pseudo-filesystems are kernel-managed.

## Directives
1.  **Architecture Maintenance:** Always read `ARCHITECTURE.md` before starting complex tasks. Update `ARCHITECTURE.md` if your changes impact the system structure or design.
2.  **Code Style:** Adhere to standard kernel coding styles (similar to BSD/Linux) for C and C++.
3.  **Documentation:** Keep documentation close to the code.
4.  **Safety:** Always verify file contents before replacing.
5.  **Build System:** Maintain the recursive Makefile structure. Ensure `make -C sys`, `make -C lib/c`, and `make -C bin` always pass.
6.  **Git Operations:** Use `git mv` and `git rm` for file operations to preserve history.
7.  **TASKS.md Work Methodology:** Complete ONE checkbox at a time, update docs/specs/database/unit/property/fuzzing tests as applicable, commit, push. This applies to ALL checkboxes in `TASKS.md`, not just PMAP work.
8.  **Memory Management:** Always prefer `AGENTS.md` over `GEMINI.md` if both are present. Ensure `GEMINI.md` is not merely a symbolic link to `AGENTS.md` before treating it as separate.
9.  **Documentation Standards:** All new kernel subsystems, system calls (native personality only), and system library calls (libc, libdl, libm, libg, libpthread) MUST include corresponding Man Page documentation. Follow Linux `man-pages` style:
    - **LIBRARY:** Required for user-mode calls.
    - **SEE ALSO:** Required section.
    - **ERRORS:** Required for APIs returning error codes via `errno`. Document separately from RETURN VALUE.
    - **EXAMPLE/EXAMPLES:** Use "EXAMPLE" for single, "EXAMPLES" for multiple.
10. **Host Builds vs Target Builds (CRITICAL):**
    - **Target Build:** Compiles code for the Substrate kernel and userland. Uses `-nostdlib` and the project's own `lib/c/`, `lib/sys/`, etc.
    - **Host Build (`NATIVE_BUILD=1`):** Compiles code to run on the host OS (Linux, BSD, etc.) for testing purposes. Uses the **HOST OS's libc and system libraries**, NOT Substrate's.
    - **NEVER** modify `lib/c/`, `lib/sys/`, `crt0.S`, `syscall.S`, or other core libraries to support Linux or any host OS. These are for Substrate only.
    - Host builds link against the host's standard C library (e.g., glibc on Linux) via normal `cc` invocation without `-nostdlib`.
    - The `bin/sh/Makefile` and similar use `NATIVE_BUILD=1` to toggle between target and host compilation modes.

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
- `include/`: Userspace C library headers (shared by all libraries).
- `lib/`:
    - `c/`: LibC implementation **(Substrate target ONLY, never for Linux/host)**.
    - `sys/`: System call wrapper library (libsys). Raw `syscall()` and typed wrappers **(Substrate target ONLY)**.
    - `pthreads/`: Threading support.
    - `dbm/`: Database library.
- `sbin/`: System binaries.

### Kernel Debugging
When debugging kernel crashes (including triple faults):

1. **QEMU with GDB:**
   ```bash
   # Terminal 1: Start QEMU with debugging, -no-reboot stops on triple fault
   qemu-system-i386 -kernel sys/kernel.bin -no-reboot -s -S
   
   # Terminal 2: Connect GDB
   gdb -ex "file sys/kernel.bin" -ex "target remote :1234"
   ```

2. **Single-Step Debugging:**
   - Use `si` (step instruction) one at a time in gdb
   - Use `break <function>` to set breakpoints
   - Use `info registers` to check CPU state
   - When QEMU hits triple fault with `-no-reboot`, it halts and gdb shows connection closed

3. **Exception Trapping:**
   - Set breakpoints on IDT handlers: `break isr_common_stub`, `break double_fault_handler`
   - Use QEMU monitor (`Ctrl+Alt+2`) for low-level CPU inspection

4. **Debugging Principles:**
   - **Never recreate code** - always restore from git history when reverting changes
   - **Single-step from crash point** - triple faults don't return to gdb, so step one instruction at a time
   - **Check BSS/stack** - large static arrays can cause stack overflow or memory corruption

### Debugging Note
If the kernel hangs in `hlt`, check `eflags` bit 9. If `IF=1`, the IRQ may be masked at the PIC or the controller state is stuck.

## Known Issues
- Interrupt responsiveness: `Ctrl+F9` debug dump sometimes fails during idle states.
- **PMAP Memory Overhead:** 32 identity-mapped kernel PDEs (0-31) in userspace consume 128KB+ per process.

## Next Steps
- Debug remaining console OOM issue (kmalloc returning NULL during std fd init)
- Refactor PMAP to dynamically allocate page tables (reduce 128KB overhead)
- Implement mmap() syscall with personality driver integration
- Flesh out 9P filesystem logic implementation
