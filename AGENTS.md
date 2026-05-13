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
- Full FreeBSD-compatible thread syscall set: `thr_new` (455),
  `thr_exit` (431), `thr_self` (432), `thr_kill` (433), `thr_suspend`
  (442), `thr_wake` (443), `thr_join` (457), `thr_set_name` (464),
  `thr_kill2` (481).  Per-thread `sig_pending`/`sig_mask` already
  drove signal delivery; the new entry points expose it to libpthread
  for cancellation, naming, and parking.  `thread_t.name[16]` field
  and `THREAD_F_WAKE_PENDING` flag for race-free suspend/wake.

### Filesystems
- VFS: link/unlink, readdir atime, chmod/chown ctime
- ext2: timestamp fixes (write ctime, add_entry/remove_entry parent timestamps)
- UDF: Complete read-write driver with unit tests and man pages
- Buffer cache (bio): hash lookup, queueing, delayed write, syncer kthread

### Drivers
- Framebuffer: linear FB, BGA, `vga=WxH@BPP` mode selection, multi-device registry
- Rendering: PSF1/PSF2/BDF/PCF font parsers, glyph cache, fb_fillrect/fb_copyarea/fb_imageblit, bold/italic/underline/strikethrough/reverse attributes
- VirtIO (block + 9P), PS/2 dual-channel, FPU lazy save
- PS/2 mouse: IntelliMouse (3-button + wheel) and IntelliMouse Explorer
  (5-button + wheel) detection via the Microsoft sample-rate knock
  sequence; 4-byte packet decoding; `BTN_SIDE` / `BTN_EXTRA` /
  `REL_WHEEL` emitted on the input layer
- PS/2 keyboard: runtime-switchable Set 1 (translated XT, default)
  and Set 2 (AT, native) scancode decoders with `0xF0` break-prefix
  state machine and an E0-prefix path that handles both encodings
- Intel HDA, AC'97, SB16, null audio backends (Sun-compat audio
  framework)
- USB HID boot-protocol mouse

### Boot & Architecture
- GRUB boot fix, early boot debugging, LAPIC identity mapping
- Shebang `#!` script execution, PT_TLS support
- EFI boot stub with GOP framebuffer

### Libraries & Build
- libsys syscall wrapper library, kernel library modularization
- Root filesystem staging (`dist/`), test framework (`tests/sys/`)
- Every library under `lib/` ships both static (`libX.a`) and shared
  (`libX.so.0`) builds from a single source tree via dual `.o` /
  `.pic.o` compile passes; `SHLIB_CFLAGS` / `SHLIB_LDFLAGS` defined
  in `Makefile.inc`.  The `libX.so` link-time symlink is install-only
  so it can't shadow `libX.a` in source-tree builds.
- `lib/c`, `lib/m`, `lib/sys` Makefiles auto-patch the OSABI byte of
  every produced `libX.so.0` to `ELFOSABI_SUBSTRATE` (0x40) via a
  one-byte `dd` post-step.  Required because host `cc -shared` stamps
  `ELFOSABI_SYSV` (0), which substrate's cross-ld rejects as "file
  in wrong format".  Build infrastructure picks up additions without
  ceremony.
- libc additions for the GCC toolchain bring-up: `putenv`,
  `localeconv` (POSIX "C" lconv), `htons` / `htonl` / `ntohs` /
  `ntohl` (real `__builtin_bswap` impls), socket / netdb / arpa-inet
  ENOSYS stub family for link-time satisfaction pending a real
  in-kernel sockets layer.
- libpthread torture suite (`tests/lib/pthread/`): portable POSIX
  `torture_kernel.c` runs on Linux/FreeBSD/macOS/substrate alike as a
  cross-OS baseline; 8 scenarios target specific scheduler/threading
  bug classes (storm, fpu, wakeup, signals, mutex_fair, tls,
  massive, lockord).  Plus a substrate-specific `torture_pthread.c`
  for libpthread surface correctness.

### Toolchain (Substrate-Native GNU Toolchain)
- **Stage 1 (cross)**: binutils 2.46.0 + GCC 16.1.0 patched for
  the substrate target, built on a Linux host.  Installs into
  `/opt/substrate` (`STAGE1_PREFIX`) as
  `i386-unknown-substrate-{gcc,g++,as,ld,ar,nm,objdump,...}`.  Used
  to cross-compile substrate userland.
