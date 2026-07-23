#include <kern/sched.h>
#include <kern/sleepq.h>
#include <kern/time.h>
#include <sys/errno.h>
#include <sys/futex.h>
#include <pm/pm.h>
#include <arch/i386/pmm.h>
#include <arch/i386/pmap.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <sys/types.h>
#ifndef HOST_TEST
#include <vm/vm_kmem.h>
#else
#include <stdlib.h>
#endif

/* current_thread is per-CPU: a macro over curthread_slot() (arch percpu.c). */

/* CFS-style minimum vruntime across ready SCHED_TIMESHARE threads, republished
 * by the pick (declared in sched.h).  Waking and newly-created timeshare
 * threads rebase to it so none starts with an unfair CPU credit. */
uint64_t sched_min_vruntime = 0;

/* Weighted fair-share (CFS) weights, indexed by nice -20..+19 as the array
 * position (40 - base_priority).  Standard geometric ~1.25x-per-level table:
 * each nice step changes CPU share ~10%, nice 0 outweighs nice +19 ~68x, and
 * nice -20 outweighs nice 0 ~87x. */
#define NICE_0_WEIGHT   1024   /* == sched_prio_to_weight[nice 0] */
#define VRUNTIME_SHIFT  20     /* fixed-point scale for the per-tick delta */
static const uint32_t sched_prio_to_weight[40] = {
    /* -20 */ 88761, 71755, 56483, 46273, 36291,
    /* -15 */ 29154, 23254, 18705, 14949, 11916,
    /* -10 */  9548,  7620,  6100,  4904,  3906,
    /*  -5 */  3121,  2501,  1991,  1586,  1277,
    /*   0 */  1024,   820,   655,   526,   423,
    /*   5 */   335,   272,   215,   172,   137,
    /*  10 */   110,    87,    70,    56,    45,
    /*  15 */    36,    29,    23,    18,    15,
};

/* Advance a running timeshare thread's weighted virtual runtime.  Called once
 * per timer tick for the current thread from the IRQ path.  A niced-down thread
 * (small weight) accrues vruntime fast and is picked less; a niced-up thread
 * accrues slowly and runs more, so CPU time ends up proportional to weight
 * without starving anyone. */
void sched_vruntime_tick(thread_t *t) {
    if (!t || t->sched_class != SCHED_TIMESHARE) return;
    int idx = 40 - t->base_priority;
    if (idx < 0) idx = 0;
    if (idx > 39) idx = 39;
    uint32_t w = sched_prio_to_weight[idx];   /* always >= 15, never 0 */
    t->vruntime += ((uint64_t)NICE_0_WEIGHT << VRUNTIME_SHIFT) / w;
}

#define TID_HASH_SIZE 64
#define TID_HASH(tid) ((unsigned)(tid) & (TID_HASH_SIZE - 1))

static thread_t *allthread = NULL;
static thread_t *tid_hash[TID_HASH_SIZE];
static int next_tid = 1;
static spinlock_t tid_lock;

/*
 * Round-robin cursor for sched_yield(): the thread picked last time, so equal-
 * priority threads get fair turns.  It is file-scope (not a sched_yield local)
 * precisely so sched_unlink_locked() can clear it the instant the thread it
 * points at is unlinked/reaped — otherwise the cursor dangles at freed storage
 * and the next pick dereferences rr_last->t_allthread_next as a use-after-free
 * (see sched_unlink_locked and sched_yield).
 */
static thread_t *rr_last = NULL;


#include <kern/arch.h>
#include <arch/i386/percpu.h>
#include <arch/i386/intr.h>
#include <sys/preempt.h>
#include <sys/smp.h>

#ifdef HOST_TEST
static thread_t *sched_storage_alloc(void) {
    thread_t *t = malloc(sizeof(*t));
    if (t) memset(t, 0, sizeof(*t));
    return t;
}
static void sched_storage_free(thread_t *t) { free(t); }
#else
static thread_t *sched_storage_alloc(void) {
    thread_t *t = kmalloc(sizeof(*t));
    if (t) memset(t, 0, sizeof(*t));
    return t;
}
static void sched_storage_free(thread_t *t) {
    if (t) kfree(t, sizeof(*t));
}
#endif

thread_t *thread_first(void) { return allthread; }
thread_t *thread_next(thread_t *t) { return t ? t->t_allthread_next : NULL; }

/*
 * KERN-06: the thread registry (allthread + tid_hash) is walked by signal
 * delivery (psignal_info) and the scheduler tick from HARD-IRQ context while
 * other threads are concurrently reaped -- sched_reap_thread() unlinks and
 * frees a thread_t under tid_lock, so an unlocked walker can dereference freed
 * storage (UAF).  Expose the registry lock so those walkers can hold it across
 * a FOREACH_THREAD.  It is IRQ-safe (masks local interrupts) because every
 * tid_lock acquisition is now IRQ-safe: a walker interrupting a tid_lock holder
 * would otherwise spin forever on it (the interrupted holder cannot release).
 * Lock order: tid_lock is acquired BEFORE any sleepq bucket lock (sq_lock) --
 * a walker's body may take sq_lock (signal_interrupt_thread), never the reverse
 * -- and BEFORE nothing that re-enters tid_lock, so no self-deadlock.
 */
unsigned long thread_registry_lock(void) {
    return spinlock_acquire_irq(&tid_lock);
}
void thread_registry_unlock(unsigned long flags) {
    spinlock_release_irq(&tid_lock, flags);
}

