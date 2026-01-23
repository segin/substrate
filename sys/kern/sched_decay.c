/*
 * sched_decay.c - Priority Decay for CPU-bound Threads
 * 
 * Implements priority aging to prevent starvation and penalize CPU hogs.
 */

#include <kern/sched.h>
#include <sys/proc.h>

/* Decay and anti-starvation constants defined in sched.h */

// Last decay tick (global for simplicity, should be per-CPU for SMP)
static uint32_t last_decay_tick = 0;
static uint32_t last_recalc_tick = 0;

// Forward declaration
extern thread_t threads[];
extern int sched_interactivity_boost(thread_t *t);

// Decay priority for a CPU-bound thread
static void decay_thread_priority(thread_t *t) {
    if (!t || t->tid < 0) return;
    if (t->sched_class != SCHED_TIMESHARE) return;
    
    // Only decay threads that have been running
    if (t->run_time == 0) return;
    
    // Calculate priority penalty based on CPU usage
    // More CPU time = more penalty
    int penalty = (int)(t->run_time / DECAY_PERIOD);
    if (penalty > 10) penalty = 10;  // Cap penalty
    
    // Apply penalty (higher number = lower priority in timeshare)
    t->priority = t->base_priority + penalty;
    
    // Clamp to valid range (0-39 for timeshare)
    if (t->priority > 39) t->priority = 39;
    if (t->priority < 0) t->priority = 0;
}

// Boost priority for starved thread
static void boost_starved_thread(thread_t *t, uint32_t current_tick) {
    if (!t || t->tid < 0) return;
    if (t->state != THREAD_READY) return;
    if (t->sched_class != SCHED_TIMESHARE) return;
    
    // Check how long thread has been waiting
    // (In a real implementation, we'd track last_run_tick)
    // For now, use sleep_time as a proxy (threads sleeping voluntarily don't count)
    
    // If thread has been on runqueue a while without running, boost it
    if (t->run_time == 0 && t->sleep_time > STARVATION_LIMIT) {
        // Thread hasn't run in a while but isn't sleeping - it's starving
        t->priority = t->base_priority - STARVATION_BOOST;
        if (t->priority < 0) t->priority = 0;
    }
}

// Recalculate effective priority for thread
void sched_recalc_priority(thread_t *t) {
    if (!t) return;
    
    switch (t->sched_class) {
        case SCHED_REALTIME:
            // RT threads keep their assigned priority
            t->priority = t->base_priority;
            break;
            
        case SCHED_TIMESHARE:
            // Start from base priority
            t->priority = t->base_priority;
            
            // Apply CPU usage decay
            {
                int penalty = (int)(t->run_time / DECAY_PERIOD);
                if (penalty > 10) penalty = 10;
                t->priority += penalty;
            }
            
            // Apply interactivity adjustment
            {
                int boost = sched_interactivity_boost(t);
                t->priority += boost;
            }
            
            // Clamp to valid range
            if (t->priority > 39) t->priority = 39;
            if (t->priority < 0) t->priority = 0;
            break;
            
        case SCHED_IDLE:
            // Idle threads always have lowest priority
            t->priority = 0;
            break;
    }
}

// Periodic decay pass (called from timer interrupt)
void sched_decay_tick(uint32_t current_tick) {
    // Check if it's time for decay pass
    if (current_tick - last_decay_tick < DECAY_PERIOD) return;
    last_decay_tick = current_tick;
    
    // Decay all timeshare threads
    for (int i = 0; i < MAX_THREADS; i++) {
        if (threads[i].tid < 0) continue;
        if (threads[i].sched_class != SCHED_TIMESHARE) continue;
        
        decay_thread_priority(&threads[i]);
        boost_starved_thread(&threads[i], current_tick);
    }
}

// Full priority recalculation pass
void sched_recalc_all_priorities(uint32_t current_tick) {
    if (current_tick - last_recalc_tick < RECALC_PERIOD) return;
    last_recalc_tick = current_tick;
    
    for (int i = 0; i < MAX_THREADS; i++) {
        if (threads[i].tid < 0) continue;
        sched_recalc_priority(&threads[i]);
    }
    
    // Reset epoch counters to allow priorities to recover
    for (int i = 0; i < MAX_THREADS; i++) {
        if (threads[i].tid < 0) continue;
        threads[i].run_time = threads[i].run_time / 2;
        threads[i].sleep_time = threads[i].sleep_time / 2;
    }
}

// Reset thread stats when it becomes interactive (e.g., waits for I/O)
void sched_decay_on_voluntary_sleep(thread_t *t) {
    if (!t) return;
    
    // Voluntary sleep indicates interactive behavior
    // Give partial credit back
    if (t->run_time > 10) {
        t->run_time -= 10;
    } else {
        t->run_time = 0;
    }
}

// Called when thread voluntarily yields CPU
void sched_decay_on_yield(thread_t *t) {
    if (!t) return;
    
    // Yielding is slightly good behavior
    if (t->run_time > 5) {
        t->run_time -= 5;
    }
}