- **Stage 2 (Canadian cross)**: same binutils + GCC sources, but
  built with the stage-1 cross compiler to produce substrate-ELF
  binaries that run *on* substrate itself.  Installs into
  `dist-toolchain/usr/` (binutils) and `/tmp/gcc-stage2-staging/usr/`
  (gcc) as `/usr/bin/{gcc,g++,ld,as,ar,nm,objdump,readelf,strip,
  ranlib,size,strings,addr2line,c++filt,elfedit,gprof,ld.bfd}`,
  `/usr/libexec/gcc/i386-unknown-substrate/16.1.0/{cc1,cc1plus,lto1,
  lto-dump,collect2,lto-wrapper}`, and
  `/usr/lib/gcc/i386-unknown-substrate/16.1.0/{libgcc.a,libgcov.a,
  crtbegin*.o,crtend*.o}`.
- **Bootstrap orchestrator**: `contrib/build-toolchain.sh` drives
  all four phases (binutils stage 1, gcc stage 1, binutils stage 2,
  gcc stage 2) with idempotent fetch + patch + build.  Per-component
  scripts at `contrib/binutils/build.sh` and `contrib/gcc/build.sh`
  can also be run individually.
- **ELFOSABI_SUBSTRATE = 64**: architecture-specific OSABI byte
  stamped by every substrate-target BFD output vec
  (`elf32-i386-substrate`, `elf64-x86-64-substrate`).  The vec has
  `ELF_OSABI_EXACT = 0` so it accepts SysV (OSABI=0) input objects
  during bootstrap, but every executable/DSO it produces carries
  OSABI=64.  This is the wire-level identifier the kernel exec
  personality dispatch uses to route a binary to its loader.
- Patch series for both binutils and gcc lives in
  `contrib/{binutils,gcc}/patches/` and is reapplied by `fetch.sh`.
  Nothing in `contrib/*/build/` is vendored.
- Image bootstrap chain (committed end-to-end):
  ```
  contrib/build-toolchain.sh        # stage 1 cross + stage 2 native
  ./build-rootfs.sh --dist          # substrate userland into dist/
  ./build-rootfs.sh --toolchain     # overlay stage-2 toolchain
  ./build-rootfs.sh --image         # bake 4 GiB rootfs.img
  ```