static void sched_link_locked(thread_t *t) {
    t->t_allthread_next = allthread;
    allthread = t;

    unsigned bucket = TID_HASH(t->tid);
    t->t_tidhash_next = tid_hash[bucket];
    tid_hash[bucket] = t;
}

static void sched_unlink_locked(thread_t *t) {
    thread_t **link;

    /* Drop the round-robin cursor if it points at the thread being unlinked.
     * sched_reap_thread() frees t's storage immediately after this call, so a
     * lingering rr_last would dangle: the next sched_yield() reads
     * rr_last->t_allthread_next off freed memory.  While the freed thread_t is
     * still pristine that read yields the NULL we store below (harmless — the
     * pick falls back to the list head), but once the slab is recycled for
     * another thread_t the field is arbitrary and the pick walks a wild list —
     * a cumulative, churn-triggered use-after-free that triple-faults the box. */
    if (rr_last == t)
        rr_last = NULL;

    for (link = &allthread; *link; link = &(*link)->t_allthread_next) {
        if (*link == t) {
            *link = t->t_allthread_next;
            break;
        }
    }
    t->t_allthread_next = NULL;

    unsigned bucket = TID_HASH(t->tid);
    for (link = &tid_hash[bucket]; *link; link = &(*link)->t_tidhash_next) {
        if (*link == t) {
            *link = t->t_tidhash_next;
            break;
        }
    }
    t->t_tidhash_next = NULL;
}

static thread_t *sched_lookup_tid_locked(int tid) {
    for (thread_t *t = tid_hash[TID_HASH(tid)]; t; t = t->t_tidhash_next) {
        if (t->tid == tid) {
            return t;
        }
    }
    return NULL;
}

static int sched_alloc_tid_locked(process_t *proc) {
    int start;
    int candidate;

    if (proc && proc->pid == 0 && !sched_lookup_tid_locked(0)) {
        return 0;
    }

    start = next_tid;
    if (start < 1 || start > SUBSTRATE_TID_MAX) {
        start = 1;
    }

    candidate = start;
    do {
        if (!sched_lookup_tid_locked(candidate)) {
            next_tid = (candidate == SUBSTRATE_TID_MAX) ? 1 : candidate + 1;
            return candidate;
        }

        candidate++;
        if (candidate > SUBSTRATE_TID_MAX) {
            candidate = 1;
        }
    } while (candidate != start);

    return -1;
}

static void sched_release_thread_storage(thread_t *t) {

    if (!t || !t->kstack_owned || t->kstack_base == 0) {
        return;
    }

    switch ((thread_kstack_type_t)t->kstack_type) {
    case THREAD_KSTACK_PMM_BLOCK:
        pmm_free_block((void *)(uintptr_t)t->kstack_base);
        break;
    case THREAD_KSTACK_PMM_CONTIG:
        pmm_free_contiguous((void *)(uintptr_t)t->kstack_base, t->kstack_units);
        break;
    case THREAD_KSTACK_KMALLOC:
        kfree((void *)(uintptr_t)t->kstack_base, t->kstack_units);
        break;
    case THREAD_KSTACK_NONE:
    default:
        break;
    }

    t->kstack_base = 0;
    t->kstack_units = 0;
    t->kstack_type = THREAD_KSTACK_NONE;
    t->kstack_owned = 0;
}



void sched_init_generic(void) {
    next_tid = 1;
    allthread = NULL;
    memset(tid_hash, 0, sizeof(tid_hash));
    spinlock_init(&tid_lock, "tid");

    pm_init();
}

// Just sets up structures, doesn't touch hardware/stack
thread_t *sched_alloc_thread(process_t *proc) {
    thread_t *thread;
    int tid;

    thread = sched_storage_alloc();
    if (!thread) {
        return NULL;
    }

    /*
     * Initialize ALL fields before linking into allthread.  The
     * default kmalloc-zeroed state would be THREAD_READY (=0), which
     * a concurrent sched_yield from a timer IRQ would happily pick
     * — switching into a thread with no proc, no kstack, no
     * anything.  The kernel-panic at eip=0xf0 with esp pointing at
     * a user stack was exactly this: a half-constructed thread
     * raced into context-switching.
     */
    thread->proc = proc;
    thread->state = THREAD_BLOCKED; /* until caller sets up stack and flips to READY */
    thread->wait_chan = NULL;
    thread->wait_reason = NULL;
    thread->sleep_expiry = 0;
    thread->priority = current_thread ? current_thread->priority : 20;
    thread->base_priority = current_thread ? current_thread->base_priority : 20;
    /* Start a new timeshare thread at the current minimum vruntime so it is
     * scheduled fairly against existing threads -- neither an unfair head start
     * from a zero vruntime nor starvation from a stale-high one. */
    thread->vruntime = sched_min_vruntime;
    thread->sched_class = current_thread ? current_thread->sched_class : SCHED_TIMESHARE;
    thread->kstack_base = 0;
    thread->kstack_units = 0;
    thread->kstack_type = THREAD_KSTACK_NONE;
    thread->kstack_owned = 0;
    thread->bound_cpu = -1;
    thread->exec_saved_bound_cpu = -1;
    thread->exec_pin_active = 0;
    thread->exec_saved_no_preempt = 0;

    thread->sig_mask = current_thread ? current_thread->sig_mask : 0;
    thread->sig_pending = 0;
    thread->sig_mask_suspend = 0;
    thread->sig_mask_suspend_active = 0;
    thread->sig_on_stack = 0;
    memset(&thread->sig_alt_stack, 0, sizeof(thread->sig_alt_stack));
    thread->in_syscall = 0;
    /* Per-thread CPU-time accounting (CLOCK_THREAD_CPUTIME_ID) starts at 0:
     * a new thread's CPU clock must read ~0 immediately after creation
     * (pthread_create/11-1). */
    thread->cpu_utime = 0;
    thread->cpu_stime = 0;

    unsigned long tf = spinlock_acquire_irq(&tid_lock);
    tid = sched_alloc_tid_locked(proc);
    if (tid < 0) {
        spinlock_release_irq(&tid_lock, tf);
        sched_storage_free(thread);
        return NULL;
    }
    thread->tid = tid;
    sched_link_locked(thread);
    spinlock_release_irq(&tid_lock, tf);

    return thread;
}

