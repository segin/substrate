#include <kern/sched.h>
#include <kern/time.h>
#include <sys/errno.h>
#include <sys/futex.h>
#include <pm/pm.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <sys/types.h>
#ifndef HOST_TEST
#include <vm/vm_kmem.h>
#else
#include <stdlib.h>
#endif

thread_t *current_thread = NULL;

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


#include <kern/arch.h>
#include <arch/i386/percpu.h>
#include <arch/i386/intr.h>
#include <sys/preempt.h>

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

static void sched_link_locked(thread_t *t) {
    t->t_allthread_next = allthread;
    allthread = t;

    unsigned bucket = TID_HASH(t->tid);
    t->t_tidhash_next = tid_hash[bucket];
    tid_hash[bucket] = t;
}

static void sched_unlink_locked(thread_t *t) {
    thread_t **link;

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
    extern void kfree(void *ptr, size_t size);
    extern void pmm_free_block(void *p);
    extern void pmm_free_contiguous(void *p, size_t count);

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

    spinlock_acquire(&tid_lock);
    tid = sched_alloc_tid_locked(proc);
    if (tid < 0) {
        spinlock_release(&tid_lock);
        sched_storage_free(thread);
        return NULL;
    }
    thread->tid = tid;
    sched_link_locked(thread);
    spinlock_release(&tid_lock);

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
        extern void pmap_activate(void *pmap);
        pmap_activate(next->proc->pmap);
    }

    if (prev && prev != next) {
        arch_switch_to(prev, next);
    }
}
void sched_yield(void) {
    if (!current_thread) return;

    proc_reap_autoreap_zombies();

    /* Disable interrupts across thread selection and the context switch so
     * a timer tick cannot re-enter sched_yield (kernel preemption) and
     * corrupt the round-robin cursor or a half-finished switch.  A newly
     * created thread first runs with interrupts disabled here and re-enables
     * them itself -- exactly as it already does when first scheduled from
     * the timer-IRQ preemption path, so this changes nothing for them. */
    uint32_t _pflags = intr_disable();

    extern int percpu_get_cpu_id(void);
    int cpu_id = percpu_get_cpu_id();

    thread_t *best_thread = NULL;
    int highest_prio = -1;
    sched_class_t best_class = SCHED_IDLE;

    /*
     * Round-robin cursor: the thread we picked last time.  We start
     * scanning at its successor (wrapping past tail back to head) so
     * equal-priority threads get fair turns regardless of which order
     * they were linked in.
     */
    static thread_t *rr_last = NULL;

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

    spinlock_acquire(&tid_lock);
    thread_t *t = sched_lookup_tid_locked(tid);
    spinlock_release(&tid_lock);
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
    extern void sched_periodic_balance(void);
    sched_periodic_balance();

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

void sched_poll_wake_pollers(void) {
    sched_wakeup_n(&g_poll_wake_chan, -1);
}

void sched_wakeup(void *chan) {
    sched_wakeup_n(chan, -1);
    /* Also kick any select/poll sleepers — they may be interested
     * in `chan` even though they're sleeping on &g_poll_wake_chan.
     * Recursion-safe: this calls sched_wakeup_n directly, not back
     * into sched_wakeup. */
    if (chan != &g_poll_wake_chan)
        sched_wakeup_n(&g_poll_wake_chan, -1);
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

    FOREACH_THREAD(thread) {
        if (thread->state == THREAD_BLOCKED && thread->wait_chan == chan) {
            thread->state = THREAD_READY;
            thread->wait_chan = NULL;
            woken++;
            if (n > 0 && woken >= n) break;
        }
    }

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

void sched_reap_thread(thread_t *t) {
    if (!t) return;

    sched_release_thread_storage(t);

    spinlock_acquire(&tid_lock);
    sched_unlink_locked(t);
    spinlock_release(&tid_lock);

    sched_storage_free(t);
}