### Dynamic Linking
- `/sbin/ld.so` (Substrate native dynamic linker, `sbin/ld.so/`):
  - Phase 1: bootstrap, asm `_start`, auxv walk, AT_ENTRY handoff.
  - Phase 2: parse program PT_DYNAMIC, summarize DT_*, self-relocate
    (R_386_RELATIVE).
  - Phase 3: load DT_NEEDED libs via `mmap` + MAP_FIXED, build symbol
    scope, apply REL/JMPREL relocations (R_386_RELATIVE / GLOB_DAT /
    JMP_SLOT / 32 / PC32) with eager binding.  DT_GNU_HASH preferred,
    DT_HASH fallback.  Search paths `/lib`, `/usr/lib`, `/usr/local/lib`.
  - Phase 4a: recursive (BFS) DT_NEEDED traversal — transitive deps
    pulled in automatically.
  - Phase 4b: DT_INIT and DT_INIT_ARRAY execution in dependency
    order (deepest deps first, program last).
  - Phase 4c: per-thread TLS — variant-II layout (TCB at TP, data
    at negative offsets), PT_TLS images copied into mmap'd block,
    GS base installed via the new native `sys_set_gsbase` syscall
    (SYS_SET_GSBASE = 274).  Local-exec / initial-exec models work;
    GD/LD models deferred until libpthread needs them.
  - Phase 4d: R_386_COPY support — `ld_resolve_skip` finds the
    source-of-truth in a SHARED library (not the executable
    receiving the copy); relocator memcpy's bytes sized via
    st_size.  PIE binaries don't emit COPY (they prefer GLOB_DAT
    to keep .text relocation-free) but the path is in place for
    non-PIE callers.
  - Phase 4e: runtime dlopen / dlsym / dlclose.  ld.so exports
    `__ldso_dlopen` / `__ldso_dlsym` / `__ldso_dlclose` with
    default visibility and registers itself as an ld_obj_t at
    the tail of the loaded-object scope so libdl's weak refs
    resolve.  libdl (`lib/dl/`) provides POSIX wrappers; static-
    linked binaries that pull libdl in without ld.so see NULL
    stubs that return 0 / -1 instead of crashing.
  - Phase 4f: DT_FINI_ARRAY at exit() time.  ld.so exports
    `__ldso_run_fini` (mirror of init_arrays in REVERSE init
    order with each object's array walked backwards); libc's
    `exit()` calls it via a weak ref before _exit syscall.
  - Per-object guards: `relocated` / `initialized` / `finalized`
    booleans on ld_obj_t prevent the non-idempotent
    R_386_RELATIVE (`*p += base`) and the run-once init/fini
    arrays from firing twice when dlopen re-walks the loaded list.
- `lib/c/arch/i386/crt0.S` is now PIC-safe (`%ebx`/GOT setup, `@PLT`
  for calls, `environ@GOT` for data) so the same `crt0.o` works for
  both static and PIE binaries.
- Specs: `docs/design/ld.so-design.md`, `docs/specs/ld.so-reloc-matrix.md`,
  `docs/kernel-ldso-abi-substrate.md`, `docs/specs/abi-i386.md`.


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
- `bin/`: User-space utilities (`ls`, `sh`, `vi`, etc.). Test sources live in `tests/bin/<program>/`; each program's `Makefile` references them via `TESTS = ../../tests/bin/<program>`.
- `include/`: Userspace C library headers (shared by all libraries).
- `lib/`:
    - `c/`: LibC implementation **(Substrate target ONLY, never for Linux/host)**.  Builds `libc.a` AND `libc.so.0`.
    - `sys/`: System call wrapper library (libsys). Raw `syscall()` and typed wrappers **(Substrate target ONLY)**.
    - `m/`: Math library — `libm.a` + `libm.so.0`.
    - `edit/`: Command-line editing — `libedit.a` + `libedit.so.0` (DT_NEEDED libc.so.0).
    - `pthreads/`: Threading support — `libpthread.a` + `libpthread.so.0`.
    - `dbm/`: Database library (currently broken upstream — missing `SEEK_SET` include in `dbm.c`).
- `sbin/`: System binaries.
    - `ld.so/`: **Substrate native dynamic linker.**  Position-independent
      ET_DYN with no DT_NEEDED.  Loaded by the kernel at AT_BASE =
      0x40000000 for every PIE binary that lists `/sbin/ld.so` in its
      PT_INTERP.  Exec ABI documented in `docs/kernel-ldso-abi-substrate.md`.
- `contrib/`: Third-party components as patch series against
  upstream releases — nothing in `contrib/*/build/` is vendored,
  `fetch.sh` downloads + patches.
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
- Replace the static `processes[16]` (`MAX_PROCS`) table with a
  pid-hash + all-procs list.  26 callers across 10 files.  My new
  `sys_thr_kill2` already routes through `proc_find()` so it'll
  come along for free.
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
- **In-kernel sockets layer for AF_UNIX.**  GCC's sarif-sink
  references socket/connect at link time; we provide ENOSYS stubs.
  syslog, X11 local connections, D-Bus, and a long tail of IPC
  users all need real UDS.  Scope: ~1500-2500 LOC kernel (generic
  socket dispatch + af_unix SOCK_STREAM/DGRAM + buffer queues +
  scm_rights for fd passing) + replacing the libc stubs with real
  syscall wrappers.
- libpthread surface expansion to match the suite:
  `pthread_cond_*`, `pthread_kill` (wraps SYS_THR_KILL), `pthread_sigmask`,
  `pthread_setname_np` (wraps SYS_THR_SET_NAME), `pthread_self`.
- Build a shared `libstdc++.so.6` once a real userland is up
  (currently shipping `libstdc++.a` only — `--disable-shared`).
  Requires ld.so to handle C++ symbol versioning, vague linkage
  dedup, and TLS GD/LD models.
- Rebuild stage 2 GCC with `--with-arch=i486` to drop the
  pentium-pro default and produce binaries that run on plain i486
  QEMU CPUs.
