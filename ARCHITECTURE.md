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

## 2. Original Design (Foundational Intent)

Substrate originally started as:
- an original monolithic i386 kernel
- a Unix process/syscall model
- a minimal but real userspace stack
- an explicit plan to own C toolchain behavior rather than permanently outsourcing it

Original design principles still apply:
- system-first design, not component-first design
- ABI correctness before convenience
- host bootstrap support is temporary scaffolding, not terminal architecture
- kernel/userspace/toolchain contracts are explicit and testable

## 3. Current Architecture (Top-Level)

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

Substrate is developed as one integrated system:
- toolchain emits binaries expected by kernel loader/runtime
- libc/libsys API and syscall ABI co-evolve with kernel
- ELF tooling is centralized in `libelfobj`

## 4. Source Tree Architecture

```text
sys/         kernel
bin/         base Unix userland
sbin/        system utilities
usr.bin/     compiler/toolchain and extended user tools
lib/         target runtime libraries (libc/libsys/libm/libpthread...)
usr.lib/     shared libraries for tooling/runtime support (elfobj, demangle, ...)
include/     userspace public headers
tests/       unit/integration/regression/property/fuzz harnesses
docs/specs/  detailed subsystem specs
docs/tasks/  refactored task planning sections and requirement/story tracking
dist/        target root filesystem staging
host_dist/   host install staging for native validation tools
```

## 5. Kernel Architecture

Kernel source is under `sys/` and remains monolithic.

Major kernel layers:
- `sys/arch/`: architecture-specific CPU/MMU/interrupt/bootstrap code
- `sys/core/`: early init and global kernel startup
- `sys/kern/`: scheduler, signals, time, synchronization, process core
- `sys/vm/`: memory management (PMM/PMAP and VM internals)
- `sys/vfs/` and `sys/fs/`: VFS and filesystem implementations
- `sys/drivers/`: device drivers
- `sys/exec/`: executable loading and personality execution paths

Kernel worker model:
- `swapper` (PID 0) remains the idle/root kernel context.
- `swapper` owns one CPU-bound idle thread per online CPU.
- VM pressure is handled by a dedicated `pagedaemon` kernel process that sleeps on a wakeup channel and runs pageout work asynchronously.

### 5.1 i386 PMAP Model

The i386 PMAP implementation is a two-level paging design:
- page directory + page tables
- recursive self-map at PDE 1023
- `V_PD` at `0xFFFFF000` and `V_PT(n)` at `0xFFC00000 + n * 4096`
- per-process user PDEs in slots `0..767`
- shared kernel PDEs in slots `768..1022`
- bootstrap direct map of physical `0..1004MB` in the higher half, with PDE 1019 reserved for LAPIC MMIO

Address-space layout is fixed:
- user virtual address space: `0x00000000..0xBFFFFFFF`
- kernel virtual address space: `0xC0000000..0xFFFFFFFF`
- kernel image load address: `0x00100000`
- kernel image link address: `0xC0100000`

Physical-memory bootstrap is two-stage:
- early PMM metadata allocation is constrained to the first 8MB of RAM
- after `pmap_bootstrap()` installs the larger kernel direct map, PMM promotes itself into a full page database sized from the detected RAM map when that metadata fits inside the direct-mapped window
- current i386 PMM accounting is capped at 3GB physical RAM
- pages above the current direct-mapped physical ceiling are detected and accounted, but are not yet exposed to generic kernel allocators that rely on `phys + 0xC0000000`

Fork and copy paths use copy-on-write with per-page reverse mappings (`pv_entry`) so the VM layer can inspect hardware accessed/dirty state and resolve COW faults without synthetic software shadow bits.

### 5.2 i386 TLB Strategy

TLB management on i386 follows a tiered strategy:
- single-page updates use `invlpg`
- bulk local flushes use CR3 reload
- SMP invalidation uses LAPIC IPIs to other CPUs plus an acknowledgement barrier
- kernel mappings use PGE/global bits when supported
- global flushes use the CR4.PGE toggle path

i386 SMP execution model:
- the kernel currently supports up to `96` CPUs
- AP bootstrap uses a copied low-memory trampoline that enters protected mode, loads the live BSP CR4/CR3/CR0 state, enables paging, and jumps into the higher-half C entry point
- `execve()` temporarily binds the calling thread to its current CPU and suppresses timer-driven rescheduling until the final userspace handoff, then restores floating scheduling state

Device namespace policy in `devfs`:
- Root pseudo devices remain at `/dev/*` (for example `/dev/null`, `/dev/zero`).
- Storage block devices are exposed under `/dev/storage/*`.
- Raw disk providers remain visible as `/dev/storage/<disk>` (for example `/dev/storage/ide0`), with GEOM-derived partition nodes exposed alongside them (for example `/dev/storage/ide0p1`).
- BSD disklabels additionally expose lettered slice nodes, with `c` reserved as the whole-container alias only when a BSD disklabel is present.
- Communication character devices self-register under `/dev/comm/*` (for example `/dev/comm/serial0`, `/dev/comm/parallel0`).
- Nested device paths are accepted only under predeclared subsystem directories (namespace hardening against arbitrary roots like `/dev/notreal/*`).

Execution personalities support native behavior plus Linux/FreeBSD compatibility paths where implemented.

