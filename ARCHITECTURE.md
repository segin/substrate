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

Headers in `include/` define userspace-facing ABI/API surfaces.

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
