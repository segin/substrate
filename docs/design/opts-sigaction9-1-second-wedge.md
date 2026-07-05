# OPTS signals/sigaction/9-1 — the SECOND (full-suite-only) kernel wedge

Status: WIP diagnosis (agent a7b25348). Committed early so the fix target
is not lost. Reproduction + GDB confirmation in progress.

## Symptom
`signals/sigaction/9-1` wedges the kernel ONLY in the full-suite OPTS
baseline (after ~600 prior tests of accumulated kernel state), never
standalone. Serial goes DEAD-silent; even the guest 20 s per-test
watchdog SIGKILL cannot fire. That signature == a HARD lockup on the
single (TCG) CPU: a spin with **preemption disabled** so no other thread
(incl. the watchdog) can ever run, while timer IRQs still fire but cannot
cause an involuntary switch (preempt_count != 0).

The one such spin in the kernel is `proc_exit()`'s post-yield
`while(1)` (sys/pm/process.c:1794): it runs with `preempt_disable()`
(line 1721) held and is only safe because `sched_yield()` at line 1791 is
supposed to switch off-CPU FOREVER (the thread is THREAD_ZOMBIE and never
picked again). If that `sched_yield()` ever RETURNS, or if the scheduler
switches INTO a resurrected zombie whose context is another proc_exit
post-yield spin, the box wedges exactly as described.

## Already fixed (the FIRST path — the resurrect race)
- sys/pm/sched.c:418 backstop: never schedule a THREAD_READY thread whose
  `proc->state == SZOMB`; correct it back to THREAD_ZOMBIE.
- sys/kern/sleepq.c: every wake path guards `state != THREAD_STOPPED &&
  state != THREAD_ZOMBIE` before flipping a waiter to THREAD_READY.

## Root cause of the SECOND path — turnstiles
Kernel `mutex_t` uses **sleepq** (guarded). But BSD `lockmgr()`
(vnode / namecache / mount locks, sys/kern/lockmgr.c) uses **turnstiles**
for priority inheritance, and a lockmgr waiter is enqueued on BOTH:

    LK_SHARED / LK_EXCLUSIVE:  turnstile_block(lkp, holder);  sleepq_add(lkp, td);
    LK_RELEASE / LK_DOWNGRADE: turnstile_release(lkp);        sleepq_wake_all(lkp);

Two defects in `sys/kern/turnstile.c`:

1. **`turnstile_release()` (line 182) is MISSING the THREAD_ZOMBIE guard**
   that every sleepq wake path has — it flips a waiter to THREAD_READY if
   `state != THREAD_STOPPED` only. So it will RESURRECT a THREAD_ZOMBIE
   waiter. (candidate (a))

2. **`turnstile_block()` links the waiter via `current_thread->next`, the
   SAME field `sleepq_add()` uses.** They alias. And there is NO
   `turnstile_remove_thread()`, so `proc_exit()` — which removes an
   exiting thread from sleepqs (sys/pm/process.c:1619) — leaves it on the
   turnstile waiter list. A multithreaded process where thread A exits
   while thread B is parked in a lockmgr wait marks B THREAD_ZOMBIE +
   pulls it off the sleepq but LEAVES B on the turnstile. When the lock
   holder later releases, `turnstile_release()` walks the stale list and
   dereferences B:
     - if B is still THREAD_ZOMBIE -> resurrected to THREAD_READY (no
       guard). The sched backstop only catches `proc->state == SZOMB`,
       NOT the `SDYING` window (process.c:1626 marks siblings ZOMBIE while
       proc is still SDYING; SZOMB isn't set until line 1725).
     - if B was already reaped by wait4 -> its `thread_t` is FREED; the
       write `waiter->state = THREAD_READY` and the read `waiter->next`
       are a use-after-free into freed/reused kernel heap.

Either way a zombie/garbage thread ends up THREAD_READY and gets
scheduled; `arch_switch_to` returns into a stale/freed kernel stack or
into a proc_exit post-yield `while(1)` -> preempt-disabled hard wedge.

This needs the rare A-exits-while-B-in-lockmgr interleaving during
file-I/O-heavy pthread tests, so it only accumulates in the full suite —
matching "full-suite-only, after ~600 tests".

## Fix (planned)
Turnstile waking is fully REDUNDANT with the sleepq (every turnstile_block
site is immediately followed by sleepq_add; every turnstile_release site
by sleepq_wake_all). The turnstile's real job is priority inheritance, not
waking. So:
- `turnstile_release()` must NOT walk waiters or touch their state/next —
  leave waking to the guarded `sleepq_wake_all()`. This kills both the
  resurrect and the UAF read in one stroke, no thread_t change.
- Keep the THREAD_ZOMBIE guard parity note for defense in depth.

To be CONFIRMED by GDB against a live reproduction before finalizing.
