#include <kern/sched.h>
#include <pm/pm.h>
#include <stddef.h>
#include <stdint.h>

#include <stddef.h>
#include <stdint.h>

thread_t threads[MAX_THREADS];
thread_t *current_thread = NULL;
static int next_tid = 1;

extern void arch_switch_to(thread_t *prev, thread_t *next);
extern void arch_set_kernel_stack(uintptr_t stack);



void sched_init_generic(void) {
    next_tid = 0;
    extern void *memset(void *s, int c, size_t n);
    memset(threads, 0, sizeof(threads));
    
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

    threads[i].tid = next_tid++;
    threads[i].proc = proc;
    threads[i].state = THREAD_READY;
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
    for (int i = 0; i < MAX_THREADS; i++) {
        if (threads[i].tid == tid) return &threads[i];
    }
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