/*
 * Kernel-preemption nesting counter, carried per-thread.  Every spinlock
 * acquire raises it and every release lowers it.  preempt_disable() must
 * run BEFORE a spinlock's atomic acquire so there is never a window where
 * the lock is held but the count is still 0.  A timer interrupt that lands
 * in kernel mode performs an involuntary context switch only when this is
 * 0 -- i.e. the interrupted context holds no spinlock and is safe to leave.
 */
void preempt_disable(void) {
    if (current_thread)
        current_thread->preempt_count++;
}

void preempt_enable_noresched(void) {
    if (current_thread && current_thread->preempt_count > 0)
        current_thread->preempt_count--;
}

uint32_t preempt_count_get(void) {
    return current_thread ? current_thread->preempt_count : 0;
}

static void sched_context_switch(thread_t *prev, thread_t *next) {
    if (prev && prev->state == THREAD_RUNNING) prev->state = THREAD_READY;

    // Handle thread exit notification after switching out but before changing address space
    if (prev && prev->state == THREAD_ZOMBIE && prev->exit_tid_ptr) {
        futex_wake_exited_thread(prev->exit_tid_ptr);
        prev->exit_tid_ptr = NULL;
    }

    next->state = THREAD_RUNNING;
    /* Maintain the BSD process-level state for ps(1)/procfs: a process with a
     * running thread is SRUN.  Preserve stop/zombie/dying (set explicitly
     * elsewhere) so scheduling never clobbers them. */
    if (next->proc) {
        uint8_t pst = next->proc->state;
        if (pst != SSTOP && pst != SZOMB && pst != SDYING)
            next->proc->state = SRUN;
    }

    THIS_CPU()->current = next;
    current_thread = next;
    current_process = current_thread->proc;

    // Arch specific hooks
    if (current_thread->kstack_top) {
        arch_set_kernel_stack(current_thread->kstack_top);
    }

    // Activate new process's address space if switching processes
    if (prev && prev->proc != next->proc && next->proc && next->proc->pmap) {
        pmap_activate(next->proc->pmap);
    }

    if (prev && prev != next) {
        arch_switch_to(prev, next);
    }
}
void sched_yield(void) {
    if (!current_thread) return;

    proc_reap_autoreap_zombies();
    sched_reap_detached_zombies();

    /* Disable interrupts across thread selection and the context switch so
     * a timer tick cannot re-enter sched_yield (kernel preemption) and
     * corrupt the round-robin cursor or a half-finished switch.  A newly
     * created thread first runs with interrupts disabled here and re-enables
     * them itself -- exactly as it already does when first scheduled from
     * the timer-IRQ preemption path, so this changes nothing for them. */
    uint32_t _pflags = intr_disable();

    int cpu_id = percpu_get_cpu_id();

    thread_t *best_thread = NULL;
    int highest_prio = -1;
    sched_class_t best_class = SCHED_IDLE;

    /*
     * Round-robin cursor (file-scope rr_last): the thread we picked last time.
     * We start scanning at its successor (wrapping past tail back to head) so
     * equal-priority threads get fair turns regardless of which order they were
     * linked in.  sched_unlink_locked() nulls rr_last when its target is
     * reaped, so it can never dangle at freed storage here.
     */

    if (!allthread) {
        intr_restore(_pflags);
        return;
    }

rescan:
    best_thread = NULL;
    highest_prio = -1;
    best_class = SCHED_IDLE;

    /* If rr_last has been unlinked, fall back to the head. */
    thread_t *start = NULL;
    if (rr_last && rr_last->t_allthread_next) {
        start = rr_last->t_allthread_next;
    } else if (rr_last) {
        start = allthread;
    } else {
        start = allthread;
    }

    thread_t *t = start;
    bool wrapped = false;
    while (t) {
        thread_t *candidate = t;
        do {
            if (candidate->state != THREAD_READY) break;
            /*
             * Never schedule a thread whose process has already exited.  A
             * thread is set THREAD_ZOMBIE by proc_exit() and then voluntarily
             * gives up the CPU, parked mid-proc_exit().  A stale wake that
             * races that teardown can flip it back to THREAD_READY (the
             * sleepq/turnstile "don't resurrect a STOPPED/ZOMBIE waiter"
             * class): the thread's process is SZOMB but its thread state is
             * READY, an impossible-in-steady-state combination.  Switching
             * into such a resurrected zombie makes switch_to() return into
             * proc_exit()'s post-yield while(1) -- a preempt-disabled spin
             * that wedges the CPU -- or, once wait4() has reaped and freed its
             * kernel stack, into a freed/reused stack, faulting in the kernel
             * at a garbage EIP (observed EIP=0x282 / a thread_t address under
             * OPTS signals/sigaction/9-1's job-control SIGSTOP/SIGCONT churn).
             * Correct the erroneous resurrection back to ZOMBIE -- so wait4()
             * still observes it as reapable -- and skip it.
             */
            if (candidate->proc && candidate->proc->state == SZOMB) {
                candidate->state = THREAD_ZOMBIE;
                break;
            }
            if (!sched_can_run_on_cpu(candidate, cpu_id)) break;

            /* Rebase a timeshare thread that has fallen behind the minimum
             * (e.g. just woke from a sleep with a stale-low vruntime) up to the
             * current minimum, so it rejoins fairly instead of monopolising the
             * core until its virtual clock catches up.  Doing it here in the
             * pick covers every wake path in one spot; the scan runs with
             * interrupts disabled, so mutating vruntime is safe. */
            if (candidate->sched_class == SCHED_TIMESHARE &&
                candidate->vruntime < sched_min_vruntime) {
                candidate->vruntime = sched_min_vruntime;
            }

            bool better = false;
            if (!best_thread) {
                better = true;
            } else if (candidate->sched_class < best_class) {
                better = true;
            } else if (candidate->sched_class == best_class) {
                if (candidate->sched_class == SCHED_TIMESHARE) {
                    /* Weighted fair-share: pick the lowest vruntime.  A tie
                     * keeps the earlier-scanned thread, so the rr_last cursor
                     * still round-robins equal-vruntime threads. */
                    if (candidate->vruntime < best_thread->vruntime) {
                        better = true;
                    }
                } else if (candidate->priority > highest_prio) {
                    /* Real-time / idle: strict highest priority. */
                    better = true;
                }
            }

            if (better) {
                best_thread = candidate;
                highest_prio = candidate->priority;
                best_class = candidate->sched_class;
            }
        } while (0);

        t = t->t_allthread_next;
        if (!t && !wrapped) {
            t = allthread;
            wrapped = true;
        }
        if (t == start) break;
    }

    /* Publish the minimum vruntime (the picked timeshare thread is the lowest,
     * i.e. the CFS leftmost) so waking/new timeshare threads can rebase to it
     * and neither starve nor monopolise the core. */
    if (best_thread && best_thread->sched_class == SCHED_TIMESHARE) {
        sched_min_vruntime = best_thread->vruntime;
    }

    if (best_thread == current_thread) {
        /* We are the most-eligible runnable thread (RUNNING, or woken to
         * READY by a wake that raced our own block) — just keep running. */
        current_thread->state = THREAD_RUNNING;
        intr_restore(_pflags);
        return;
    }

    if (best_thread) {
        rr_last = best_thread;
        sched_context_switch(current_thread, best_thread);
        intr_restore(_pflags);
        return;
    }

    /* Nothing else is runnable.  If the current thread is itself still
     * runnable, keep running it. */
    if (current_thread->state == THREAD_RUNNING ||
        current_thread->state == THREAD_READY) {
        current_thread->state = THREAD_RUNNING;
        intr_restore(_pflags);
        return;
    }

    /*
     * current_thread is BLOCKED (or a zombie) and no other thread is
     * runnable.  NEVER fall through and return into it: a blocked thread
     * that keeps executing re-enters its sleep path and double-registers
     * itself on the sleepq, corrupting the queue -- and with preemption
     * disabled inside a sleepq spinlock that spin wedges the whole box.
     * Instead idle until an interrupt (a timer tick, a device IRQ, or a
     * sched_tick deadline wake) makes some thread runnable, then rescan.
     *
     * Only reachable from a voluntary block (sched_sleep / sleepq), never
     * from the timer-IRQ preemption path (which always calls sched_yield
     * with current RUNNING and is handled above), so re-enabling interrupts
     * and halting here is safe.
     */
    intr_restore(_pflags);
    __asm__ volatile("sti; hlt");
    _pflags = intr_disable();
    goto rescan;
}

