# Substrate

Substrate is a complete Unix operating system project.

It is not just a kernel repository. The tree contains:
- a native kernel (`sys/`)
- base userland (`bin/`, `sbin/`, `usr.bin/`)
- system libraries (`lib/`, `usr.lib/`)
- an in-tree C toolchain (`usr.bin/cc`, `usr.bin/as`, `usr.bin/ld`, `usr.lib/elfobj`)
- build/install staging for both target and host (`dist/`, `host_dist/`)

## Design Origin

Substrate started as an original monolithic i386 Unix-like kernel with:
- a conventional process/syscall model
- virtual memory, scheduler, VFS, and core drivers
- a minimal C userspace and bootstrap-oriented build flow

The original toolchain direction was equally explicit: own the C compilation path end-to-end (preprocess, compile, assemble, link), while keeping host bootstrap practical during bring-up.

That original design intent remains:
- Substrate is a system, not a single component.
- Kernel, libc, and toolchain are co-designed.
- Host bootstrap is a means, not the final architecture.

## Current Scope

### Kernel
- Monolithic kernel with primary i386 support and active x86_64 work.
- Core subsystems: VM/PMM/PMAP, scheduler, process/signal model, VFS, exec personalities.
- Driver stack includes storage, input, serial, framebuffer/video, and virtualization-facing devices.

### Userland
- Core Unix programs under `bin/` and `sbin/`.
- Expanded tool/user programs under `usr.bin/`.
- System headers in `include/`.

### Libraries
- C library and syscall wrappers under `lib/`.
- Additional runtime/tool libraries under `usr.lib/` (including `libelfobj` and demangling work).

### Toolchain
- `usr.bin/cc`: C compiler driver + frontend/middle/backends.
- `usr.bin/as`: assembler.
- `usr.bin/ld`: linker.
- `usr.lib/elfobj`: ELF object model and manipulation library shared by tooling.

## Repository Layout

```text
sys/         kernel source
bin/         base user commands
sbin/        system administration commands
usr.bin/     toolchain and additional user tools
lib/         target runtime libraries (libc, libsys, etc.)
usr.lib/     reusable tool/user libraries (elfobj, demangle, ...)
include/     userspace headers
tests/       test suites
docs/specs/  subsystem and tool specs
dist/        target rootfs staging
host_dist/   host-install staging for native validation tools
```

## Build Model

Substrate uses two build modes:

- Target build: builds for Substrate runtime environment.
- Host build (`NATIVE_BUILD=1`): builds host-runnable binaries for validation and bring-up.

Key rule: host mode is for validation/bootstrap only; target libraries and ABI behavior must remain Substrate-correct.

## Documentation

- `ARCHITECTURE.md`: system architecture and integration model.
- `AGENTS.md`: project constraints and engineering directives.
- `TASKS.md`: active execution plan/checklist.
- `docs/specs/`: detailed subsystem specifications.

## Contributing

Substrate expects production-quality engineering hygiene:
- small scoped commits
- tests with behavior changes
- architecture docs updated when structure changes
- no silent drift between kernel/userland/toolchain contracts

Read `AGENTS.md` before making non-trivial changes.
