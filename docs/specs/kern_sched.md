# Kernel Scheduler Specification

## Overview
The Substrate scheduler implements a Round-Robin scheduling algorithm with support for preemptive multitasking. It manages `thread_t` units across processes.

## Preemption
- **Mechanism:** Driven by the Programmable Interval Timer (PIT) on i386.
- **Frequency:** 100 Hz (10ms time slice).
- **Interrupt:** IRQ0 (mapped to IDT vector 32).
- **Yielding:** The `isr_handler` calls `sched_yield()` upon receiving a timer interrupt, forcing a context switch to the next ready thread.

## Scheduling States
- `THREAD_READY`: Thread is in the run queue and can be scheduled.
- `THREAD_RUNNING`: Thread is currently executing on a CPU.
- `THREAD_BLOCKED`: Thread is waiting for an event (e.g., I/O, mutex, `sleep`).
- `THREAD_ZOMBIE`: Thread has terminated and is awaiting reaping.

## Sleep / Wakeup (Condition Variables)
- `sched_sleep(void *chan)`: Marks the current thread as `THREAD_BLOCKED` on the provided wait channel address and calls `sched_yield()`.
- `sched_wakeup(void *chan)`: Wakes up all threads sleeping on the specified channel, marking them as `THREAD_READY`.

## Scheduling Classes
- `SCHED_REALTIME`: Highest priority. Realtime threads always preempt lower class threads.
- `SCHED_TIMESHARE`: Normal user/kernel threads.
- `SCHED_IDLE`: Lowest priority. Only runs when no other threads are ready.

## Priorities
- Each thread has a `priority` and `base_priority` (0-255).
- Within a scheduling class, the thread with the highest `priority` value is selected.
- In case of a tie, the scheduler currently picks the first one encountered in the thread table (fixed-slot Round-Robin).

## Algorithm
1. The scheduler maintains a global list of threads.
2. `sched_yield()` scans all threads to find the one with the highest-ranking scheduling class.
3. If multiple threads exist in that class, it picks the one with the highest priority.
4. If the selected thread is different from the current thread, a context switch is performed.

## Context Switching
- Saved on the kernel stack: `EBP`, `EDI`, `ESI`, `EBX`.
- The stack pointer (`ESP`) is saved in the `thread_t` structure.
- Upon switching, `ESP` is loaded from the new `thread_t`, and registers are popped.
- The `iret` or `ret` instruction completes the transition.