void sched_switch(thread_t *next) {
    if (!next) return;
    if (!current_thread) return;
    if (next == current_thread && current_thread->state == THREAD_RUNNING) return;
    
    sched_context_switch(current_thread, next);
}

int sched_get_current_tid(void) {
    if (current_thread) return current_thread->tid;
    return -1;
}

thread_t *sched_get_thread(int tid) {
    if (tid < 0) return NULL;

    unsigned long tf = spinlock_acquire_irq(&tid_lock);
    thread_t *t = sched_lookup_tid_locked(tid);
    spinlock_release_irq(&tid_lock, tf);
    return t;
}

void sched_set_priority(int tid, sched_class_t cls, int prio) {
    thread_t *t = sched_get_thread(tid);
    if (!t) return;
    t->sched_class = cls;
    t->priority = prio;
    t->base_priority = prio;
}

void sched_sleep(void *chan) {
    if (!current_thread) return;

    // Check for pending signals before sleeping if interruptible
    if ((current_thread->flags & THREAD_F_INTERRUPTIBLE) &&
        (current_thread->sig_pending & ~current_thread->sig_mask)) {
        return;
    }

    /*
     * Lost-wakeup safety net.
     *
     * The universal sleep idiom is `while (!cond) sched_sleep(chan);`: the
     * caller tests `cond`, then calls here to register on `chan` and block.
     * sched_wakeup() only wakes threads already BLOCKED on `chan`, so a
     * wakeup that fires in the window between the caller's test and the
     * `state = THREAD_BLOCKED` below targets a thread not yet on the
     * channel and is lost — the sleeper then blocks forever.  Kernel
     * preemption (a timer tick that switches to the waker mid-window, or a
     * device IRQ that wakes from the window) makes this readily reachable
     * under load and is the cause of the intermittent pipe / waitpid / pty
     * freezes.
     *
     * Closing the window correctly requires every caller to register intent
     * before testing (prepare-to-wait); there are 20+ call sites.  Instead,
     * arm a generous fallback deadline so a lost wakeup self-heals on the
     * next sched_tick rather than hanging.  The interval is long enough
     * (~250 ms) that the genuine wakeup virtually always fires first, so
     * this only triggers on an actual loss — safe even for the handful of
     * single-shot sleepers (e.g. vfork) where a premature wake would be
     * wrong.  Callers that set their own (shorter) deadline via
     * sched_sleep_until() are left untouched.
     */
    int armed_fallback = 0;
    if (current_thread->sleep_expiry == 0) {
        uint32_t hz = get_hz();
        uint64_t span = hz ? (hz / 4u) : 32u;   /* ~250 ms */
        if (span == 0) span = 1;
        current_thread->sleep_expiry = get_ticks() + span;
        armed_fallback = 1;
    }

    current_thread->wait_chan = chan;
    current_thread->state = THREAD_BLOCKED;
    /* Reflect the block in the process-level state for ps(1)/procfs (this is
     * the timed-wait path used by nanosleep/sleep, distinct from sleepq). */
    if (current_thread->proc) {
        uint8_t pst = current_thread->proc->state;
        if (pst != SSTOP && pst != SZOMB && pst != SDYING)
            current_thread->proc->state = SSLEEP;
    }
    sched_yield();

    /* Clear our fallback so it can't leak into the next, unrelated sleep
     * (sched_wakeup does not touch sleep_expiry, only sched_tick does). */
    if (armed_fallback) {
        current_thread->sleep_expiry = 0;
    }
}

