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
x86_64 core tables and syscall-entry wiring are now host-validated even though
full x86_64 runtime bring-up remains incomplete.

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
lib/         target runtime libraries (libc/libsys/libm/libpthread/libusb...)
lib/usb/     libusb 1.0-compatible userspace USB access library over usbdevfs
usr.lib/     shared libraries for tooling/runtime support (elfobj, demangle, ...)
include/     userspace public headers
tests/       unit/integration/regression/property/fuzz harnesses
docs/specs/  detailed subsystem specs
docs/tasks/  refactored task planning sections and requirement/story tracking
dist/        target root filesystem staging
host_dist/   host install staging for native validation tools
usr.man/     manual page source tree
```

`dist/` is reserved for the contents of a real Substrate target root filesystem.
Boot media artifacts such as `sys/kernel.flp`, ad hoc boot disks, and scratch
bring-up images are not architecture-level rootfs staging and must not be
stored under `dist/` unless they are themselves installed files inside `/`.

## 5. Kernel Architecture

Kernel source is under `sys/` and remains monolithic.

Major kernel layers:
- `sys/arch/`: architecture-specific CPU/MMU/interrupt/bootstrap code
- `sys/core/`: early init and global kernel startup
- `sys/kern/`: scheduler, signals, time, synchronization, kernel core
- `sys/pm/`: process manager (process lifecycle, pgrp, rusage, wait)
- `sys/vm/`: memory management (PMM/PMAP and VM internals)
- `sys/vfs/` and `sys/fs/`: VFS and filesystem implementations
- `sys/drivers/`: device drivers
- `sys/exec/`: executable loading and personality execution paths

Kernel worker model:
- `swapper` (PID 0) remains the idle/root kernel context.
- `swapper` owns one CPU-bound idle thread per online CPU.
- VM pressure is handled by a dedicated `pagedaemon` kernel process that sleeps on a wakeup channel and runs pageout work asynchronously.

Boot/init sequencing highlights:
- `kmain()` parses the kernel command line before runtime console registration so `console=console0|serial0..serial3` can steer bring-up output policy.
- boot-time Multiboot modules are registered as RAM block devices before root mount, allowing `initrd`-style root selection via `/dev/storage/ram*`.
- root mount accepts `rootfstype=` as either a single filesystem name, a comma-separated probe list, or `auto`; when unspecified, the i386 boot path probes the registered block-backed root filesystems in kernel order (currently `ext2`, `fat`, `minix`, `udf`).
- after root mount, the kernel ensures `/dev`, `/proc`, and `/sys` mountpoints exist and mounts `devfs`, `procfs`, and `sysfs` automatically.
- init is spawned before VM background workers so `PID 1` remains the first userspace process.
- i386 also provides a BIOS floppy boot artifact `sys/kernel.flp`: a fixed-layout 1.44MB image with a two-stage real-mode loader that prompts for a hand-typed kernel command line, falls back to a built-in default boot line when Enter is pressed on an empty prompt, refuses to boot on pre-386 CPUs, loads `kernel.zimage` from the floppy payload, patches the Linux boot header `cmd_line_ptr`, and then transfers control to the normal `zImage` setup entry.
- the i386 `zImage` setup path fabricates Multiboot memory information from BIOS services in descending fidelity order: `E820`, then legacy aggregate sizing via `E801`, then `INT 15h AH=88h`; if only aggregate sizing is available, the kernel reserves the first 1MB conservatively and seeds PMM from one extended-memory run above 1MB.
- the i386 `zImage` real-mode setup path now owns BIOS text-mode programming before protected-mode handoff: `vga=ask` prompts on the BIOS text console and applies the selected BIOS text mode, while explicit `textmode=` or `video=text:COLSxROWS` requests are applied directly without the menu. The setup stub appends canonical handoff tokens so the higher-half VT geometry stays aligned with the programmed mode after boot. The fixed menu always includes `80x25`, `80x43`, and `80x50`; when VBE text modes are advertised by the firmware, the selector also offers detected `132x25`, `132x43`, `132x50`, and `132x60` variants.

### 5.1 i386 PMAP Model

The i386 kernel is compiled to an explicit i486 baseline:
- kernel C objects use `-march=i486 -mtune=i486`
- compiler-generated Pentium+ instructions such as `cmov` are not permitted in the core kernel image
- newer CPU instructions remain isolated to explicit runtime-gated code paths (for example `cpuid`, `rdtsc`, `fxsave`, `rdrand`) and must not execute unless the early CPU feature probe has marked them present
- PCI support is optional at runtime; the PCI layer first verifies configuration mechanism #1 is present and degrades to a no-op/all-ones config space view on non-PCI machines such as older 486-class systems

The i386 PMAP implementation is a two-level paging design:
- page directory + page tables
- recursive self-map at PDE 1023
- `V_PD` at `0xFFFFF000` and `V_PT(n)` at `0xFFC00000 + n * 4096`
- per-process user PDEs in slots `0..767`
- shared kernel PDEs in slots `768..1022`
- bootstrap direct map of physical `0..1004MB` in the higher half, with PDE 1019 reserved for LAPIC MMIO
- when PSE is available, the first higher-half 4MB window (`0xC0000000..0xC03FFFFF`) is installed as a single large PDE so the kernel image/text/data execute inside a 4MB mapping from the first paging handoff

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
- the i386 PMM wrappers now actively constrain generic page and contiguous
  allocations to the direct-mapped ceiling and reject frees outside that
  window, preventing wrapped low virtual addresses on high-RAM machines

Fork and copy paths use copy-on-write with per-page reverse mappings (`pv_entry`) so the VM layer can inspect hardware accessed/dirty state and resolve COW faults without synthetic software shadow bits.

i386 legacy-execution support includes a bounded VM86 path:
- `sys_vm86()` and BSD `sysarch(I386_VM86, ...)` copy user VM86 state into kernel-owned structures before entry
- `vm86_enter()` asserts `EFLAGS.VM|IF` and enters VM86 through an `iret` frame
- GPFs taken with `EFLAGS.VM` set are redirected to `vm86_gpf_handler()` for opcode emulation (`CLI`, `STI`, `PUSHF`, `POPF`, `INT`, `IRET`, basic `IN`/`OUT`) or monitor fault reporting
- the per-CPU TSS I/O bitmap is initialized deny-all and can be opened per-port or per-range through the exported TSS bitmap helpers
- the detailed contract is defined in `docs/specs/arch_i386_vm86.md`

i386 exception diagnostics are explicit and stable:
- exception reporting emits the exception name plus saved general-register state before escalation
- invalid-opcode faults dump up to 16 instruction bytes at `EIP` when the address is safe to read
- the panic path emits a fixed high-visibility `*** KERNEL PANIC ***` banner, the fatal message, a stack trace, and a halt footer

### 5.2 i386 TLB Strategy

TLB management on i386 follows a tiered strategy:
- single-page updates use `invlpg`
- bulk local flushes use CR3 reload
- SMP invalidation uses LAPIC IPIs to other CPUs plus an acknowledgement barrier
- kernel mappings use PGE/global bits when supported
- global flushes use the CR4.PGE toggle path

i386 SMP execution model:
- the kernel currently supports up to `96` CPUs
- early CPU feature probing supports pre-CPUID and non-APIC i486-class systems by detecting 486-vs-386 through EFLAGS toggling, refusing to assume `CR4`, and falling back to PIC/UP mode when LAPIC support is absent
- CPU discovery prefers ACPI MADT and falls back to Intel MP tables; validated MP configuration table addresses are cached so later discovery passes do not depend on BIOS page-zero mappings after NULL protection is enabled
- AP bootstrap uses a copied low-memory trampoline that enters protected mode, loads the live BSP CR4/CR3/CR0 state, enables paging, and jumps into the higher-half C entry point
- MADT parsing now registers I/O APICs and ISA IRQ-to-GSI overrides before the scheduler starts userspace
- `execve()` temporarily binds the calling thread to its current CPU and suppresses timer-driven rescheduling until the final userspace handoff, then restores floating scheduling state

Device namespace policy in `devfs`:
- Root pseudo devices remain at `/dev/*` (for example `/dev/null`, `/dev/zero`).
- Storage block devices are exposed under `/dev/storage/*`.
- Raw disk providers remain visible as `/dev/storage/<disk>` (for example `/dev/storage/ide0`), with GEOM-derived partition nodes exposed alongside them (for example `/dev/storage/ide0p1`).
- BSD disklabels additionally expose lettered slice nodes, with `c` reserved as the whole-container alias only when a BSD disklabel is present.
- Communication character devices self-register under `/dev/comm/*` (for example `/dev/comm/serial0`, `/dev/comm/parallel0`).
- USB character devices are reserved under `/dev/usb/busN/devM`, with the usbdevfs ioctl ABI carried by `<sys/usbdevfs.h>` and the published permission contract `root:usb` mode `0664`.
- Device-model managed nodes may be published through `device_publish()` and withdrawn through `device_unpublish()`, allowing add/remove lifecycle to drive devfs automatically for drivers that opt into the framework path.
- Stable device aliases are exposed under `/dev/by-id/*` when a device model entry carries a serial string or GUID.
- Nested device paths are accepted only under predeclared subsystem directories (namespace hardening against arbitrary roots like `/dev/notreal/*`).

Driver-model and legacy bus notes:
- The core bus model now includes PCI and legacy ISA buses. PCI remains optional at runtime and old non-PCI 486-class systems are handled by a fixed-resource ISA probe pass instead of assuming PCI presence.
- `isa_probe_legacy()` registers standard ISA-era devices (UART, LPT, IDE, PS/2) on the ISA bus when their fixed ports respond, including tertiary/quaternary IDE legacy ports, so the kernel device tree remains meaningful on pre-PCI hardware and ISA add-in storage controllers.
- the IDE core now registers ISA and PCI drivers with the framework instead of being attached directly from `main`; on old non-PCI systems it consumes ISA bus hints before probing, and on PCI systems it binds against IDE-class PCI devices.
- the floppy controller now also binds through legacy ISA presence hints, using the classic `0x3F0`/`0x370` controller bases and publishing detected drives as `/dev/storage/fd*` block devices through the storage layer.
- when a PCI IDE controller is present, the IDE core consumes the controller's programming-interface bits and BAR layout for native-mode channel bases and bus-master DMA windows before probing drives, while still retaining legacy fixed-base support for ISA/compatibility-mode systems.
- the AHCI driver registers a PCI driver against class 0x01/0x06/0x01 (Mass Storage / SATA / AHCI 1.0) through the device-model framework. It maps BAR5 via `pci_iomap()`, enables bus mastering, takes AHCI ownership from BIOS, allocates DMA-coherent command lists, FIS receive areas, and command tables per port, probes each implemented port for device signatures, issues IDENTIFY DEVICE for SATA disks and publishes them as `/dev/storage/sataN` block devices, and wraps SATAPI (ATAPI-over-SATA) optical drives behind the SCSI mid-layer via `scsi_link_t`. The driver operates in polling mode with a single command slot per port.
- the VirtIO family no longer performs its own private PCI rescan during init; block, 9P, and SCSI transports now register per-device PCI drivers against the framework-owned PCI device list and bind existing devices through the generic probe/attach path.
- late driver registration now binds already-enumerated devices immediately instead of only probing them, so controller families migrated onto the device model work regardless of whether the bus enumerator or the driver registers first.
- Controller-family implementation work such as IDE transport internals, VirtIO transport refactors, USB host controllers, and ISA-PnP protocol support is tracked under the driver tasklists rather than the bus-core tasklist.

Power-management model:
- The device model owns tree-wide suspend/resume traversal (`device_suspend_all()` / `device_resume_all()`).
- A minimal runtime PM core exists in the device layer with opt-in autosuspend (`device_runtime_enable/get/put/poll()`); it provides framework policy but not a separate userspace-facing power daemon or ACPI policy engine.

Console policy:
- the default screen console remains `console0`
- `console=serial0..serial3` selects COM1..COM4 respectively for runtime console routing
- `serial_debug` or `console=serialN` registers the UART backend with the kernel console framework for mirrored output
- the hardware text console now treats the last physical text row as a kernel-owned status line rendered black-on-white; the usable tty geometry reported to userland excludes that row (for example `80x24` on an `80x25` mode, `80x49` on an `80x50` mode)
- the timer tick raises a once-per-second wakeup for a dedicated kernel `vtstatus` thread, which refreshes the status line without doing the redraw work directly in interrupt context; the line currently shows the active VT number plus wall-clock time in ISO 8601 UTC form
- init/stdout/stderr attachment is done through the `/dev/console` facade while the process controlling-tty pointer is set to the active VT tty, so console file descriptors continue to follow the active kernel text console without losing tty ioctl/job-control semantics
- global wall-clock and scheduler timeout accounting advance only from CPU 0; secondary CPUs may take local preemption ticks, but they must not multiply system time
- `video=text` keeps the system on the hardware text console even when framebuffer drivers are available
- `textmode=` and `video=text:COLSxROWS` select hardware text geometry for the kernel text console; the BIOS setup path on `zImage` and floppy boots can program `80x25`, `80x43`, `80x50`, and any detected VBE text modes such as `132x60`, while the higher-half driver directly reprograms only the stable in-kernel `80x25` and `80x50` cases and otherwise trusts the BIOS-programmed geometry handoff
- EFI boots now translate GOP framebuffer state into the Multiboot framebuffer fields consumed by the higher-half video stack, and the generic framebuffer core preserves firmware-provided channel layouts so GRUB or EFI framebuffers with RGBX or BGRX packing render correctly without a dedicated mode set
- the EFI boot stub enumerates all GOP modes via `QueryMode`, selects the highest-resolution 32-bit linear mode that does not exceed 1920x1200, and calls `SetMode` before populating the Multiboot info; if the current mode is already optimal, no switch occurs
- EFI runtime services (`GetTime`, `SetTime`, `ResetSystem`, `GetVariable`, `SetVariable`) are saved before `ExitBootServices` and wired into a kernel-facing `efi_runtime` interface (`kern/efi_runtime.c`) initialised during boot; the interface gracefully returns failure on non-EFI boots via a weak symbol
- the VT layer owns per-console backing buffers, per-VT `tty` bindings, and per-VT scrollback history; the VGA text backend owns all active-screen redraw and cursor updates so VT switching does not memcpy live VGA memory directly
- the active text console exports `/dev/tty1` through `/dev/tty12`; `Alt+F1..F12` switches VTs and `Shift+PageUp/PageDown` enters and exits scrollback on the active VT

Execution personalities support native behavior plus Linux/FreeBSD compatibility paths where implemented.
- Linux signal compatibility is explicit at the ABI edge: Linux signal numbers,
  sigsets, frame layouts, and optional `sa_restorer` callbacks are translated
  by the Linux personality without redefining the native Substrate signal
  contract.

Executable identity policy:
- `execve()` treats the backing object identity, not the pathname string, as the canonical executable identity
- the current ELF metadata cache is keyed by filesystem identity plus inode identity and stores immutable image parse results (ELF header, program headers, `PT_INTERP`, AUXV `AT_PHDR` derivation)
- filesystem implementations are expected to provide stable `fs_node->inode` values; FAT synthesizes stable identities for entries that do not have a useful cluster-backed inode number

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

ELKS personality contract:
- ELKS binaries are recognized as Minix-style 16-bit `a.out` images and loaded
  through a private per-process LDT.
- Per-process ELKS LDT state is serialized by a dedicated process lock so LDT
  growth, replacement, clone, free, and activation see a coherent descriptor
  table under multi-threaded activity.
- Canonical full-size LDT tables are backed by a dedicated UMA zone, with a
  fallback path retained for short transitional tables created during loader
  and clone/grow operations.
- ELKS execution runs as a 16-bit protected-mode personality (`BITNESS_16`),
  not VM86.
- ELKS syscalls use `INT 0x80` with the ELKS register argument order
  `BX, CX, DX, DI, SI`.
- ELKS `signal()` / `kill()` are translated at the personality edge, and
  signal delivery uses the ELKS libc callback convention: the kernel pushes a
  far-return frame for `_signal_cbhandler(sig)` on the ELKS user stack and
  resumes the interrupted `CS:IP` via `lret $2`.
- i386 also exposes a native per-process `modify_ldt(2)` contract through `<sys/ldt.h>` and `libsys`. The ABI is single-sourced by the public `struct user_desc`; `LDT_READ` copies the current process LDT image, `LDT_WRITE` accepts exactly one validated user descriptor per call, and `LDT_READ_DEFAULT` currently returns 0. Invalid descriptors or sizes fail with `EINVAL`, inaccessible buffers fail with `EFAULT`, and LDT-growth failure returns `ENOMEM`. The Linux i386 personality also wires syscall `123` (`modify_ldt`) to this same hardened path and exposes matching syscall-trace metadata (`int`, `pointer`, `long`) for compatibility tracing.
- The detailed i386 LDT ownership, locking, permission model, rejection rules,
  and verification matrix are defined in `docs/specs/arch_i386_ldt.md`.
- ELKS `/dev/kmem` compatibility is personality-scoped rather than native:
  ELKS processes opening native `/dev/kmem` are given an ELKS-shaped synthetic
  task snapshot through intercepted `ioctl`, `lseek`, and `read` operations so
  upstream ELKS process-inspection tools can run without changing native
  `/dev/kmem` semantics. The synthetic image now exports task, `_seg_all`, and
  `_heap_all` rings plus bounded `MEM_GETUSAGE` accounting so upstream ELKS
  `ps` and `meminfo` can run coherently. The exported task-table view reserves
  slot 0 for the swapper/idle view so older ELKS `ps` binaries that start
  scanning at slot 1 still see the active ELKS process table coherently.
- `INT 0x20` from ELKS userspace is treated as a Minix-86 trap attempt, logged,
  and converted into `SIGSYS`.

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

### 6.2 find(1) — Multi-Dialect File Hierarchy Walker

`bin/find/` implements a POSIX.1-2024 `find` with FreeBSD-default semantics,
OpenBSD/NetBSD deltas, and GNU findutils extensions.

**Architecture** (4 source files + shared header):
- `find.h` — shared types: `node_t` AST nodes, `entry_t` per-file state, global traversal variables, debug flags
- `find_main.c` — main entry, Phase 1–4 orchestration (startup options → paths → expression → traversal)
- `find_parse.c` — recursive descent parser (precedence: `()` > `!` > AND > OR), optimizer (cost-based AND reordering)
- `find_eval.c` — expression evaluator, output (print/ls/printf/file-directed), exec helpers (batch + execdir PATH safety)
- `find_traverse.c` — directory traversal engine with loop detection, xdev, sorted readdir, depth-first support

**Semantic token classes** (per the 4-class model):
1. **Startup options** — `-H`, `-L`, `-P`, `-E`, `-s`, `-x`, `-X`, `-D`, `-O`, `-regextype`, `-f`, `-files0-from`
2. **Global modifiers** — `-depth`, `-xdev`, `-maxdepth`, `-mindepth`, `-follow`, `-daystart`, etc. (return NODE_TRUE in AST)
3. **Pure tests** — `-name`, `-type`, `-perm`, `-newer`, `-regex`, etc. (no side effects)
4. **Actions** — `-print`, `-exec`, `-delete`, `-quit`, etc. (side-effecting; inhibit implicit `-print`)

**Dialect conflict policy** documented in `docs/find/conflicts.md`.
Feature matrix and implementation status in `docs/find/spec-baseline.md`.

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

### Kernel Test Suite

The kernel test suite (`tests/sys/`) is **not** compiled into the kernel by default.
To build a kernel with the test suite linked in, pass `KERNEL_TESTS=1`:

```
make -C sys KERNEL_TESTS=1
```

At boot, pass `test=<name>` or `test=all` on the kernel command line to run tests.
Without `KERNEL_TESTS=1`, the tests target compiles a no-op stub (`kern/tests_stub.c`).

### Host Tests

Host-runnable tests (`host_test_*` in `tests/sys/`) are built separately and run on the
host OS for quick validation without booting the kernel. Build them with:

```
make -C tests/sys
```

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
