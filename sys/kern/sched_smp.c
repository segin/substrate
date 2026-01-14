/*
 * sched_smp.c - SMP Scheduler Support
 * 
 * Per-CPU runqueues, load balancing, and CPU affinity handling.
 */

#include <kern/sched.h>
#include <kern/runqueue.h>
#include <sys/proc.h>
#include <string.h>

// Maximum CPUs supported
#define MAX_CPUS 32

// Per-CPU runqueues
static runqueue_t cpu_runqueues[MAX_CPUS];
static int num_cpus = 1;

// Initialize per-CPU runqueues
void sched_smp_init(int cpu_count) {
    if (cpu_count > MAX_CPUS) cpu_count = MAX_CPUS;
    num_cpus = cpu_count;
    
    for (int i = 0; i < num_cpus; i++) {
        runqueue_init(&cpu_runqueues[i], i);
    }
}

// Get runqueue for a CPU
runqueue_t *sched_get_runqueue(int cpu_id) {
    if (cpu_id < 0 || cpu_id >= num_cpus) return NULL;
    return &cpu_runqueues[cpu_id];
}

// Get current CPU's runqueue
runqueue_t *sched_get_current_runqueue(void) {
    // For now, assume CPU 0 (BSP)
    // In full SMP, use percpu_get_cpu_id()
    extern int percpu_get_cpu_id(void);
    int cpu_id = percpu_get_cpu_id();
    return sched_get_runqueue(cpu_id);
}

// Add thread to appropriate CPU's runqueue
void sched_enqueue(thread_t *t) {
    if (!t) return;
    if (t->on_runqueue) return;  // Already enqueued
    
    // Pick CPU based on affinity or load balancing
    int target_cpu = 0;
    
    if (t->cpu_affinity != 0) {
        // Find first allowed CPU in affinity mask
        for (int i = 0; i < num_cpus; i++) {
            if (t->cpu_affinity & (1U << i)) {
                target_cpu = i;
                break;
            }
        }
    } else {
        // Find least loaded CPU
        uint32_t min_load = cpu_runqueues[0].total_threads;
        for (int i = 1; i < num_cpus; i++) {
            if (cpu_runqueues[i].total_threads < min_load) {
                min_load = cpu_runqueues[i].total_threads;
                target_cpu = i;
            }
        }
    }
    
    runqueue_t *rq = &cpu_runqueues[target_cpu];
    
    // Lock runqueue (atomic spinlock)
    while (__sync_lock_test_and_set(&rq->lock, 1)) {
        while (rq->lock) __asm__ volatile("pause");
    }
    
    runqueue_add(rq, t);
    t->on_runqueue = 1;
    
    // Unlock
    __sync_lock_release(&rq->lock);
}

// Remove thread from its runqueue
void sched_dequeue(thread_t *t) {
    if (!t) return;
    if (!t->on_runqueue) return;  // Not on runqueue
    
    // Find which runqueue has this thread
    for (int i = 0; i < num_cpus; i++) {
        runqueue_t *rq = &cpu_runqueues[i];
        
        // Lock
        while (__sync_lock_test_and_set(&rq->lock, 1)) {
            while (rq->lock) __asm__ volatile("pause");
        }
        
        // Check if thread is in this runqueue
        // (In production, thread would track its owning runqueue)
        int found = 0;
        for (int level = 0; level < RQ_TOTAL_LEVELS; level++) {
            thread_t *curr = rq->queues[level].head;
            while (curr) {
                if (curr == t) {
                    found = 1;
                    break;
                }
                curr = curr->rq_next;
            }
            if (found) break;
        }
        
        if (found) {
            runqueue_remove(rq, t);
            t->on_runqueue = 0;
            __sync_lock_release(&rq->lock);
            return;
        }
        
        // Unlock and try next CPU
        __sync_lock_release(&rq->lock);
    }
}

// Pick next thread to run on current CPU
thread_t *sched_pick_next(void) {
    runqueue_t *rq = sched_get_current_runqueue();
    if (!rq) return NULL;
    
    // Lock
    while (__sync_lock_test_and_set(&rq->lock, 1)) {
        while (rq->lock) __asm__ volatile("pause");
    }
    
    thread_t *t = runqueue_pop(rq);
    if (t) {
        t->on_runqueue = 0;
    }
    
    // Unlock
    __sync_lock_release(&rq->lock);
    
    return t;
}

// Get load of a CPU
uint32_t sched_get_cpu_load(int cpu_id) {
    if (cpu_id < 0 || cpu_id >= num_cpus) return 0;
    return cpu_runqueues[cpu_id].total_threads;
}

// Get total system load
uint32_t sched_get_system_load(void) {
    uint32_t total = 0;
    for (int i = 0; i < num_cpus; i++) {
        total += cpu_runqueues[i].total_threads;
    }
    return total;
}

// Check if load balancing is needed
int sched_needs_load_balance(void) {
    if (num_cpus <= 1) return 0;
    
    uint32_t min_load = cpu_runqueues[0].total_threads;
    uint32_t max_load = cpu_runqueues[0].total_threads;
    
    for (int i = 1; i < num_cpus; i++) {
        if (cpu_runqueues[i].total_threads < min_load) {
            min_load = cpu_runqueues[i].total_threads;
        }
        if (cpu_runqueues[i].total_threads > max_load) {
            max_load = cpu_runqueues[i].total_threads;
        }
    }
    
    // Consider imbalance if difference > 2
    return (max_load - min_load) > 2;
}