int sched_sleep_until(void *chan, uint64_t deadline_tick) {
    if (!current_thread) return 0;

    current_thread->sleep_expiry = deadline_tick;
    current_thread->sleep_status = 0;

    sched_sleep(chan);

    current_thread->sleep_expiry = 0;
    return current_thread->sleep_status;
}

void sched_tick(void) {
    uint64_t now = get_ticks();

    // Perform periodic SMP load balancing
    sched_periodic_balance();

    /* KERN-06: sched_tick runs in the timer IRQ; hold the registry lock so a
     * thread being reaped concurrently can't be freed under this walk. */
    unsigned long rf = thread_registry_lock();
    FOREACH_THREAD(thread) {
        if (thread->state == THREAD_BLOCKED &&
            thread->sleep_expiry > 0 &&
            now >= thread->sleep_expiry) {

            thread->state = THREAD_READY;
            thread->sleep_status = -ETIMEDOUT;
            thread->sleep_expiry = 0;
            /*
             * Deliberately leave wait_chan set.  sched_tick runs in the
             * timer IRQ and must not take the sleepq bucket lock (it is a
             * plain spinlock; grabbing it here could deadlock against an
             * interrupted holder), so it cannot dequeue a sleepq sleeper.
             * A sleepq waiter that armed a fallback deadline (e.g. pipe_wait)
             * therefore removes ITSELF from the queue on wake, using
             * wait_chan to find its bucket -- so we must preserve it here.
             * For plain sched_sleep sleepers wait_chan is harmless once
             * READY (wakeups are gated on THREAD_BLOCKED) and is overwritten
             * by their next sleep.
             */
        }
    }
    thread_registry_unlock(rf);
}

/*
 * Dedicated wake channel for select()/poll() blockers.
 *
 * kern_poll() previously used (void *)current_thread as its sleep
 * channel and slept for fixed ~10 ms intervals — a busy-poll, since
 * neither sched_wakeup() nor sleepq_wake_all() target a per-thread
 * private channel.  At idle, a typical X session (matwm2 + xterms
 * + Xnest forwarding) burned measurable CPU just spinning around
 * the 10 ms timer.
 *
 * Now every kern_poll sleeper uses &g_poll_wake_chan as its wait
 * channel, and any wake path that could plausibly affect a polled
 * fd's readiness (sched_wakeup for pipe/tty/socket/input, plus
 * sleepq_wake_all for AF_UNIX rx/tx and accept) calls
 * sched_poll_wake_pollers() to kick them.  The kern_poll sleeper
 * wakes, re-polls every fd, and either returns ready or goes back
 * to sleep — no more 10 ms timer spin.
 */
char g_poll_wake_chan;
volatile uint64_t g_poll_wake_seq = 0;

/*
 * Per-channel poll registry.
 *
 * A poller in kern_poll() registers one entry per polled fd, mapping the fd's
 * readiness wake-channel (the this_chan a driver's ->poll stores) to the
 * poller's private cookie.  When an object signals readiness on its channel
 * (sched_wakeup / sleepq_wake_all), poll_notify() wakes ONLY the pollers
 * registered on THAT channel — instead of the old system-wide fan-out to
 * g_poll_wake_chan that woke every poller on every event (the thundering herd
 * that pinned the kernel under heavy AF_UNIX/X11 traffic).
 *
 * Entries live on the registering thread's kern_poll stack frame (it is blocked
 * there while registered), so no allocation is needed.  The lock is IRQ-safe:
 * readiness wakes fire from hard-IRQ context (tty/tcp/af_inet).
 */
