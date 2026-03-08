# Kernel Signal Specification

## Overview

Substrate implements process-directed and trap-directed signals with its own
native contract, currently BSD-shaped for job control, default-action
behavior, and synchronous fault delivery, with i386 signal-frame delivery.

Linux personality support is layered on top of this kernel contract. The
personality is responsible for exposing Linux signal numbers, frame layouts,
and sigreturn ABI entry points without redefining the native Substrate signal
model.

## Native ABI Contract

The native Substrate ABI is the kernel's source-of-truth contract for signal
semantics.

That native contract currently means:

- BSD-shaped default actions and job-control behavior
- native signal-numbering and `sigprop[]` policy
- native `si_code` meanings for synchronous faults
- native handler-mask and stop/continue semantics in the process core

Compatibility personalities do not replace that contract. They translate at
the ABI edge:

- Linux personality remaps signal numbers, sigsets, frame layouts, and
  optional `sa_restorer` callbacks
- FreeBSD personality may use its own user-visible frame ABI while still
  relying on the native kernel stop/continue/exit policy

The signal path is split into four phases:

1. Generation
2. Pending-state update
3. Delivery point selection
4. User handler or default-action execution

## Generation Paths

The kernel currently generates signals from these sources:

- `sys_kill()` for PID, process-group, and broadcast delivery
- `psignal()` for process-directed kernel delivery
- `pgsignal()` / `pgrp_signal()` for process-group delivery
- `trapsignal()` for synchronous CPU exceptions
- TTY line discipline for `SIGINT`, `SIGQUIT`, `SIGTSTP`, `SIGTTIN`, `SIGTTOU`,
  `SIGWINCH`, and `SIGHUP`
- process exit / stop / continue notifications for `SIGCHLD`

## Delivery Model

### Process-Directed Signals

`psignal()` applies process-directed signals to every thread in the target
thread group by setting the pending bit on each thread.

It also performs signal-specific policy:

- PID 1 protection for default `SIGKILL`, `SIGTERM`, and `SIGSTOP`
- `SIGCONT` clears pending stop signals, wakes stopped threads, transitions the
  process back to `SRUN`, and sets `P_CONTINUED`
- stop signals clear pending `SIGCONT`

For immediate delivery preference, `psignal()` ranks candidate threads as:

1. running or ready thread with the signal unmasked
2. interruptible blocked thread with the signal unmasked
3. any other thread with the signal unmasked
4. fallback thread

Only interruptible sleepers are woken for asynchronous signal delivery.

### Trap Signals

`trapsignal()` is synchronous. It targets the current faulting thread, stores
`trap_signo`, `trap_code`, and fault address metadata on that thread, and marks
the signal pending on that thread only. If the current thread does not match
the target process, the kernel falls back to `psignal()`.

Native i386 exception mapping currently includes:

- divide-by-zero -> `SIGFPE` / `FPE_INTDIV`
- breakpoint -> `SIGTRAP` / `TRAP_BRKPT`
- single-step debug trap -> `SIGTRAP` / `TRAP_TRACE`
- invalid opcode -> `SIGILL` / `ILL_ILLOPC`
- general-protection fault -> `SIGILL` / `ILL_PRVOPC`
- page fault -> `SIGSEGV` / `SEGV_MAPERR` or `SEGV_ACCERR`

For page faults, `siginfo_t.si_addr` carries the faulting virtual address. For
invalid-opcode and protection-fault cases, `si_addr` carries the faulting
instruction pointer.

## Checking Points

The kernel checks pending signals when returning toward user mode:

- syscall exit path in `sys/arch/i386/syscall.c`
- interrupt / trap exit paths in `sys/arch/i386/idt.c`

`signal_handle_pending()` does not deliver signals while returning to kernel
mode. It only acts on a user-mode return frame.

## Default Actions

Default-action handling in `signal_handle_pending()` currently covers:

- terminate: `SIGINT`, `SIGTERM`, `SIGSEGV`, `SIGILL`, `SIGFPE`, `SIGKILL`
- stop: `SIGSTOP`, `SIGTSTP`, `SIGTTIN`, `SIGTTOU`
- ignore-by-default paths such as `SIGCHLD` and non-stop `SIGCONT`

Job-control stop signals are ignored for orphaned process groups.

## SA_RESTART Behavior

When a thread is returning from a syscall and:

- `current_thread->in_syscall` is set
- the interrupted syscall return register contains `-EINTR`
- the installed handler carries `SA_RESTART`

the kernel restores the original syscall number/return register state and
rewinds `EIP` by the `int 0x80` instruction width so the syscall is retried on
return to user mode.

## SIGCHLD Semantics

The kernel currently generates `SIGCHLD` for:

- child exit
- child stop unless `SA_NOCLDSTOP`
- child continue unless `SA_NOCLDSTOP`

`wait4()` consumes zombie, stopped, and continued state using the child
process state plus `P_WAITED` / `P_CONTINUED`.

## Related i386 Frame Layout

The i386 frame layout, trampoline contract, and sigreturn restore rules are
documented in [arch_i386_signal.md](./arch_i386_signal.md).

## Personality Boundary

Native Substrate signal behavior is the kernel's source of truth:

- BSD-shaped default action and job-control semantics
- native `sigprop[]` policy
- native `si_code` namespace for synchronous traps

Compatibility personalities adapt that internal contract at the ABI edge:

- Linux personality remaps signal numbers and builds Linux `sigframe` /
  `rt_sigframe` layouts
- Linux personality also translates `rt_sigaction(2)` / `rt_sigprocmask(2)`
  signal numbers and sigsets at syscall entry, and preserves Linux
  `sa_restorer` pointers separately from the native kernel disposition state
- FreeBSD personality uses its own legacy frame ABI while preserving native
  kernel stop/continue/exit policy
