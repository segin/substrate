# i386 LDT Architecture

## Scope

This document defines the current production contract for the i386 Local
Descriptor Table (LDT) path used by native `modify_ldt(2)` and ELKS private
16-bit execution contexts.

## Current Implementation Audit

The active implementation spans:

- `sys/arch/i386/ldt.c`
- `sys/arch/i386/sched.c`
- `sys/pm/process.c`
- `sys/exec/formats/elf.c`
- `sys/exec/formats/elks_aout.c`
- `tests/sys/test_ldt.c`
- `tests/sys/host_test_ldt_lifecycle.c`
- `tests/sys/host_test_ldt_race.c`
- `tests/sys/host_test_i386_sched_ldt.c`

Implemented today:

- per-process LDT storage attached directly to `process_t`
- private `ldt_lock` protecting pointer/count replacement
- lazy growth for native `modify_ldt(2)`
- explicit allocate/replace/clone/free helpers
- explicit set/clear/read helper paths inside `sys_modify_ldt()`
- context-switch `LDTR` activation and clear-on-no-LDT behavior
- fork inheritance through `ldt_clone_process()`
- flat ELF exec cleanup through `ldt_free_process()`
- process teardown cleanup through `ldt_free_process()`
- full-size LDT UMA backing for the canonical 8192-entry case
- Linux personality compatibility path for syscall `123` (`modify_ldt`)
- internal validation/allocation failure counters via `ldt_get_diag_snapshot()`

Still missing or intentionally deferred:

- fuzz coverage for malformed `modify_ldt` argument space
- broader integration coverage beyond native and Linux caller surfaces
- x86_64 LDT contract work

## Ownership Model

- LDT state is owned by `process_t`.
- The active table is represented by:
  - `proc->ldt`
  - `proc->ldt_entry_count`
  - `proc->ldt_is_uma`
- No LDT state is shared across unrelated processes.
- Fork clones the caller LDT by value; the child receives a private copy.
- Exec of flat ELF images clears any inherited private LDT state.
- ELKS exec installs a new private LDT for the new image.
- Exit frees the process LDT exactly once during teardown.

## Locking Policy

- `proc->ldt_lock` serializes LDT pointer/count replacement and active-table
  inspection.
- Callers may allocate replacement storage outside the lock, then install it
  under the lock.
- `ldt_activate_locked()` requires the caller to hold `ldt_lock`.
- `ldt_activate()` is the public switch helper and acquires `ldt_lock`
  internally when needed.
- Remote CPUs never mutate another process LDT directly; activation happens
  when the target process is scheduled on a CPU.

## Lifecycle Rules

- Context switch:
  - `arch_switch_to()` activates the next process pmap first and then calls
    `ldt_activate(next->proc)`.
  - Processes with no LDT clear `LDTR` instead of inheriting stale state.
- Fork:
  - `proc_fork_common()` clones LDT state by value through
    `ldt_clone_process()`.
  - Parent and child never share the same mutable LDT allocation.
- Exec:
  - Flat native/Linux/FreeBSD ELF exec clears any inherited private LDT state
    before creating the new `vm_map`.
  - ELKS exec replaces the process LDT with a freshly built descriptor set for
    the new 16-bit image.
- Exit/reap:
  - `proc_release_zombie_resources()` frees the process LDT before the process
    slot is recycled.
- SMP update rule:
  - LDT mutation changes the owning process state under `ldt_lock`.
  - The current CPU reloads `LDTR` immediately when mutating the current
    process.
  - Remote CPUs see the new `LDTR` state on the next context switch into that
    process; there is no remote in-place descriptor rewrite path.

## Permission Model

Current kernel policy:

- `modify_ldt(2)` operates only on the calling process.
- No cross-process LDT mutation ABI exists.
- Because the syscall is self-scoped, no `uid`, `euid`, or superuser gate is
  currently applied.

Forward-compatible capability model:

- If a future ABI allows mutating another task's LDT, that operation must be
  treated as privileged and mapped to `CAP_SYS_ADMIN`-style policy once the
  capability framework exists.
- The current self-scoped ABI should remain unprivileged.

## Descriptor Rejection Rules

The native `modify_ldt(2)` interface accepts only user code/data descriptors.
Hard rejection rules are:

- entry index must be `< LDT_ENTRIES`
- payload size must be exactly `sizeof(struct user_desc)` for `LDT_WRITE`
- `contents > 2` is rejected
- descriptors are always encoded as code/data (`S=1`); system descriptors are
  not representable through this ABI
- descriptors are always installed with `DPL=3`; kernel privilege levels are
  not representable through this ABI
- `LDT_READ_DEFAULT` exposes no separate default-table image and returns `0`

Design note:

- The kernel supports both 16-bit and 32-bit user segments through this ABI.
- Strict "16-bit only" validation is not the right contract for Substrate's
  native `modify_ldt(2)` path because Linux-compatible callers and user TLS
  style workloads need 32-bit descriptors too.

## Verification Matrix

- `tests/sys/test_ldt.c`
  - native kernel `LDT_READ`, `LDT_READ_DEFAULT`, `LDT_WRITE`
- `tests/sys/host_test_ldt_lifecycle.c`
  - clone, replace, free, invalid descriptor rejection, suspicious-descriptor
    logging
- `tests/sys/host_test_ldt_race.c`
  - concurrent read/write/alloc/clone races
- `tests/sys/host_test_ldt_codec.c`
  - descriptor encode/decode round-trip for base/limit semantics
- `tests/sys/host_test_ldt_fuzz.c`
  - randomized `sys_modify_ldt()` function/size/descriptor stress
- `tests/sys/host_test_i386_sched_ldt.c`
  - switch-time activation and no-reload fast path
- `tests/sys/test_linux_personality.c`
  - Linux syscall `123` wiring, name, and trace metadata
- ELKS host/runtime integration
  - the ELKS loader and signal/core tests exercise private per-process LDT
    selectors and 16-bit `CS/DS/SS` behavior through the ELKS personality path