#define POLLREG_HASH        256
#define POLL_NOTIFY_BATCH   32
static struct poll_ent *pollreg[POLLREG_HASH];
static spinlock_t pollreg_lock = SPINLOCK_INIT("pollreg");

/*
 * Count of currently-registered pollers.  poll_notify() fires on EVERY
 * sched_wakeup / sleepq_wake_all — the hottest path in the kernel (every block
 * I/O completion, pipe, socket, and sleepq wake) — but a poller is registered
 * only while a thread is blocked inside poll()/select(), which is rare relative
 * to that traffic.  When the count is zero, poll_notify() skips the lock and
 * bucket walk entirely and just bumps the mid-scan re-scan sequence.
 */
static volatile int pollreg_count;

static inline unsigned pollreg_hash(const void *chan) {
    return (unsigned)(((uintptr_t)chan >> 4)) & (POLLREG_HASH - 1);
}

void poll_register(struct poll_ent *e, void *chan, void *cookie) {
    if (!e || !chan) return;
    e->chan = chan;
    e->cookie = cookie;
    unsigned h = pollreg_hash(chan);
    unsigned long f = spinlock_acquire_irq(&pollreg_lock);
    e->hnext = pollreg[h];
    pollreg[h] = e;
    pollreg_count++;
    spinlock_release_irq(&pollreg_lock, f);
}

void poll_unregister(struct poll_ent *e) {
    if (!e || !e->chan) return;
    unsigned h = pollreg_hash(e->chan);
    unsigned long f = spinlock_acquire_irq(&pollreg_lock);
    for (struct poll_ent **pp = &pollreg[h]; *pp; pp = &(*pp)->hnext) {
        if (*pp == e) { *pp = e->hnext; pollreg_count--; break; }
    }
    spinlock_release_irq(&pollreg_lock, f);
    e->chan = NULL;
}

void poll_notify(void *chan) {
    if (!chan) return;

    /* Fast path: with no poller registered anywhere, skip the lock and bucket
     * walk — this runs on every wakeup in the kernel, so the common no-poller
     * case must be nearly free.  The seq bump still fires so a poller that is
     * RUNNING mid-scan (registered after it read the seq) re-scans instead of
     * sleeping into its backstop. */
    if (__atomic_load_n(&pollreg_count, __ATOMIC_RELAXED) == 0) {
        g_poll_wake_seq++;
        return;
    }

    /* Snapshot the matching cookies under the lock, then wake outside it —
     * sched_wakeup_n() takes tid_lock/sq_lock, which must never nest under
     * pollreg_lock. */
    void *cookies[POLL_NOTIFY_BATCH];
    int n = 0;
    unsigned h = pollreg_hash(chan);
    unsigned long f = spinlock_acquire_irq(&pollreg_lock);
    for (struct poll_ent *e = pollreg[h]; e; e = e->hnext) {
        if (e->chan == chan) {
            if (n < POLL_NOTIFY_BATCH) cookies[n++] = e->cookie;
            /* else: overflow -> the poller's backstop deadline recovers it */
        }
    }
    spinlock_release_irq(&pollreg_lock, f);

    for (int i = 0; i < n; i++)
        sched_wakeup_n(cookies[i], -1);

    /* Bump the seq so a poller that is RUNNING mid-scan re-scans rather than
     * sleeping into its backstop (the same lost-wakeup recovery as before). */
    g_poll_wake_seq++;
}

void sched_poll_wake_pollers(void) {
    /* Legacy global kick: pollers now sleep on private cookies woken by
     * poll_notify(), so this only bumps the mid-scan re-scan sequence. */
    g_poll_wake_seq++;
}

void sched_wakeup(void *chan) {
    sched_wakeup_n(chan, -1);
    /* Targeted poll/select wake: only sleepers registered on THIS channel. */
    poll_notify(chan);
}

void sched_wakeup_n(void *chan, int n) {
    int woken = 0;

    /* Bump the poll wake-sequence on every fan-out to the poll channel —
     * unconditionally, even if no poller is currently BLOCKED, because that
     * (a wake arriving while the poller is RUNNING mid-scan) is exactly the
     * lost-wakeup case kern_poll() recovers from by re-scanning.  A plain
     * volatile increment is fine: callers only test "changed vs snapshot",
     * so a lost increment between racing wakers still flips the value. */
    if (chan == &g_poll_wake_chan)
        g_poll_wake_seq++;

    /* KERN-06: sched_wakeup_n runs from IRQ wake paths (tty/tcp/af_inet) and
     * walks the registry; hold it so a concurrent reap can't free a thread_t
     * mid-walk.  The body takes the sleepq bucket lock (sleepq_remove_thread),
     * which is ranked below tid_lock -- consistent order, no ABBA. */
    unsigned long rf = thread_registry_lock();
    FOREACH_THREAD(thread) {
        if (thread->state == THREAD_BLOCKED && thread->wait_chan == chan) {
            /*
             * KERN-04: a thread parked via sleepq_add() is linked in a
             * sleepq bucket AND carries wait_chan == chan.  Readying it by
             * only clearing wait_chan would strand its bucket entry: the
             * sleepq self-unlink path (sleepq_remove_thread) keys off
             * wait_chan, which we are about to NULL, so the stale entry would
             * be popped by a later sleepq_wake and dereferenced after the
             * thread_t is freed (UAF).  Dequeue it from the sleepq first —
             * sleepq_remove_thread finds the bucket via wait_chan (still set
             * here) and is a no-op for pure sched_sleep() sleepers, which are
             * on no bucket.  It is IRQ-safe (KERN-01), so this is fine even on
             * the IRQ-context wake paths (tty/tcp/af_inet).
             */
            sleepq_remove_thread(thread);
            thread->state = THREAD_READY;
            thread->wait_chan = NULL;
            woken++;
            if (n > 0 && woken >= n) break;
        }
    }
    thread_registry_unlock(rf);

    if (woken > 0 && current_thread) {
        current_thread->needs_resched = 1;
    }
}