Planned x86 Unix personality targets:
- Substrate native ABI (primary)
- Linux
- FreeBSD
- NetBSD
- OpenBSD
- Solaris / SVR4 family
- SunOS 4.x (Sun386i)
- Microsoft Xenix `MS-X/86`
- Microsoft Xenix `MS-X/286`
- Microsoft Xenix `MS-X/386`
- SCO Xenix `SCO-X/86`
- SCO Xenix `SCO-X/286`
- SCO Xenix `SCO-X/386`
- SCO Unix 3.2v2 (`SCO-U/3.2v2`)
- SCO Unix 3.2v4 / ODT3 (`SCO-U/ODT3`)
- SCO OpenServer 5 (`SCO-OSR5`)
- iBCS2 compatibility targets
- ELKS (16-bit Linux-like) personality
- Minix `a.out` compatibility path

## 6. Userland and Libraries

Userland is split by role:
- `bin/`: essential commands for a usable base system
- `sbin/`: system administration/maintenance tools
- `usr.bin/`: extended tools including toolchain and ELF utilities

Libraries:
- `lib/`: target runtime libraries (Substrate ABI contracts)
- `usr.lib/`: reusable libraries for tools and runtime components
  - `usr.lib/link`: reusable link-plumbing library used by `bin/ln` for POSIX/BSD/GNU option and destination-resolution semantics

Headers in `include/` define userspace-facing ABI/API surfaces.

### 6.1 Canonical System-Introspection Surface (`libsys`)

`lib/sys` plus `include/sys/sysinfo.h` is the canonical userspace interface for:
- process enumeration/introspection (`sys_proc_*`)
- VM and memory telemetry (`sys_vm_*`, `sysinfo`)
- CPU/system metadata (`sys_cpu_*`, `sys_uptime`, `sys_hostname`, etc.)
- mount and filesystem control (`mount(2)`, `umount(2)`, future `sys_mount_list`)

Architecture rule:
- Base system tools must prefer these typed APIs over ad-hoc parsing of kernel internals.
- `/proc` and `/sys` are compatibility and observability surfaces, not a replacement for stable typed ABI contracts.

Command integration contract:
- `bin/ps` and `bin/top`: implement process/cpu views via `sys_proc_*` and `sys_cpu_*`.
- `bin/free`: implement memory reporting via `sys_vm_stats`/`sysinfo`.
- `bin/mount` and `bin/umount`: implement control path via `mount(2)`/`umount(2)` and mount-list reporting via typed APIs when available.

## 7. Toolchain Architecture

### 7.1 Compiler

`usr.bin/cc` contains:
- standalone preprocessor behavior (`cpp` mode and `-E` path)
- lexer/parser/sema frontend
- IR/middle-end pipeline
- x86 backend and assembly emission
- driver orchestration for compile/assemble/link stages

### 7.2 Assembler

`usr.bin/as` is the in-tree assembler.

Design goal:
- parse and encode native assembly formats directly in-tree
- produce correct ELF relocatable objects without placeholder text/data emission
- preserve deterministic diagnostics and tool behavior

### 7.3 Linker

`usr.bin/ld` is the in-tree ELF linker.

Design goal:
- host-aware operation for development convenience
- correct ELF link semantics for executable and shared-object generation
- no mandatory dependency on external compiler wrappers in steady state

### 7.4 ELF Core Library

`usr.lib/elfobj` is the shared ELF substrate for:
- linker/assembler object handling
- readelf/objdump/nm/strip-family behavior
- validation and structured diagnostics

`libelfobj` is the canonical ELF model boundary across toolchain components.

## 8. Build and Deployment Model

Two explicit build modes:

- Target mode:
  - builds for Substrate runtime environment
  - uses target libraries/ABI contracts

- Host mode (`NATIVE_BUILD=1`):
  - builds host-runnable binaries for validation and bring-up
  - may use host libc/runtime at execution time
  - must not mutate target ABI definitions to satisfy host behavior

Install staging:
- `dist/`: target filesystem image/staging
- `host_dist/`: host install prefix staging for validation tools

## 9. ABI and Interface Boundaries

Critical boundaries:
- syscall ABI between userland and kernel
- ELF ABI expectations between toolchain outputs and loader/runtime
- libc/libsys interfaces consumed by user programs

Any boundary change requires:
- code changes in all affected layers
- updated tests
- documented architecture/spec updates

### 9.1 Introspection ABI Stability

The `sys_proc_*`, `sys_vm_*`, `sys_cpu_*`, and system-info interfaces are first-class architecture boundaries.
Changes to these interfaces require:
- versioned structure/layout review (`include/sys/sysinfo.h`)
- syscall number/dispatch review
- wrapper parity review (`lib/c` and `lib/sys`)
- userland consumer validation (`ps`, `top`, `free`, `mount`-family tools)
- man-page and `docs/syscalls/` synchronization

## 10. Testing Strategy (System-Level)

Testing is multi-layer:
- unit tests for isolated modules
- integration tests for toolchain + ELF + runtime paths
- regression tests for previously fixed bugs
- property/fuzz tests for parser/ELF robustness

Architecture policy:
- toolchain behavior changes must include assembly/link/runtime validation
- kernel changes must not silently invalidate userspace/toolchain assumptions

## 11. Documentation Policy

When architecture meaningfully changes, update:
- this file (`ARCHITECTURE.md`)
- relevant specs in `docs/specs/`
- task tracking in `TASKS.md` when execution plans shift

This document should stay concise, structural, and system-oriented.
Detailed implementation mechanics belong in subsystem spec documents.
