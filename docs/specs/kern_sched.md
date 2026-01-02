# Kernel Scheduler Specification

## Overview
The TestUnix scheduler implements a Round-Robin scheduling algorithm with support for preemptive multitasking. It manages `thread_t` units across processes.

## Preemption
- **Mechanism:** Driven by the Programmable Interval Timer (PIT) on i386.
- **Frequency:** 100 Hz (10ms time slice).
- **Interrupt:** IRQ0 (mapped to IDT vector 32).
- **Yielding:** The `isr_handler` calls `sched_yield()` upon receiving a timer interrupt, forcing a context switch to the next ready thread.

## Scheduling States
- `THREAD_READY`: Thread is in the run queue and can be scheduled.
- `THREAD_RUNNING`: Thread is currently executing on a CPU.
- `THREAD_BLOCKED`: Thread is waiting for an event (e.g., I/O, mutex).
- `THREAD_ZOMBIE`: Thread has terminated and is awaiting reaping.

## Algorithm
1. The scheduler maintains a global list of threads.
2. `sched_yield()` iterates through the thread list starting from the current thread.
3. It selects the first thread in the `THREAD_READY` state.
4. It saves the context of the current thread and restores the context of the selected thread.
5. If no other `THREAD_READY` thread is found, it continues executing the current thread.

## Context Switching
- Saved on the kernel stack: `EBP`, `EDI`, `ESI`, `EBX`.
- The stack pointer (`ESP`) is saved in the `thread_t` structure.
- Upon switching, `ESP` is loaded from the new `thread_t`, and registers are popped.
- The `iret` or `ret` instruction completes the transition.
