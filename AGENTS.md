# AGENTS.md

## Project Description
This is the Substrate operating system project targeting x86 32-bit architecture (with x86_64 plans). The goal is to build a Unix-like system with a kernel, standard utilities, and libraries, capable of running native, Linux, and FreeBSD binaries via personality emulation.

## Technical Constraints & Standards
- **Architecture:** x86 32-bit (primary), x86 64-bit (planned/stubbed).
- **C ABI:** Standard Intel C ABI.
- **Toolchain:** Modern GCC (`-m32`, `-nostdlib`, `-fno-builtin`).
- **Userland Linker Flags:** `-m32 -nostdlib -fno-pie`.

## Recent Accomplishments

For the full detailed changelog, see `docs/CHANGELOG.md`.

### Kernel Core
- PMM Buddy Allocator (Phase 2), UMA-backed kmalloc, per-process pmap with COW
- MLFQ + SMP scheduler with per-CPU runqueues, work stealing, and CPU affinity
- Turnstiles (priority inheritance), hashed sleep queues, sleepq-backed mutex/semaphore
- BSD-style `lockmgr()` with shared/exclusive/upgrade/drain modes; vnode, namecache, and mount locking
- Process model: Swapper (PID 0) / Init (PID 1), process groups, sessions
- TTY signals (SIGINT/SIGQUIT/SIGTSTP), VMIN/VTIME support, per-process ctty
- 64-bit time_t, POSIX timestamp compliance (atime/mtime/ctime across VFS and ext2)
- Syscall tracing with typed arguments and personality detail

### Filesystems
- VFS: link/unlink, readdir atime, chmod/chown ctime
- ext2: timestamp fixes (write ctime, add_entry/remove_entry parent timestamps)
- UDF: Complete read-write driver with unit tests and man pages
- Buffer cache (bio): hash lookup, queueing, delayed write, syncer kthread

### Drivers
- Framebuffer: linear FB, BGA, `vga=WxH@BPP` mode selection, multi-device registry
- Rendering: PSF1/PSF2/BDF/PCF font parsers, glyph cache, fb_fillrect/fb_copyarea/fb_imageblit, bold/italic/underline/strikethrough/reverse attributes
- VirtIO (block + 9P), PS/2 dual-channel, FPU lazy save

### Boot & Architecture
- GRUB boot fix, early boot debugging, LAPIC identity mapping
- Shebang `#!` script execution, PT_TLS support
- EFI boot stub with GOP framebuffer

### Libraries & Build
- libsys syscall wrapper library, kernel library modularization
- Root filesystem staging (`dist/`), test framework (`tests/sys/`)


## Current Status

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
1.  **Architecture Maintenance:** **CRITICAL:** Always read `ARCHITECTURE.md` before starting complex tasks. Update `ARCHITECTURE.md` if your changes impact the system structure or design.
    - **Format:** Maintain the structure and format defined in `ARCHITECTURE.md` (based on the template at `https://architecture.md/`).
    - **Frequency:** Update `ARCHITECTURE.md` whenever major components are added, refactored, or when directory structures change.
2.  **Code Style:** Adhere to standard kernel coding styles (similar to BSD/Linux) for C and C++.
3.  **Documentation:** Keep documentation close to the code.
4.  **Safety:** Always verify file contents before replacing.
5.  **Build System:** Maintain the recursive Makefile structure. Ensure `make -C sys`, `make -C lib/c`, and `make -C bin` always pass.
6.  **Git Operations:** Use `git mv` and `git rm` for file operations to preserve history.
7.  **TASKS.md Work Methodology:** Complete ONE checkbox at a time, update docs/specs/database/unit/property/fuzzing tests as applicable, commit, push. This applies to ALL checkboxes in `TASKS.md`, not just PMAP work.
8.  **Memory Management:** Always prefer `AGENTS.md` over `GEMINI.md` if both are present. Ensure `GEMINI.md` is not merely a symbolic link to `AGENTS.md` before treating it as separate.
9.  **Documentation Standards:** All new kernel subsystems, system calls (native personality only), and system library calls (libc, libdl, libm, libg, libpthread) MUST include corresponding Man Page documentation. Follow Linux `man-pages` style:
    - Store manual pages under `usr.man/man<section>/`, not `docs/man/`.
    - **LIBRARY:** Required for user-mode calls.
    - **SEE ALSO:** Required section.
    - **ERRORS:** Required for APIs returning error codes via `errno`. Document separately from RETURN VALUE.
    - **EXAMPLE/EXAMPLES:** Use "EXAMPLE" for single, "EXAMPLES" for multiple.
10. **Host Builds vs Target Builds (CRITICAL):**
11. **Header & Macro Standards (CRITICAL):**
12. **Autonomous Execution (CRITICAL):**
    - **Use `#askuser`:** When communicating with the user for clarification, confirmation, or feedback, always use `#askuser` rather than plain freeform prompting.
    - Default to autonomous mode: do not stop for routine confirmations.
    - Make best-effort decisions and continue immediately unless an operation is destructive (`reset`, `checkout --`, force-push) or truly blocked.
    - In dirty worktrees, commit only files related to the active task and ignore unrelated modifications.
    - Do not pause to report routine repo-state warnings; continue and summarize decisions in commit messages.
    - **No Manual Externs:** Never manually `extern` functions or variables in C files (especially syscalls). Always include the appropriate header.
    - **No Relative Includes:** Avoid relative include paths (e.g., `"../include/foo.h"`). Use include paths set in the Makefile and `<foo.h>`.
    - **Macros in Headers:** Do not define constants or macros in C files if they are arguably part of an interface or shared. Put them in headers.
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
- Refactor PMAP to dynamically allocate page tables (reduce 128KB overhead)
- Implement mmap() syscall with personality driver integration
- Flesh out 9P filesystem logic implementation
