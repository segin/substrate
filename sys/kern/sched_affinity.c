/*
 * sched_affinity.c - CPU Affinity Support
 * 
 * Implements sched_setaffinity/sched_getaffinity for thread CPU binding.
 */

#include <kern/sched.h>
#include <sys/proc.h>
#include <string.h>

/* MAX_CPUS defined in sched.h */

extern int num_cpus;
extern int percpu_get_cpu_id(void);
extern void sched_dequeue(thread_t *t);
extern void sched_enqueue(thread_t *t);

static uint32_t sched_valid_affinity_mask(void) {
    if (num_cpus >= 32) {
        return 0xFFFFFFFFu;
    }
    return (1U << num_cpus) - 1;
}

// Set CPU affinity mask for a thread
// mask: bitmask of allowed CPUs (bit N = CPU N allowed)
// Returns: 0 on success, -1 on error
int sched_set_affinity(int tid, uint32_t mask) {
    thread_t *t = sched_get_thread(tid);

    if (!t) return -1;
    
    // Validate mask - at least one valid CPU must be set
    if (mask == 0) return -1;
    
    // Check that mask only includes valid CPUs
    uint32_t valid_mask = sched_valid_affinity_mask();
    if ((mask & valid_mask) == 0) return -1;
    
    // Apply mask
    t->cpu_affinity = mask & valid_mask;
    
    return 0;
}

// Get CPU affinity mask for a thread
// Returns: bitmask, or 0 on error
uint32_t sched_get_affinity(int tid) {
    thread_t *t = sched_get_thread(tid);

    if (!t) return 0;
    
    // Return mask, or all CPUs if unset
    if (t->cpu_affinity == 0) {
        return sched_valid_affinity_mask();  // All CPUs allowed
    }
    return t->cpu_affinity;
}

// Set affinity for current thread
int sched_set_affinity_self(uint32_t mask) {
    extern thread_t *current_thread;
    if (!current_thread) return -1;
    return sched_set_affinity(current_thread->tid, mask);
}

// Get affinity for current thread
uint32_t sched_get_affinity_self(void) {
    extern thread_t *current_thread;
    if (!current_thread) return 0;
    return sched_get_affinity(current_thread->tid);
}

// Check if thread can run on a specific CPU
int sched_can_run_on_cpu(thread_t *t, int cpu_id) {
    if (!t) return 0;
    if (cpu_id < 0 || cpu_id >= num_cpus) return 0;

    if (t->bound_cpu >= 0) {
        return t->bound_cpu == cpu_id;
    }
    
    // No affinity set = can run anywhere
    if (t->cpu_affinity == 0) return 1;

    if (cpu_id >= 32) return 0;
    
    // Check bit in mask
    return (t->cpu_affinity & (1U << cpu_id)) != 0;
}

int sched_bind_thread(thread_t *t, int cpu_id) {
    if (!t) return -1;
    if (cpu_id < 0 || cpu_id >= num_cpus) return -1;
    t->bound_cpu = (int16_t)cpu_id;
    return 0;
}

void sched_unbind_thread(thread_t *t) {
    if (!t) return;
    t->bound_cpu = -1;
}

// Clear affinity (allow all CPUs)
int sched_clear_affinity(int tid) {
    thread_t *t = sched_get_thread(tid);
    if (!t) return -1;
    t->cpu_affinity = 0;
    return 0;
}

// Migrate thread to a different CPU if needed
// Called after affinity change if thread is on a now-forbidden CPU
void sched_migrate_if_needed(thread_t *t) {
    if (!t) return;
    if (t->cpu_affinity == 0) return;  // No restriction
    
    // Get current CPU
    int cpu = percpu_get_cpu_id();
    
    // Check if current CPU is allowed
    if (sched_can_run_on_cpu(t, cpu)) return;
    
    // Thread is on forbidden CPU - need to migrate
    // For running thread, set needs_resched flag
    if (t->state == THREAD_RUNNING) {
        t->needs_resched = 1;
    }
    
    // For ready thread, dequeue and re-enqueue (will pick valid CPU)
    if (t->state == THREAD_READY && t->on_runqueue) {
        sched_dequeue(t);
        sched_enqueue(t);
    }
}