void sched_iterate_threads(void (*callback)(thread_t *t, void *arg), void *arg) {
    FOREACH_THREAD(thread) {
        callback(thread, arg);
    }
}

void sched_reap_process_threads(process_t *proc) {
    if (!proc) {
        return;
    }

    thread_t *t, *next;
    for (t = thread_first(); t; t = next) {
        next = thread_next(t);
        if (t->proc != proc) continue;
        sched_reap_thread(t);
    }
}

/* Non-zero if t is currently the running thread on some CPU other than the
 * caller's — i.e. still executing on another core. */
int sched_thread_running_remote(thread_t *t) {
    int self = percpu_get_cpu_id();
    int n = smp_get_cpu_count();
    if (n > MAX_CPUS) n = MAX_CPUS;
    for (int c = 0; c < n; c++) {
        if (c == self) continue;
        if (percpu_get_cpu(c)->current == t) {
            return 1;
        }
    }
    return 0;
}

/* Bounded spin until t is off every remote CPU.  A thread marked
 * THREAD_ZOMBIE is dropped by the remote scheduler on its next reschedule
 * (<= one timer tick), so this normally returns almost immediately; the cap
 * prevents a wedge if a remote CPU is stuck.  On a uniprocessor — or with the
 * APs parked — it is a no-op.  Callers use it to avoid freeing or force-
 * mutating a sibling thread that is still live on another core. */
void sched_wait_thread_offcpu(thread_t *t) {
    if (!t) return;
    int spins = 0;
    while (sched_thread_running_remote(t) && spins++ < 20000000) {
        __asm__ volatile("pause");
    }
}

void sched_reap_thread(thread_t *t) {
    if (!t) return;

    /* Never free a thread that is still executing on another CPU: proc_exit
     * marks siblings THREAD_ZOMBIE without forcing them off remote cores, so a
     * concurrent wait4 reap could otherwise free the kstack/thread_t out from
     * under a running sibling (A26).  Wait for it to leave the CPU first. */
    sched_wait_thread_offcpu(t);

    /* Release any kernel mutexes this thread still holds, now that it is
     * guaranteed off-CPU.  proc_exit defers a still-running sibling's release
     * to here rather than force-releasing (and corrupting held_mutexes) while
     * the sibling concurrently mutates it on another core (A22).  Idempotent
     * if proc_exit already released them. */
    mutex_release_owned_by_thread(t);

    /* A thread can still be linked on a sleepq at reap time: proc_exit
     * dequeues each thread once, but a thread killed mid-sleep can re-block
     * (e.g. a looping FUTEX_WAIT) before it takes the fatal signal, landing
     * back on a sleepq as it zombifies.  Freeing the thread_t while a sleepq
     * still points at it is a use-after-free the next wake would trip over,
     * so make sure it is off every queue before we release its storage. */
    sleepq_remove_thread(t);

    /* A thread zombified while blocked in poll()/select() never ran its own
     * poll_unregister (kern_poll), so its poll_ent registrations still point
     * into the stack we are about to free.  Remove them from pollreg now, while
     * the stack is still valid -- otherwise the next poll_notify() walk follows
     * a dangling pointer into freed/reused memory and faults (SMP UAF). */
    if (t->poll_ents && t->poll_nents) {
        struct poll_ent *pents = (struct poll_ent *)t->poll_ents;
        for (unsigned int k = 0; k < t->poll_nents; k++)
            poll_unregister(&pents[k]);
        t->poll_ents = NULL;
        t->poll_nents = 0;
    }

    sched_release_thread_storage(t);

    unsigned long tf = spinlock_acquire_irq(&tid_lock);
    sched_unlink_locked(t);
    spinlock_release_irq(&tid_lock, tf);

    sched_storage_free(t);
}

/* ---------------------------------------------------------------------------
 * Detached-LWP self-reap + per-LWP suspend/continue.
 *
 * A detached thread (NetBSD LWP_DETACHED / _lwp_detach) has no joiner, so when
 * it exits nobody calls sched_reap_thread() for it and its kernel stack +
 * thread_t would leak as a permanent zombie.  sys_thr_exit() flags the pending
 * work via sched_mark_detached_zombie(); sched_yield() then drains it from a
 * safe context (never the exiting thread's own stack) — the same deferred-reap
 * shape as proc_reap_autoreap_zombies().
 * ------------------------------------------------------------------------- */
static volatile int detached_zombie_pending;

void sched_mark_detached_zombie(void) {
    __sync_fetch_and_add(&detached_zombie_pending, 1);
}

