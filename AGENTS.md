# AGENTS.md

## Project Description
This is the Substrate operating system project targeting x86 32-bit architecture (with x86_64 plans). The goal is to build a Unix-like system with a kernel, standard utilities, and libraries, capable of running native, Linux, and FreeBSD binaries via personality emulation.

## Technical Constraints & Standards
- **Architecture:** x86 32-bit (primary), x86 64-bit (planned/stubbed).
- **C ABI:** Standard Intel C ABI.
- **Toolchain:** Modern GCC (`-m32`, `-nostdlib`, `-fno-builtin`).
- **Userland Linker Flags:** `-m32 -nostdlib -fno-pie`.

## Current Capabilities

A terse reference of what exists. Full history: `docs/CHANGELOG.md`.
Design/structure: `ARCHITECTURE.md`. Toolchain detail: `docs/toolchain.md`.
Third-party ports: `docs/contrib-ports.md`.

### Kernel Core
- PMM buddy allocator (Phase 2), UMA-backed kmalloc, per-process pmap with COW.
- MLFQ + SMP scheduler: per-CPU runqueues, work stealing, CPU affinity, IPI preemption.
- Turnstiles (priority inheritance), hashed sleep queues, sleepq-backed mutex/semaphore; BSD `lockmgr()` (shared/exclusive/upgrade/drain) over vnode, namecache, and mount locks.
- Process model: Swapper (PID 0) / Init (PID 1), process groups, sessions; dynamic process registry (no fixed cap).
- TTY signals (SIGINT/SIGQUIT/SIGTSTP), VMIN/VTIME, per-process controlling terminal.
- 64-bit `time_t`; POSIX atime/mtime/ctime across VFS and ext2.
- Syscall tracing with typed arguments and personality detail.
- FreeBSD-compatible thread syscall set (`thr_new`/`thr_exit`/`thr_self`/`thr_kill`/`thr_suspend`/`thr_wake`/`thr_join`/`thr_set_name`/`thr_kill2`) driving libpthread cancellation, naming, and parking.
- Demand-paged user stack (128 KiB mapped at exec, grown one page at a time to an 8 MiB ceiling).
- `memtrack` per-call-site physical-page accounting via `/proc/memtrack` and `sys_vm_slabs(2)`.
- 16 KiB per-process kernel stacks (absorb the deepest nested syscall + IRQ chains).
- Always-on VM kernel-heap corruption tripwires: vm_object magic canary, buddy double-allocation detector, UMA per-item double-free guard, `vm_map_audit`.
- `psignal()` discards ignore-disposition signals instead of leaving them pending (avoids aborting interruptible sleeps).
- System V IPC: semaphores (`ipc_sem.c`) and shared memory (`ipc_shm.c`), wired into the Linux/FreeBSD/NetBSD personalities; `SEM_UNDO` reversed at `proc_exit`.
- POSIX message queues (`posix_mqueue.c`) — a named cross-process kernel object; userspace `mq_*` wrappers in librt.

### Networking
- TCP/IPv4 (`sys/net/tcp.c`): full open + close handshake, retransmit, dup-ACK fast-retransmit, zero-window persist timer, `snd_wnd` send-side flow control, leak-free PCB reaping.
- BSD sockets (`af_inet.c`, `af_unix.c`): socket/bind/listen/accept/connect/send/recv, `shutdown(2)`, `O_NONBLOCK` accept, backlog enforcement; AF_UNIX STREAM/DGRAM with SCM_RIGHTS fd passing.
- Loopback via a dedicated drain kthread (no TX→RX stack recursion); 256 KiB AF_UNIX socket buffers.
- `sbin/telnetd`: thread-per-connection telnet server bridging each socket to a PTY running `/bin/login`.

### Filesystems
- VFS: link/unlink, readdir atime, chmod/chown ctime; readdir/getdents 64-bit `d_off` byte-offset cookies.
- ext2 (read-write, POSIX timestamps); exFAT read-write (`sys/fs/exfat/`: readdir/finddir/read plus write, truncate, mkdir, mknod/`O_CREAT`, unlink, rmdir, rename — maintains the allocation bitmap, FAT chains, directory entry-set `SetChecksum` + `NameHash`, and an up-case table loaded at mount); UDF read-write; FAT; minix; 9P.
- Buffer cache (bio): hash lookup, queueing, delayed write, syncer kthread. Block-level read cache keyed at the block-device layer — transparent to every storage driver and filesystem (`docs/design/block-cache-consolidation.md`).

### Drivers
- Framebuffer: linear FB, BGA, `vga=WxH@BPP` mode selection, multi-device registry.
- Rendering: PSF1/PSF2/BDF/PCF font parsers, glyph cache, fb_fillrect/fb_copyarea/fb_imageblit, bold/italic/underline/strikethrough/reverse attributes.
- VirtIO (block + 9P), PS/2 dual-channel, FPU lazy save.
- PS/2 mouse: IntelliMouse and IntelliMouse Explorer (3/5-button + wheel); PS/2 keyboard: runtime-switchable Set 1 (translated XT) and Set 2 (AT) decoders.
- Audio: Sun-compatible framework over Intel HDA / AC'97 / SB16 / null backends with IRQ-driven DMA-ring refill.
- USB: HID boot-protocol mouse; `lsusb` via `/proc/devtree` + `/dev/usb` nodes.
- Block device registration logs its size as a 64-bit value (a 4 GiB disk no longer logs "0 bytes").
- TTY write path has real write-side flow control (blocks instead of dropping output); PTY delivers a writer's final bufferful across last-close via a per-pair linger buffer.

