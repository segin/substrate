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

#define TID_HASH_SIZE 64
#define TID_HASH(tid) ((unsigned)(tid) & (TID_HASH_SIZE - 1))

static thread_t *allthread = NULL;
static thread_t *tid_hash[TID_HASH_SIZE];
static int next_tid = 1;
static spinlock_t tid_lock;


#include <kern/arch.h>
#include <arch/i386/percpu.h>

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

static void sched_context_switch(thread_t *prev, thread_t *next) {
    if (prev && prev->state == THREAD_RUNNING) prev->state = THREAD_READY;

    // Handle thread exit notification after switching out but before changing address space
    if (prev && prev->state == THREAD_ZOMBIE && prev->exit_tid_ptr) {
        futex_wake_exited_thread(prev->exit_tid_ptr);
        prev->exit_tid_ptr = NULL;
    }

    next->state = THREAD_RUNNING;

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
        return;
    }

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

            bool better = false;
            if (!best_thread) {
                better = true;
            } else if (candidate->sched_class < best_class) {
                better = true;
            } else if (candidate->sched_class == best_class) {
                if (candidate->priority > highest_prio) {
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

    if (best_thread == current_thread && current_thread->state == THREAD_RUNNING) return;

    if (best_thread) {
        rr_last = best_thread;
        sched_context_switch(current_thread, best_thread);
    }
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

    current_thread->wait_chan = chan;
    current_thread->state = THREAD_BLOCKED;
    sched_yield();
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
            thread->wait_chan = NULL;
            thread->sleep_status = -ETIMEDOUT;
            thread->sleep_expiry = 0;
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
