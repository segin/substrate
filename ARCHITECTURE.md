# Architecture

This document defines Substrate as a full Unix operating system architecture.
It is a living technical baseline for kernel, userland, libraries, and toolchain.

## 1. System Identity

Substrate is a complete Unix OS project with five first-class pillars:
- Kernel (`sys/`)
- Userland programs (`bin/`, `sbin/`, `usr.bin/`)
- Runtime/system libraries (`lib/`, `usr.lib/`) — shipped as both static
  archives (`libX.a`) and shared objects (`libX.so.0`).
- Native dynamic linker (`sbin/ld.so`) — loaded by the kernel at the
  PT_INTERP base for every dynamically-linked native binary.  It:
  - performs recursive DT_NEEDED traversal, symbol resolution
    (DT_GNU_HASH + DT_HASH), and i386 REL/JMPREL relocations
    (RELATIVE / GLOB_DAT / JMP_SLOT / 32 / PC32 / COPY / TLS_TPOFF);
  - runs DT_INIT_ARRAY + DT_FINI_ARRAY and installs per-thread TLS
    (PT_TLS images copied into a variant-II layout, GS base set via the
    native `sys_set_gsbase` syscall);
  - offers runtime `dlopen` / `dlsym` / `dlclose` for plugin-style
    loading;
  - honours the **canonical function address** rule for non-PIE
    executables: when the program takes the address of a
    shared-library function the static linker emits that symbol
    UND-with-`st_value` (its own PLT stub), and ld.so hands that PLT
    address to every *other* module so `&func` is identical everywhere
    (function-pointer equality — required by Xt's `XtInherit*`
    class-method machinery, which CDE's front panel depends on).  The
    program's own PLT slots still bind to the real defining DSO;
  - registers itself in the loaded-object scope so its `__ldso_*`
    exports are visible to libdl and to libc (which calls
    `__ldso_run_fini` as a weak hook from `exit()` to run destructors
    before the process is reaped);
  - records each loaded object's runtime program-header table and
    exports `dl_iterate_phdr(3)` (via `__ldso_dl_iterate_phdr`, bridged
    by libc and `include/link.h`).  This is the runtime support for
    shared-`libgcc_s` C++ exception unwinding across the exe/DSO
    boundary (PT_GNU_EH_FRAME-based unwinding), which is done and
    verified end-to-end — see §6.
- Toolchain via the GNU stage-2 port (binutils + GCC under `contrib/`),
  installed on the image as the system `cc` / `as` / `ld` / `g++`.

Primary target architecture is i386. x86_64 support is active and expanding.

## 2. Design Principles

- **System-First Design:** Focus on the integrated behavior of the entire OS.
- **ABI Correctness:** Prioritize stable, correct interfaces before implementation convenience.
- **Native Autonomy:** Aim to own the entire stack, including the C toolchain.
- **Testable Contracts:** Ensure kernel/userspace/toolchain boundaries are explicit and verified.

## 3. Top-Level Architecture

```text
User Programs (static or PIE)
  -> /sbin/ld.so (PT_INTERP for PIE only)
       -> mmap loaded shared objects (lib*.so.0)
       -> resolve relocations across the loaded-object scope
  -> libc/libm/libedit/libpthread/libsys/... (userspace ABI)
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
sys/boot/    Substrate BIOS bootloader (stage1 asm + stage2 C)
bin/         base Unix userland
sbin/        system utilities
sbin/ld.so/  Substrate native dynamic linker.  Phases 1-4g live:
             bootstrap + auxv handoff (1), self-relocate + parse
             program PT_DYNAMIC (2), recursive DT_NEEDED loading +
             REL/JMPREL relocations (3, 4a), DT_INIT_ARRAY
             execution (4b), variant-II TLS install via GS segment
             (4c), R_386_COPY relocations (4d), runtime
             dlopen/dlsym/dlclose API (4e), DT_FINI_ARRAY at
             exit() time via libc weak hook (4f), canonical
             function-address equality for non-PIE exes (4g), plus
             dl_iterate_phdr(3).  Per-object
             relocated/initialized/finalized guards prevent the
             non-idempotent R_386_RELATIVE and the run-once
             init/fini arrays from firing twice when dlopen
             re-walks the loaded list.  Design in
             docs/design/ld.so-design.md, reloc matrix in
             docs/specs/ld.so-reloc-matrix.md, kernel ABI contract
             in docs/kernel-ldso-abi-substrate.md.
usr.bin/     extended user tools (ar, nm, readelf, ldd, yacc, lex, ...).
             The C toolchain (cc/as/ld/g++/cpp) comes from the GNU
             stage-2 port under contrib/{binutils,gcc}/.
lib/         target runtime libraries (libc/libsys/libm/libpthread/libedit/libusb...)
usr.lib/     shared libraries for tooling/runtime support (elfobj, demangle, ...)
include/     userspace public headers
tests/       unit/integration/regression/property/fuzz harnesses
docs/specs/  detailed subsystem specs
docs/tasks/  refactored task planning sections
dist/        target root filesystem staging
host_dist/   host install staging for native validation tools
linux/       Linux-host compatibility tools (binfmt_misc runner, host bridges)
usr.man/     manual page source tree
contrib/     third-party components, each as a patch series + fetch.sh
             + build.sh against an upstream tarball.  The set spans the
             toolchain (binutils, gcc), the shell (zsh), text/build
             tools, compression/crypto/net, the man toolchain
             (mandoc, less), the full X11 client stack + xterm +
             window managers + bitmap fonts, CDE, e2fsprogs/e2tools,
             gdb, and the SDL2/PsyMP3 multimedia stack.  Full catalog:
             docs/contrib-ports.md.  Each port lands at
             ${SUBSTRATE_TOP}/dist-overlay/dist-<pkg>/ which
             build-rootfs.sh overlays onto the image.
tools/       build and install helper scripts
build.sh     repo-root end-to-end build (toolchain + native +
             contrib + image).  See build.sh header for env knobs.
```

Detailed staging rules for `dist/` are defined in `docs/specs/rootfs.md`.

## 5. Kernel Architecture

The kernel remains monolithic and is organized into logical layers:
- `sys/arch/`: Architecture-specific implementation (CPU/MMU/Bootstrap).
- `sys/core/`: Early initialization and global startup.
- `sys/kern/`: Core services (Scheduler, Signals, Time, Sync, memtrack). Inter-process communication lives here too:
  - System V semaphores (`ipc_sem.c`) and shared memory (`ipc_shm.c`).
  - POSIX message queues (`posix_mqueue.c`) — a named, cross-process kernel object: a fixed table of queues (priority-ordered message lists, per-queue `ipc_perm`, single-registration `SIGEV_SIGNAL` notify) fronted by a per-open descriptor table (`mqd_t`), with interruptible, absolute-timeout-aware blocking on a sleep queue. Its userspace surface (`mq_*`) is in `lib/rt`.
- `sys/pm/`: Process management and lifecycle.
- `sys/vm/`: Memory management (PMM, PMAP, VM objects, demand-paged user stacks).
  - Always-on kernel-heap corruption tripwires — a `vm_object` magic canary, a buddy-allocator double-allocation detector, a UMA per-item double-free guard, and a `vm_map` entry/object auditor — turn silent corruption into an immediate located panic.
  - Per-process kernel stacks are 16 KiB to absorb the deepest nested syscall + IRQ call chains (notably the network TX path interrupted by a NIC RX IRQ).
- `sys/vfs/` and `sys/fs/`: Virtual Filesystem and concrete implementations.
  - Concrete filesystems: ext2 (read-write), exFAT (`sys/fs/exfat/`, read-write: readdir/read plus write, truncate, mkdir, mknod/`O_CREAT`, unlink, rmdir, rename — maintaining the allocation bitmap, FAT chains, entry-set `SetChecksum` + `NameHash`, and an up-case table; hardened per a full Rev 1.00 spec audit — boot/set/table checksums and geometry verified on read, all mutations under a per-mount lock, new clusters zeroed, deferred unlink-while-open, and cycle-safe rename; substrate-written volumes pass host `fsck.exfat`), UDF (read-write), FAT, minix, and 9P.
  - The buffer cache (`sys/vfs/bio.c`) is keyed at the block-device layer — `blkdev_do_read`/`do_write` go through `bio_dev_get`/`release` keyed by `(struct blkdev *, sector)` with read coalescing and write-through — so caching is transparent to every storage driver and filesystem; the filesystems carry no caching logic of their own. See `docs/design/block-cache-consolidation.md`.
- `sys/drivers/`: Device driver framework and hardware drivers.
  - The audio stack (`sys/drivers/audio/`) is a Sun-compatible framework over AC'97 / Intel HDA / SB16 / null backends; each backend decouples the `write()` producer from the DMA ring via a deep software PCM FIFO refilled from the per-slot completion IRQ, and the AC'97 backend restarts a halted bus master from both the producer and the IRQ handler (`ac97_kick_locked`).  The HDA backend's stream engine is an explicit state machine (`hda_stream_stop` / `_reset` / `_start`): the HDA spec makes RUN asynchronous and the descriptor registers writable only after a reset, so stopping polls RUN to zero and every start re-runs SRST before reprogramming BDPL/LVI/CBL/FMT.  Its completion handler never writes the stream control register — it flags a deferred halt that the producer, drain or close path retires in process context — both because the spec advises against ISR read-modify-write on that register and because BCIS fires when a buffer is fetched into the controller FIFO rather than played, so an immediate stop would truncate the tail of every clip.
  - USB devices are enumerated under `/proc/devtree` and as `/dev/usb` nodes (`sys/drivers/usb/usbdevfs.c`), which `lsusb` reads.
- `sys/net/`: Network stack — TCP/IPv4, AF_INET / AF_UNIX sockets, loopback.
- `sys/exec/`: Executable loading and execution personalities.
  - Segmented 16-bit personalities run in real LDT segments rather than a flattened address space: ELKS (`elks_aout.c`) and SCO Xenix/286 (`xout286.c`) both give each program segment its own descriptor, so the selectors a 1980s linker baked into the binary resolve as they did on hardware and an out-of-range offset still faults. `x.out` covers the 8086, 80286 and 80386 Xenix targets under one magic number, so it is dispatched on the header's processor field: the 80286 loader is `xout286.c` (16-bit, `int $5` syscalls, `perso_sco_x286.c`) and the 80386 one is `xout.c` (32-bit, `lcall $7,$0`, `perso_xenix.c`). Each vendor/processor pairing has its own personality id, since they are unrelated ABIs.
- The process registry allocates every `process_t` dynamically and tracks live processes on a pid-hash plus an all-procs list; there is no fixed process-table cap. Live objects are not relocatable.

Detailed subsystem behavior belongs in `docs/specs/`, including:
- boot and initialization: `docs/specs/kmain_init.md`, `docs/specs/arch_i386_boot.md`, `docs/specs/bootloader_ext2_boot.md`
- memory management: `docs/specs/pmm.md`, `docs/specs/pmap.md`
- process model: `docs/specs/kern_process_exit.md`, `docs/specs/kern_pid1.md`
- VFS and filesystems: `docs/specs/vm_subsystem.md`, `docs/specs/fs_devfs.md`, `docs/specs/vfs_bio.md`
- device and terminal subsystems: `docs/specs/driver_model.md`, `docs/specs/driver_tty.md`, `docs/specs/driver_vt.md`, `docs/specs/driver_fb_console.md`, `docs/specs/driver_virtio_gpu.md`
- the ISA discovery path now includes both fixed-resource legacy probes and ISA Plug-and-Play isolation; activated ISA-PnP logical devices are registered on the ISA bus with cloned IO/IRQ/DMA/MEM resources so UART/LPT/IDE can bind through the driver model rather than ad hoc attachment.
- virtual terminals derive their physical geometry from the active console backend and reserve the last hardware row for kernel status UI; tty-visible geometry excludes that row.
- execution personalities: `docs/specs/personality_targets.md`, `docs/specs/personality_elks.md`

## 6. Userland and Libraries

Userland is split by role (essential, admin, extended) and supported by a suite of target-native libraries.

### Key Components
- **`libsys`:** The canonical typed interface for system introspection and control.
- **`ex`/`vi` Editor Stack:** The base editors remain first-class userland programs in `bin/`, with a shared implementation in `usr.lib/exvi/`, thin frontends in `bin/ex` and `bin/vi`, and an in-tree full-screen `vi` engine backed by PTY regression tests. Detailed editor design notes and backlog tracking live in `docs/specs/exvi.md`; the current standards/compatibility record lives in `docs/specs/exvi_conformance.md`; user-facing manuals are in `usr.man/man1/ex.1`, `usr.man/man1/vi.1`, and `usr.man/man1/view.1`.
- **`lib/edit`:** Command-line editing and history library for shells and prompts. Reusable low-level pieces for the editor stack are documented in `docs/specs/exvi.md`.
- **`lib/rt` (librt, `-lrt`):** POSIX realtime extensions. The POSIX message-queue wrappers (`mq_*`) are thin shims over the kernel's native mq syscalls (`sys/kern/posix_mqueue.c`); POSIX asynchronous I/O (`aio_*`, `lio_listio`) is a self-contained userspace worker-thread pool over libpthread (the glibc/librt model — no kernel AIO). Ships static + shared with DT_NEEDED `libpthread.so.0` and `libc.so.0`.
- **`find(1)`:** A multi-dialect file hierarchy walker. See `docs/find/architecture.md`.
- **C Toolchain:** Supplied by the GNU stage-2 port at
  `contrib/binutils/` (binutils 2.46.0) and `contrib/gcc/` (GCC 16.1.0).
  The cross-compiler runs on the host, the stage-2 native compiler
  runs on substrate.  `cc` on the image is a symlink to `gcc`.  See
  `contrib/BUILD-TOOLCHAIN.md` and `contrib/build-toolchain.sh`.
  ELF object and symbol inspection helpers (substrate-side `nm`,
  `readelf`, `ar`, etc.) still live under `usr.bin/` on top of
  `usr.lib/elfobj/` — see `usr.lib/elfobj/README.md` and
  `usr.lib/elfobj/ABI_POLICY.md`.  C++ cross-DSO exception unwinding is
  done and verified end-to-end via
  `contrib/gcc/patches/0010-libgcc-pt-gnu-eh-frame-substrate.patch`:
  `USE_PT_GNU_EH_FRAME`, `--eh-frame-hdr` via `LINK_EH_SPEC`, a shared
  `libgcc_s.so` (`t-slibgcc`), and `thread_file=posix`, so a single FDE
  registry — located at throw time via `dl_iterate_phdr(3)` in
  ld.so/libc — spans every loaded module instead of one static copy per
  DSO.  A throw in a `.so` is caught in the exe and TagLib reads FLAC
  metadata; C++ apps using `std::mutex` must link `-lpthread`
  (gthr-posix uses hard, non-weak pthread refs).  `gdb` (`contrib/gdb/`)
  also runs natively, atop the libsys `ptrace` PEEK bridge.  See
  `docs/toolchain.md` for the full toolchain detail.
- **System Shell:** `/bin/sh` is a symlink to `/usr/bin/zsh` from
  `contrib/zsh/` (zsh 5.9).  zsh detects argv[0]'s basename and
  enters POSIX `sh` emulation when invoked as `sh`.  The previous
  hand-rolled `bin/sh/` is retained in-tree but disabled at the
  `bin/Makefile` SUBDIRS level — zsh covers everything autoconf
  scripts probe for (functions, `<<-` heredocs, full parameter
  expansion, signal handling, fd redirection).
- **Terminal Handling:** Provided by `contrib/ncurses/` (ncurses
  6.4) — full terminfo backend with the upstream 2851-entry
  database under `/usr/share/terminfo/`.  Replaces the earlier
  `lib/curses/` link-time stub (which is now disabled at the
  `lib/Makefile` SUBDIRS level).  Consumers: zsh, vi, less, top,
  every other terminfo-aware tool.  The stub's source stays
  in-tree for size-constrained embedded profiles.
- **Manual Pages:** Substrate-native pages live in
  `usr.man/man<section>/` and install to `/usr/share/man/`.
  `contrib/mandoc/` provides the `mandoc` / `man` / `makewhatis`
  / `apropos` / `whatis` toolchain; the indexed database lives
  at `/usr/share/man/mandoc.db`.  `contrib/less/` provides the
  system pager (`/usr/bin/less` + `more` symlink), used by `man`
  for output paging.
- **Account Management:** `usr.sbin/{useradd,usermod,userdel,
  groupadd,groupmod,groupdel}` and `bin/groups` provide the
  POSIX user/group admin surface.  All share `lib/pwdb/`
  (`libpwdb.so.0`, public header `<sys/pwdb.h>`) which centralizes
  `/etc/passwd` / `/etc/group` / `/etc/shadow` parsing,
  `pwdb_atomic_rewrite` (write-to-`~`-then-rename), file locking
  via `flock`, ID allocation, name validation, and POSIX
  day-count helpers.
- **Display Manager:** `sbin/sdm/` supervises the graphical login —
  `sdm` (a shell supervisor) starts an `Xfbdev` server plus the
  `sgreet` Xlib greeter, and on session end tears X down and loops
  back to a fresh greeter.  `sgreet` authenticates against
  `/etc/passwd` + `/etc/shadow` (same policy as `bin/login`), offers
  a session chooser driven by `/etc/sdm/sessions` (`Label = command`
  entries; F1 cycles), and execs the choice via `/etc/X11/Xsession`,
  which runs `${SDM_SESSION:-matwm2}` as the session leader.
- **Build Orchestration:** `build.sh` at the repo root drives a
  clean-checkout end-to-end build in four stages: (0) cross
  toolchain via `contrib/build-toolchain.sh`, (1) native
  substrate (`sys/`, `lib/`, `usr.lib/`, `sbin/ld.so/`, `bin/`,
  `sbin/`, `usr.bin/`), (2) contrib ports in dependency order,
  (3) `build-rootfs.sh` to assemble `dist/` and bake
  `rootfs.img`.  After stage 1 and after each contrib build it
  mirrors libs + headers into the cross-toolchain sysroot at
  `${STAGE1_PREFIX}/i386-unknown-substrate/{lib,include}` so the
  next layer's `configure` probes find them.

## 7. ABI and Interface Boundaries

Substrate maintains strict ABI boundaries between the kernel and userland, and between toolchain outputs and the runtime loader. Any change to these boundaries requires synchronization across all affected layers and documentation updates.

### Library Build Policy

Every public library under `lib/` produces both flavors from a single source tree:

- **Static archive** `libX.a` — compiled with `USER_CFLAGS` (`-fno-pie`),
  what `-lX` resolves to in source-tree builds.  Used by every binary
  that statically links today (`bin/echo`, `bin/sh`, ..., `sbin/init`).
- **Shared object** `libX.so.0` — compiled with `SHLIB_CFLAGS`
  (`-fPIC`), linked with `-shared -Bsymbolic-functions -z now`,
  carrying the `DT_SONAME` `libX.so.0`.  Installed under
  `$(DESTDIR)/lib/`; the `libX.so` link-time symlink is created
  ONLY at install time so it doesn't shadow `libX.a` in the source
  tree (otherwise `-lc` would silently pull undefined symbols like
  `feraiseexcept` from `libc.so` into static-linked binaries).

Build infrastructure lives in `Makefile.inc` as `SHLIB_CFLAGS` /
`SHLIB_LDFLAGS`.  See `docs/design/ld.so-design.md` for the full
loader contract that `.so.0` files must honour.

`libsys` is the sole owner of the raw `syscall()` dispatcher and the
`sys_*` typed wrappers — `libc` no longer duplicates them, so there are
no colliding symbols when both are linked.  Every binary therefore
links `libsys`: `Makefile.bin.inc` adds `-l:libsys.a` (inside a
`--start-group` with `libc`/`libm`) for static links and
`-l:libsys.so.0` for dynamic ones.  A binary that calls `syscall()`
directly (e.g. `bin/ldtctl`) needs it on the link line because ld
won't follow `libc.so.0`'s `DT_NEEDED` to resolve an object-file
reference.

### Native crt0

`lib/c/arch/i386/crt0.S` is PIC-safe: it sets up `%ebx` as the GOT
pointer via `call/pop` + `_GLOBAL_OFFSET_TABLE_`, accesses `environ`
through `environ@GOT(%ebx)`, and routes every external call
(`__stdio_init`, `main`, `exit`) through `@PLT`.  This single object
serves both static and dynamic links — for static builds the linker
resolves the `@PLT`/`@GOT` references to direct addresses; for PIE
builds `/sbin/ld.so` fills the GOT slots during its relocation pass
before `_start` runs.

## 8. Testing Strategy

Testing is a first-class citizen, utilizing a multi-layer approach:
- **Isolated Unit Tests:** Validating individual modules.
- **System Integration Tests:** Verifying toolchain and kernel interoperability.
- **Host-Based Validation:** Using `NATIVE_BUILD=1` for rapid feedback.
- **Toolchain Conformance Inputs:** Native toolchain validation is expected to include out-of-tree package builds such as GNU coreutils and GNU Bash so `cc -> as -> ld` is exercised against large, real-world sources instead of only in-tree fixtures.
- **Linux Compatibility Harnesses:** Linux-host execution bridges live under
  `linux/`; `linux/runner/` provides the binfmt_misc-oriented Substrate i386
  ELF runner used for host-side ABI experiments.
- **Property and Fuzz Testing:** Ensuring robustness of parsers and ABI handlers.

ELF object/symbol helpers (`usr.lib/elfobj/`, the remaining stand-alone tools in `usr.bin/`) keep their regression surfaces under `usr.lib/elfobj/tests/` and `tests/usr.bin/<tool>/`. Userland program tests live under `tests/bin/<program>/` — all `bin/*/tests/` directories have been consolidated there; each `bin/*/Makefile` references its test sources via `$(TESTS) = ../../tests/bin/<program>`.  Toolchain (cc/as/ld/g++) regression is delegated to upstream binutils + GCC; substrate-specific patches in `contrib/{binutils,gcc}/patches/` are validated through the bootstrap orchestrator (`contrib/build-toolchain.sh`).

For detailed testing policies, see `docs/specs/vm_page.md` (as a template) and the `tests/` directory.

---
*This document focuses on structural and system-oriented architecture. Detailed mechanics belong in the linked subsystem specifications.*