### Boot & Architecture
- GRUB boot, early boot debugging, LAPIC identity mapping.
- Shebang `#!` script execution, PT_TLS support.
- EFI boot stub with GOP framebuffer.
- ISA discovery: legacy fixed-resource probes + ISA Plug-and-Play isolation, both registered on the ISA bus.

### Libraries & Build
- libsys is the sole owner of the raw `syscall()` dispatcher and the `sys_*` typed wrappers; every binary links it (static via `-l:libsys.a`, dynamic via `-l:libsys.so.0`).
- Every `lib/` library ships both `libX.a` and `libX.so.0` from one source tree (dual `.o` / `.pic.o` passes); shared objects are OSABI-branded (0x40) via a one-byte `dd` post-step.
- libc: GCC toolchain bring-up additions (`putenv`, `localeconv`, `htons`/`htonl`/`ntohs`/`ntohl`, socket/netdb stubs) and errno hygiene (malloc/calloc/realloc, `mmap` wrapper negative-errno handling).
- librt (`-lrt`): POSIX message-queue wrappers + POSIX AIO (userspace worker-thread pool over libpthread).
- libpthread torture suite (portable cross-OS baseline `torture_kernel.c` + substrate-specific surface tests).
- Root filesystem staging (`dist/`); kernel test framework (`tests/sys/`); `build.sh` end-to-end orchestrator.

### Toolchain
Substrate-native GNU toolchain (binutils 2.46.0 + GCC 16.1.0) for the `i386-unknown-substrate` target: stage-1 cross (built on a Linux host) plus stage-2 Canadian cross (runs on substrate). `ELFOSABI_SUBSTRATE = 64` branding routes exec personality dispatch. C++ cross-DSO exceptions are FIXED and verified end-to-end (shared `libgcc_s` + PT_GNU_EH_FRAME unwinding via `dl_iterate_phdr`; a throw in a `.so` is caught in the exe and TagLib reads FLAC metadata) — C++ apps using `std::mutex` must link `-lpthread` (gthr-posix hard refs). `gdb` runs natively atop the libsys `ptrace` bridge. Full detail in `docs/toolchain.md`.

### Userland Tools & Display Manager
- `bin/top`: procps-grade `top(1)` (multi-source snapshot, sortable process table, SIGWINCH resize). `bin/df` sizes its columns dynamically to the data.
- `sbin/sdm`: display manager supervising `Xfbdev` + the `sgreet` Xlib greeter, with a `/etc/sdm/sessions` session chooser (`$SDM_SESSION` execed via `/etc/X11/Xsession`).

### Userland Ports (contrib/)
Third-party userland lives under `contrib/<pkg>/` as patch series against upstream tarballs — never vendored source. Each port ships `fetch.sh` (download + SHA-verify + extract + apply), `build.sh` (configure + make + stage into `dist-overlay/dist-<pkg>/usr/`), a `patches/` series, and a `README.SUBSTRATE.md`. The set spans the system shell (zsh), text/build tools, compression/crypto/net, the man toolchain (mandoc/less), the full X11 client stack + xterm + window managers + bitmap fonts, CDE (a live desktop), e2fsprogs/e2tools, gdb, and the SDL2/PsyMP3 multimedia stack. Full catalog: `docs/contrib-ports.md`.

