# Kernel Scheduler Specification

## Overview
The Substrate scheduler is an MLFQ/ULE-style design built around `thread_t` execution contexts, per-CPU runqueues, and SMP-aware load balancing.

## Scheduling Model
- **Scheduling classes:**
  - `SCHED_REALTIME`
  - `SCHED_TIMESHARE`
  - `SCHED_IDLE`
- **Priority representation:**
  - realtime queues occupy the highest class
  - timeshare queues carry dynamic priority influenced by CPU use and interactivity
  - idle threads only run when no higher-class thread is runnable
- **Runqueue structure:**
  - each CPU owns a `runqueue_t`
  - each runqueue contains multilevel queues plus bitmaps for O(1) queue selection

## Interactivity and Decay
- `sched_interactivity.c` tracks `run_time`, `sleep_time`, and an interactivity score.
- I/O-bound timeshare threads receive a priority boost and shorter timeslices.
- CPU-bound timeshare threads receive a decay penalty and longer batch-oriented timeslices.
- `sched_decay.c` periodically recalculates timeshare priorities and applies anti-starvation boosts.

## SMP Behavior
- **Per-CPU runqueues:** scheduling decisions are local by default.
- **Work stealing:** idle or lightly loaded CPUs steal runnable work from busier CPUs.
- **CPU affinity:** threads may float, carry an affinity mask, or be hard-bound to a CPU.
- **Remote preemption:** scheduler IPIs can request reschedule on other CPUs.

## Synchronization Integration
- **Turnstiles:** priority inheritance for contended locks.
- **Sleep queues:** hashed O(1) wait-channel lookup with per-bucket locking.

## Context Switching
- `arch_switch_to(prev, next)` handles the architecture-specific register/stack handoff.
- i386 context switches save and restore the callee-saved register set via `switch_to`.
- Address-space activation occurs when switching between processes with different pmaps.
- Lazy FPU switching uses CR0.TS and the `#NM` handler to restore FPU state on demand.
- PCB/thread metadata documents the thread/process separation boundary in `sys/kern/pcb.h`.

## Kernel Threads and Idle Context
- `swapper` (PID 0) is the always-valid kernel process context.
- each online CPU owns an idle thread bound to that CPU
- memory reclamation is handled by a dedicated `pagedaemon` kernel process rather than by executing pageout work directly inside the idle loop

## Process Bitness
- `process_t` carries a `bitness` field for 16/32/64-bit execution mode tracking.
- bitness is inherited across `fork()`
- binary loaders update bitness during `execve()` based on the loaded image format

## Verification
- host tests cover LAPIC/IPI/SMP scheduler support where possible
- in-kernel tests cover process bitness reporting
- QEMU SMP boots validate the live bring-up path for 2, 4, and 8 CPUs
