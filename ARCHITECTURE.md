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
  PT_INTERP base for every dynamically-linked native binary; performs
  recursive DT_NEEDED traversal, symbol resolution (DT_GNU_HASH +
  DT_HASH), i386 REL/JMPREL relocations (RELATIVE / GLOB_DAT /
  JMP_SLOT / 32 / PC32 / COPY / TLS_TPOFF), DT_INIT_ARRAY +
  DT_FINI_ARRAY execution, per-thread TLS install (PT_TLS images
  copied into a variant-II layout and the GS base installed via the
  native `sys_set_gsbase` syscall), and runtime `dlopen` / `dlsym` /
  `dlclose` for plugin-style loading.  ld.so registers itself in the
  loaded-object scope so its `__ldso_*` exports are visible to libdl
  and to libc (which calls `__ldso_run_fini` as a weak hook from
  `exit()` to run destructors before the process is reaped).
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
sbin/ld.so/  Substrate native dynamic linker.  Phases 1-4f live:
             bootstrap + auxv handoff (1), self-relocate + parse
             program PT_DYNAMIC (2), recursive DT_NEEDED loading +
             REL/JMPREL relocations (3, 4a), DT_INIT_ARRAY
             execution (4b), variant-II TLS install via GS segment
             (4c), R_386_COPY relocations (4d), runtime
             dlopen/dlsym/dlclose API (4e), DT_FINI_ARRAY at
             exit() time via libc weak hook (4f).  Per-object
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
contrib/     third-party components (ext2-boot bootloader, ...)
tools/       build and install helper scripts
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
- process and thread registries reserve embedded bootstrap slots for PID/TID 0 context, then grow with stable dynamically allocated slot chunks; live objects are not relocatable.

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
- **`find(1)`:** A multi-dialect file hierarchy walker. See `docs/find/architecture.md`.
- **C Toolchain:** Supplied by the GNU stage-2 port at
  `contrib/binutils/` (binutils 2.46.0) and `contrib/gcc/` (GCC 16.1.0).
  The cross-compiler runs on the host, the stage-2 native compiler
  runs on substrate.  `cc` on the image is a symlink to `gcc`.  See
  `contrib/BUILD-TOOLCHAIN.md` and `contrib/build-toolchain.sh`.
  ELF object and symbol inspection helpers (substrate-side `nm`,
  `readelf`, `ar`, etc.) still live under `usr.bin/` on top of
  `usr.lib/elfobj/` — see `usr.lib/elfobj/README.md` and
  `usr.lib/elfobj/ABI_POLICY.md`.

## 7. ABI and Interface Boundaries

Substrate maintains strict ABI boundaries between the kernel and userland, and between toolchain outputs and the runtime loader. Any change to these boundaries requires synchronization across all affected layers and documentation updates.

### Library Build Policy

Every public library under `lib/` produces both flavors from a single source tree:

- **Static archive** `libX.a` — compiled with `USER_CFLAGS` (`-fno-pie`),
  what `-lX` resolves to in source-tree builds.  Used by every binary
  that statically links today (`bin/echo`, ..., `sbin/init`).
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