void sched_reap_detached_zombies(void) {
    if (detached_zombie_pending == 0)
        return;                       /* fast path: nothing to reap */

    /* Serialize reapers (a concurrent CPU / a nested yield) so a thread_t is
     * claimed and freed exactly once. */
    static volatile int reaping;
    if (__sync_lock_test_and_set(&reaping, 1))
        return;

    for (;;) {
        thread_t *victim = NULL;

        unsigned long tf = spinlock_acquire_irq(&tid_lock);
        FOREACH_THREAD(t) {
            if (t == current_thread)
                continue;
            if (t->state == THREAD_ZOMBIE && (t->flags & THREAD_F_DETACHED)) {
                /* Claim by unlinking from the registry under tid_lock: this is
                 * the atomic ownership transfer.  Once unlinked, neither
                 * another detached-reaper scan nor proc_exit's
                 * sched_reap_process_threads() walk can find it, so it is freed
                 * exactly once.  sched_reap_thread()'s own unlink below is then
                 * an idempotent no-op. */
                victim = t;
                sched_unlink_locked(t);
                break;
            }
        }
        spinlock_release_irq(&tid_lock, tf);

        if (!victim)
            break;
        __sync_fetch_and_sub(&detached_zombie_pending, 1);
        sched_reap_thread(victim);
    }

    __sync_lock_release(&reaping);
}

/* Mark tid detached.  If it has already exited (a joinable thread that
 * zombied before the detach), hand it to the deferred reaper. */
int sched_lwp_detach(tid_t tid) {
    thread_t *t = sched_get_thread((int)tid);
    if (!t || t->proc != current_process)
        return -ESRCH;
    t->flags |= THREAD_F_DETACHED;
    if (t->state == THREAD_ZOMBIE)
        sched_mark_detached_zombie();
    return 0;
}

/* Set the detached flag on a freshly-created LWP (from _lwp_create's
 * LWP_DETACHED); it has not run yet, so this is race-free. */
int sched_lwp_set_detached(tid_t tid) {
    thread_t *t = sched_get_thread((int)tid);
    if (!t || t->proc != current_process)
        return -ESRCH;
    t->flags |= THREAD_F_DETACHED;
    return 0;
}

/* _lwp_suspend: take an LWP off-CPU until _lwp_continue.  THREAD_F_SUSPENDED
 * keeps it distinct from a job-control THREAD_STOPPED so SIGCONT won't resume
 * it (see signal.c).  A READY/RUNNING target is parked in THREAD_STOPPED so the
 * scheduler skips it; a blocked target just carries the flag and parks when it
 * would next become runnable. */
int sched_lwp_suspend(tid_t tid) {
    thread_t *t = sched_get_thread((int)tid);
    if (!t || t->proc != current_process)
        return -ESRCH;
    if (t == current_thread)
        return -EDEADLK;              /* suspending self is not supported here */
    t->flags |= THREAD_F_SUSPENDED;
    if (t->state == THREAD_READY || t->state == THREAD_RUNNING)
        t->state = THREAD_STOPPED;
    return 0;
}

/* _lwp_continue: undo _lwp_suspend and make the LWP runnable again. */
int sched_lwp_continue(tid_t tid) {
    thread_t *t = sched_get_thread((int)tid);
    if (!t || t->proc != current_process)
        return -ESRCH;
    t->flags &= ~THREAD_F_SUSPENDED;
    if (t->state == THREAD_STOPPED)
        t->state = THREAD_READY;
    return 0;
}

/* Find, atomically claim, and reap one *waitable* zombie sibling of the caller
 * in the current process; returns its tid, or -1 if none is a zombie right now.
 * A detached LWP is NOT waitable — the kernel's own detached reaper owns it, so
 * _lwp_wait() must neither reap nor report it (NetBSD kern_lwp.c skips
 * LPR_DETACHED when counting waitable LWPs).
 * The claim (sched_unlink_locked under tid_lock) removes the victim from the
 * registry so a concurrent _lwp_wait(0) cannot find and double-free it; the
 * subsequent sched_reap_thread()'s own unlink is then a harmless no-op.
 * Backs _lwp_wait(0) ("wait for any LWP"). */
int sched_reap_any_zombie_sibling(void) {
    thread_t *victim = NULL;
    int lid = -1;
    unsigned long tf = spinlock_acquire_irq(&tid_lock);
    FOREACH_THREAD(t) {
        if (t == current_thread || t->proc != current_process)
            continue;
        if (t->flags & THREAD_F_DETACHED)
            continue;
        if (t->state == THREAD_ZOMBIE) {
            victim = t;
            lid = t->tid;
            sched_unlink_locked(t);       /* claim before releasing the lock */
            break;
        }
    }
    spinlock_release_irq(&tid_lock, tf);
    if (!victim)
        return -1;
    sched_reap_thread(victim);
    return lid;
}

/* True if the current process has any *waitable* sibling — one that is neither
 * the caller nor detached (zombie or not).  _lwp_wait() reports ESRCH when
 * there is nothing it could ever wait for, matching NetBSD's `nfound == 0`
 * check in lwp_wait(). */
int sched_has_waitable_siblings(void) {
    int found = 0;
    unsigned long rf = thread_registry_lock();
    FOREACH_THREAD(t) {
        if (t == current_thread || t->proc != current_process)
            continue;
        if (t->flags & THREAD_F_DETACHED)
            continue;
        found = 1;
        break;
    }
    thread_registry_unlock(rf);
    return found;
}

/* Validate a specific _lwp_wait(lid) target: ESRCH if it is not an LWP of this
 * process, EINVAL if it is detached (NetBSD returns EINVAL for a detached
 * target rather than waiting on it), else 0. */
int sched_lwp_wait_check(tid_t tid) {
    thread_t *t = sched_get_thread((int)tid);
    if (!t || t->proc != current_process)
        return -ESRCH;
    if (t->flags & THREAD_F_DETACHED)
        return -EINVAL;
    return 0;
}
