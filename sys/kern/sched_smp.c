/*
 * sched_smp.c - SMP Scheduler Support
 * 
 * Per-CPU runqueues, load balancing, and CPU affinity handling.
 */

#include <kern/sched.h>
#include <kern/runqueue.h>
#include <sys/proc.h>
#include <string.h>

/* MAX_CPUS defined in sched.h */

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
// Get current CPU's runqueue
runqueue_t *sched_get_current_runqueue(void) {
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

// ==================== Load Balancing (Work Stealing) ====================

// Find busiest CPU
static int find_busiest_cpu(int exclude_cpu) {
    int busiest = -1;
    uint32_t max_load = 0;
    
    for (int i = 0; i < num_cpus; i++) {
        if (i == exclude_cpu) continue;
        if (cpu_runqueues[i].total_threads > max_load) {
            max_load = cpu_runqueues[i].total_threads;
            busiest = i;
        }
    }
    
    return busiest;
}

// Try to steal a thread from another CPU's runqueue
thread_t *sched_steal_thread(int target_cpu) {
    runqueue_t *rq = &cpu_runqueues[target_cpu];
    thread_t *stolen = NULL;
    
    // Lock target runqueue
    while (__sync_lock_test_and_set(&rq->lock, 1)) {
        while (rq->lock) __asm__ volatile("pause");
    }
    
    // Only steal if they have enough threads
    if (rq->total_threads < 2) {
        __sync_lock_release(&rq->lock);
        return NULL;
    }
    
    // Steal from lowest priority (idle) queues first to minimize impact
    // Work backwards from idle queue
    for (int level = RQ_TOTAL_LEVELS - 1; level >= RQ_TIMESHARE_BASE; level--) {
        runqueue_level_t *q = &rq->queues[level];
        if (q->count == 0) continue;
        
        // Get a thread (from tail to minimize cache impact)
        thread_t *t = q->tail;
        if (!t) continue;
        
        // Check CPU affinity - can we run this thread?
        extern int percpu_get_cpu_id(void);
        int my_cpu = percpu_get_cpu_id();
        if (t->cpu_affinity != 0 && !(t->cpu_affinity & (1U << my_cpu))) {
            continue;  // Thread can't run on our CPU
        }
        
        // Steal this thread
        runqueue_remove(rq, t);
        t->on_runqueue = 0;
        stolen = t;
        break;
    }
    
    __sync_lock_release(&rq->lock);
    return stolen;
}

// Perform load balancing for current CPU
// Returns: thread stolen, or NULL if none
thread_t *sched_load_balance(void) {
    extern int percpu_get_cpu_id(void);
    int my_cpu = percpu_get_cpu_id();
    
    // Find busiest CPU
    int busiest = find_busiest_cpu(my_cpu);
    if (busiest < 0) return NULL;
    
    // Check if imbalance is significant
    uint32_t my_load = cpu_runqueues[my_cpu].total_threads;
    uint32_t their_load = cpu_runqueues[busiest].total_threads;
    
    // Only steal if they have at least 2 more threads than us
    if (their_load < my_load + 2) return NULL;
    
    // Try to steal
    return sched_steal_thread(busiest);
}

// Called when a CPU becomes idle - try to find work
thread_t *sched_idle_balance(void) {
    extern int percpu_get_cpu_id(void);
    int my_cpu = percpu_get_cpu_id();
    
    // First, check our own runqueue
    runqueue_t *my_rq = sched_get_runqueue(my_cpu);
    if (my_rq && !runqueue_empty(my_rq)) {
        return sched_pick_next();
    }
    
    // Try to steal from each CPU in turn
    for (int i = 0; i < num_cpus; i++) {
        if (i == my_cpu) continue;
        
        thread_t *t = sched_steal_thread(i);
        if (t) return t;
    }
    
    return NULL;
}

// Periodic load balancing (called from timer)
void sched_periodic_balance(void) {
    if (num_cpus <= 1) return;
    if (!sched_needs_load_balance()) return;
    
    // Only rebalance occasionally to avoid overhead
    static uint32_t balance_counter = 0;
    balance_counter++;
    if (balance_counter % 100 != 0) return;
    
    // Each CPU checks if it should steal
    thread_t *t = sched_load_balance();
    if (t) {
        // Re-enqueue stolen thread to our runqueue
        sched_enqueue(t);
    }
}