### Dynamic Linking
- `/sbin/ld.so` (`sbin/ld.so/`): recursive DT_NEEDED loading, DT_GNU_HASH/DT_HASH symbol resolution, i386 REL/JMPREL relocations (RELATIVE / GLOB_DAT / JMP_SLOT / 32 / PC32 / COPY / TLS_TPOFF), DT_INIT_ARRAY + DT_FINI_ARRAY execution, variant-II per-thread TLS via `sys_set_gsbase`, runtime dlopen/dlsym/dlclose, canonical function-address (function-pointer) equality for non-PIE executables, and `dl_iterate_phdr(3)`.
- PIC-safe `lib/c/arch/i386/crt0.S` serves both static and PIE links.
- Specs: `docs/design/ld.so-design.md`, `docs/specs/ld.so-reloc-matrix.md`, `docs/kernel-ldso-abi-substrate.md`, `docs/specs/abi-i386.md`.

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
    - On-target install path is `/usr/share/man/man<section>/`
      (matching contrib ports and mandoc's default tree).
      `usr.man/Makefile` installs to `$(DESTDIR)/usr/share/man/`;
      `build-rootfs.sh install_to_dist()` runs
      `make -C usr.man install` so the pages are staged into
      `dist/` for `--image` to pick up.  MAN_DIRS covers
      man1..man9 (including man8 for sysadmin tools).
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
- `bin/`: User-space utilities (`ls`, `sh`, `vi`, etc.). Test sources live in `tests/bin/<program>/`; each program's `Makefile` references them via `TESTS = ../../tests/bin/<program>`.
- `include/`: Userspace C library headers (shared by all libraries).
- `lib/`:
    - `c/`: LibC implementation **(Substrate target ONLY, never for Linux/host)**.  Builds `libc.a` AND `libc.so.0`.
    - `sys/`: System call wrapper library (libsys). Raw `syscall()` and typed wrappers **(Substrate target ONLY)**.
    - `m/`: Math library — `libm.a` + `libm.so.0`.
    - `edit/`: Command-line editing — `libedit.a` + `libedit.so.0` (DT_NEEDED libc.so.0).
    - `pthreads/`: Threading support — `libpthread.a` + `libpthread.so.0`.
    - `pwdb/`: passwd / group / shadow database helpers — `libpwdb.a`
      + `libpwdb.so.0`.  Public interface in `<sys/pwdb.h>`.  Used
      by the `useradd` / `usermod` / `userdel` / `groupadd` /
      `groupmod` / `groupdel` admin tools under `usr.sbin/` and
      by `bin/groups`.  Provides `pwdb_lock`, `pwdb_unlock`,
      `pwdb_atomic_rewrite` (write to `<file>~` + rename),
      `pwdb_next_free_id`, `pwdb_split`, `pwdb_valid_name`,
      `pwdb_today_days`, `pwdb_require_root`, `pwdb_die`.
    - `dbm/`: Database library (currently broken upstream — missing `SEEK_SET` include in `dbm.c`).
- `sbin/`: System binaries.
    - `ld.so/`: **Substrate native dynamic linker.**  Position-independent
      ET_DYN with no DT_NEEDED.  Loaded by the kernel at AT_BASE =
      0x40000000 for every PIE binary that lists `/sbin/ld.so` in its
      PT_INTERP.  Exec ABI documented in `docs/kernel-ldso-abi-substrate.md`.
- `contrib/`: Third-party components as patch series against
  upstream releases — nothing in `contrib/*/build/` is vendored,
  `fetch.sh` downloads + patches.  Full catalog in `docs/contrib-ports.md`.
    - `binutils/`: GNU binutils 2.46.0 patch series + build script.
      Adds the `elf32-i386-substrate` / `elf64-x86-64-substrate` BFD
      output vecs, the `elf_i386_substrate` ld emulation, the
      ELFOSABI_SUBSTRATE constant in `include/elf/common.h`, and
      readelf's name-resolution for OSABI=64.
    - `gcc/`: GCC 16.1.0 patch series + build script.  Configures
      `i386-unknown-substrate` as a target and a libstdc++-v3 OS
      port at `libstdc++-v3/config/os/substrate/`.
    - `build-toolchain.sh`: orchestrates both at stages 1 and 2.
    - `ext2-boot/`: third-party BIOS ext2 bootloader (submodule).
- `tests/`:
    - `bin/`: Per-program test suites for `bin/` utilities.
    - `lib/pthread/`: portable POSIX `torture_kernel.c` +
      substrate-specific `torture_pthread.c` (scheduler stress + libpthread surface).
    - `lib/m/`: unit + property tests for libm including
      `test_bessel.c` / `prop_bessel.c` and the rest.

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
- /sbin/ld.so remaining work: per-thread TLS for additional
  threads (currently only the initial thread is set up;
  pthread_create needs libpthread to allocate per-thread blocks
  via the same layout); GD/LD TLS models (need __tls_get_addr,
  only matters once dlopen-loaded libs use them); RTLD_NEXT
  semantics (need to know which object the caller was in);
  proper dlclose unmap + reverse-dependency safety; dlerror
  string buffer.
- Switch userland binaries to dynamic linking incrementally — start
  with non-bootstrap-essential ones (`bin/echo` is a good first
  candidate) and verify before expanding.
- Fix `lib/dbm/dbm.c` (missing `SEEK_SET`/`SEEK_CUR` include) so
  libdbm.{a,so.0} build cleanly.
- **Make gas stamp ELFOSABI_SUBSTRATE on output `.o` files.**
  Currently gas emits `ELFOSABI_SYSV` (0); we sidestep with
  `ELF_OSABI_EXACT=0` in the substrate BFD vec.  Real fix is in
  `gas/config/obj-elf.c` (or a new gas/config/te-substrate.h)
  to set the OSABI byte when the target is `i386-unknown-substrate`.
- Record telnet/ssh pts logouts in wtmp.  Console getty logins are
  covered — init writes the `DEAD_PROCESS` record when it reaps a
  getty line — but telnetd/sshd sessions are supervised by those
  daemons and would each need to write their own pts logout.
- Build a shared `libstdc++.so.6` once a real userland is up
  (currently shipping `libstdc++.a` only — `--disable-shared`).
  Requires ld.so to handle C++ symbol versioning, vague linkage
  dedup, and TLS GD/LD models.
- Rebuild stage 2 GCC with `--with-arch=i486` to drop the
  pentium-pro default and produce binaries that run on plain i486
  QEMU CPUs.
