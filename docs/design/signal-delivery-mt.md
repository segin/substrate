# Signal Delivery for Multithreaded Processes

**Status:** decision locked in; implementation in progress.

## Background

POSIX (IEEE Std 1003.1-2017 §2.4.1) distinguishes two kinds of signals:

1. **Process-directed.** `kill(pid, sig)`, `tgkill(pid, -1, sig)`, signals
   from terminal drivers (`SIGINT`/`SIGQUIT`/`SIGTSTP`), `pgsignal` to
   any process group member, `SIGCHLD` to the parent of an exiting child.
   POSIX requires exactly one thread to handle the signal.

2. **Thread-directed.** `pthread_kill(thread, sig)`, `sys_thr_kill`,
   `tgkill(pid, tid, sig)`. POSIX requires the named thread to handle
   it (or hold it pending on that thread if it blocks the signal).

Synchronous trap signals from CPU exceptions (`SIGSEGV`/`SIGFPE`/
`SIGILL`/`SIGBUS`) are a special case: they go to the offending thread
unconditionally. Substrate's `trapsignal()` already does this
correctly.

## Substrate's previous (incorrect) behaviour

`psignal()` (in `sys/kern/signal.c`) walked every thread of the
target process and set the signal bit in every thread's
`sig_pending`. Whichever thread was scheduled first would clear *its*
bit and deliver; other threads kept the bit and would also deliver on
their next return-to-user. Result: a process-directed signal could
fire its handler N times in an N-threaded process. The "best thread"
selection that the code *does* perform was only used to pick which
thread to interruptibly wake — not to constrain delivery.

## Substrate's locked-in policy

| Source | Target |
| --- | --- |
| `kill(pid, sig)`, `pgsignal`, `tgkill(pid, -1, sig)`, TTY signals | One thread chosen by the kernel from threads that have the signal **unblocked**. If all threads block, hold pending at the process level until any thread unblocks. |
| `pthread_kill`, `sys_thr_kill`, `tgkill(pid, tid, sig)` | The named thread, regardless of its mask. Stays pending on that thread if blocked. |
| Synchronous trap (`SIGSEGV`/`SIGFPE`/`SIGILL`/`SIGBUS`) | The offending thread (already correct via `trapsignal()`). |
| `SIGKILL`, `SIGSTOP`, `SIGCONT` | Whole process. Cannot be blocked, caught, or ignored. |

### Thread-selection priority for process-directed signals

When the kernel must pick one thread, it scores candidates that have
the signal unblocked, picks the highest:

| Score | State |
| --- | --- |
| 3 | RUNNING or READY |
| 2 | BLOCKED + INTERRUPTIBLE |
| 1 | other (e.g. uninterruptible) |
| 0 | signal is blocked — not a candidate |

The "best thread" selection in the previous code already did this; the
fix is that it now constrains *delivery*, not just *wakeup*.

### Process-level pending bitmap

`process_t` gains a `sig_pending` bitmap. `psignal()` sets a bit in
that map iff every thread blocks the signal. The map is consulted
on every `sigprocmask` / `pthread_sigmask` that unblocks signals: the
unblocking thread migrates any newly-deliverable bit from
`process->sig_pending` to its own `thread->sig_pending` (atomic
clear-on-process, set-on-thread, so the bit is delivered exactly
once).

A thread that exits (`thr_exit`) does **not** consume the process
bitmap — its signals were never assigned to it.

### Interaction with the existing thread iteration

`psignal()` currently iterates threads twice in a single pass:
- For SIGCONT: clear stop-related pending bits and wake stopped
  threads.
- For everything else: compute the "best thread" score and wake any
  blocked thread that has the signal unblocked.

After the fix, the iteration also tracks "did *any* thread have the
signal unblocked?". If yes → set the bit on the best thread, wake the
best thread if blocked. If no → set the bit in `process->sig_pending`
instead.

## Implementation plan

1. ✅ Document the decision (this file). [Locked in.]
2. Add `uint32_t sig_pending` to `process_t` in `sys/include/sys/proc.h`.
3. Rewrite the "set pending on all threads" loop in `psignal()`
   (`sys/kern/signal.c`) to set on exactly one thread, or on
   `process->sig_pending` if all block.
4. In `sigprocmask` (`sys/kern/signal.c`), after applying the new
   mask, AND `process->sig_pending` with `~new_mask` (atomic) and OR
   the cleared bits into the current thread's `sig_pending`.
5. SIGKILL/SIGSTOP/SIGCONT keep their "applies to all threads"
   path (cannot be blocked anyway).
6. Update `psignal` man page (or add one) to describe the policy.

Step 2-3 land first as the smallest correctness fix; steps 4-6
follow.

## Why not Linux-style siginfo queueing

Linux delivers process-directed signals via per-process `sigqueue` and
per-thread `sigqueue` lists, and waitable signals carry siginfo_t
payloads. Substrate stores at most one bit per signal (no per-signal
siginfo). This matches FreeBSD's classical sigaction model and keeps
the kernel simple. The price is no `SI_QUEUE` / realtime-signal
preserving payloads; trap signals already carry siginfo via the
thread's `trap_signo`/`trap_code`/`trap_addr` fields, which is enough
for SA_SIGINFO.
