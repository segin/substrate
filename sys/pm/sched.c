#include <kern/sched.h>
#include <pm/pm.h>
#include <stddef.h>
#include <stdint.h>

#include <stddef.h>
#include <stdint.h>

thread_t threads[MAX_THREADS];
// Generation counters for each thread slot to enable O(1) TID lookup.
// TID = index + (generation * MAX_THREADS)
static uint32_t slot_generations[MAX_THREADS];
thread_t *current_thread = NULL;

extern void arch_switch_to(thread_t *prev, thread_t *next);
extern void arch_set_kernel_stack(uintptr_t stack);



void sched_init_generic(void) {
    extern void *memset(void *s, int c, size_t n);
    memset(threads, 0, sizeof(threads));
    memset(slot_generations, 0, sizeof(slot_generations));
    
    // Initialize PM
    pm_init();

    // Zero out arrays
    for (int i = 0; i < MAX_THREADS; i++) {
        threads[i].tid = -1;
        threads[i].state = THREAD_ZOMBIE;
    }
}

// Just sets up structures, doesn't touch hardware/stack
thread_t *sched_alloc_thread(process_t *proc) {
    int i;
    for (i = 0; i < MAX_THREADS; i++) {
        if (threads[i].tid == -1) break;
    }
    if (i == MAX_THREADS) return NULL;

    // Generate O(1) compatible TID
    threads[i].tid = i + (slot_generations[i] * MAX_THREADS);
    slot_generations[i]++;

    threads[i].proc = proc;
    threads[i].state = THREAD_BLOCKED; // Set to BLOCKED until stack is ready
    threads[i].wait_chan = NULL;
    threads[i].wait_reason = NULL;
    threads[i].priority = current_thread ? current_thread->priority : 20;
    threads[i].base_priority = current_thread ? current_thread->base_priority : 20;
    threads[i].sched_class = current_thread ? current_thread->sched_class : SCHED_TIMESHARE;
    
    // Initialize Signals
    threads[i].sig_mask = current_thread ? current_thread->sig_mask : 0;
    threads[i].sig_pending = 0;
    threads[i].sig_on_stack = 0;
    extern void *memset(void *s, int c, size_t n);
    memset(&threads[i].sig_alt_stack, 0, sizeof(threads[i].sig_alt_stack));
    threads[i].in_syscall = 0;
    
    return &threads[i];
}

void sched_yield(void) {
    if (!current_thread) return;

    extern void kprint(const char *);

    thread_t *best_thread = NULL;
    int highest_prio = -1;
    sched_class_t best_class = SCHED_IDLE;

    // Scan for best thread to run (Generic Policy)
    for (int i = 0; i < MAX_THREADS; i++) {
        if (threads[i].tid == -1 || threads[i].state != THREAD_READY) continue;

        bool better = false;
        if (!best_thread) {
            better = true;
        } else if (threads[i].sched_class < best_class) {
            better = true;
        } else if (threads[i].sched_class == best_class) {
            if (threads[i].priority > highest_prio) {
                better = true;
            }
        }

        if (better) {
            best_thread = &threads[i];
            highest_prio = threads[i].priority;
            best_class = threads[i].sched_class;
        }
    }

    if (best_thread == current_thread && current_thread && current_thread->state == THREAD_RUNNING) return;

    // Context Switch
    thread_t *prev = current_thread;
    thread_t *next = best_thread;
    
    if (prev && prev->state == THREAD_RUNNING) prev->state = THREAD_READY;
    next->state = THREAD_RUNNING;
    
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

int sched_get_current_tid(void) {
    if (current_thread) return current_thread->tid;
    return -1;
}

thread_t *sched_get_thread(int tid) {
    if (tid < 0) return NULL;
    int idx = tid % MAX_THREADS;
    if (threads[idx].tid == tid) return &threads[idx];
    return NULL;
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

void sched_wakeup(void *chan) {
    sched_wakeup_n(chan, -1);
}

void sched_wakeup_n(void *chan, int n) {
    int woken = 0;
    for (int i = 0; i < MAX_THREADS; i++) {
        if (threads[i].tid != -1 && threads[i].state == THREAD_BLOCKED && threads[i].wait_chan == chan) {
            threads[i].state = THREAD_READY;
            threads[i].wait_chan = NULL;
            woken++;
            if (n > 0 && woken >= n) break;
        }
    }
}

void sched_iterate_threads(void (*callback)(thread_t *t, void *arg), void *arg) {
    for (int i = 0; i < MAX_THREADS; i++) {
        if (threads[i].tid != -1) {
            callback(&threads[i], arg);
        }
    }
}

/* Load Average Calculation */

static unsigned long avenrun[3] = { 0, 0, 0 };

uint32_t sched_get_active_tasks(void) {
    uint32_t count = 0;
    for (int i = 0; i < MAX_THREADS; i++) {
        if (threads[i].tid != -1 &&
           (threads[i].state == THREAD_RUNNING || threads[i].state == THREAD_READY)) {
            count++;
        }
    }
    return count;
}

uint32_t sched_get_total_tasks(void) {
    uint32_t count = 0;
    for (int i = 0; i < MAX_THREADS; i++) {
        if (threads[i].tid != -1) {
            count++;
        }
    }
    return count;
}

void sched_update_loadavg(void) {
    /* Get active tasks scaled to fixed point */
    unsigned long active = sched_get_active_tasks() * SI_LOAD_SCALE;

    /* Calculate new load averages */
    avenrun[0] = (avenrun[0] * EXP_1 + active * (SI_LOAD_SCALE - EXP_1)) >> 11;
    avenrun[1] = (avenrun[1] * EXP_5 + active * (SI_LOAD_SCALE - EXP_5)) >> 11;
    avenrun[2] = (avenrun[2] * EXP_15 + active * (SI_LOAD_SCALE - EXP_15)) >> 11;
}

void sched_get_loadavg(unsigned long *loads) {
    if (!loads) return;
    loads[0] = avenrun[0];
    loads[1] = avenrun[1];
    loads[2] = avenrun[2];
}
