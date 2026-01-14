/*
 * sched_interactivity.c - Interactivity Scoring for Scheduler
 * 
 * Implements ULE-style interactivity heuristics to boost I/O-bound threads.
 */

#include <kern/sched.h>
#include <sys/proc.h>

// Interactivity constants
#define INTERACT_MAX        128     // Maximum interactivity score
#define INTERACT_THRESH     30      // Threshold to be considered "interactive"
#define INTERACT_DECAY      2       // Decay rate per tick when running
#define INTERACT_BOOST      4       // Boost rate per tick when sleeping

// Time slice constants (in scheduler ticks)
#define SLICE_MIN           1       // Minimum time slice
#define SLICE_MAX           10      // Maximum time slice (for idle threads)
#define SLICE_INTERACTIVE   4       // Time slice for interactive threads
#define SLICE_BATCH         8       // Time slice for batch/CPU-bound threads

// Calculate interactivity score based on sleep/run ratio
// Returns: -128 (CPU-bound) to +127 (I/O-bound)
int sched_calc_interactivity(thread_t *t) {
    if (!t) return 0;
    
    uint32_t total = t->sleep_time + t->run_time;
    if (total == 0) return 0;  // No data yet
    
    // Ratio: (sleep - run) / total, scaled to -128..+127
    int32_t diff = (int32_t)t->sleep_time - (int32_t)t->run_time;
    int32_t score = (diff * INTERACT_MAX) / (int32_t)total;
    
    // Clamp to int16 range
    if (score > 127) score = 127;
    if (score < -128) score = -128;
    
    return (int)score;
}

// Update thread interactivity when it starts running
void sched_interactivity_on_run(thread_t *t) {
    if (!t || t->sched_class != SCHED_TIMESHARE) return;
    
    // Calculate new interactivity score
    t->interactivity = (int16_t)sched_calc_interactivity(t);
    
    // Reset epoch counters periodically to allow adaptation
    if (t->run_time + t->sleep_time > 1000) {
        t->run_time = t->run_time / 2;
        t->sleep_time = t->sleep_time / 2;
    }
}

// Update thread when it goes to sleep (I/O wait, etc.)
void sched_interactivity_on_sleep(thread_t *t) {
    if (!t || t->sched_class != SCHED_TIMESHARE) return;
    
    // Being willing to sleep is a sign of interactivity
    // The actual sleep time will be counted when it wakes up
}

// Called on timer tick while thread is running
void sched_interactivity_tick(thread_t *t) {
    if (!t) return;
    
    // Count running time
    t->run_time++;
    
    // Decay time slice
    if (t->time_slice > 0) {
        t->time_slice--;
    }
    
    // For timeshare threads, adjust interactivity
    if (t->sched_class == SCHED_TIMESHARE) {
        // Slight decay of interactivity while running
        if (t->interactivity > -INTERACT_MAX) {
            t->interactivity -= INTERACT_DECAY;
        }
    }
}

// Called when thread wakes up from sleep
void sched_interactivity_on_wakeup(thread_t *t, uint32_t sleep_ticks) {
    if (!t) return;
    
    // Credit sleep time
    t->sleep_time += sleep_ticks;
    
    // Boost interactivity for timeshare threads
    if (t->sched_class == SCHED_TIMESHARE) {
        int32_t boost = (int32_t)sleep_ticks * INTERACT_BOOST / 10;
        if (boost > 20) boost = 20;  // Cap single boost
        
        t->interactivity += (int16_t)boost;
        if (t->interactivity > INTERACT_MAX - 1) {
            t->interactivity = INTERACT_MAX - 1;
        }
    }
    
    // Recalculate time slice based on interactivity
    t->time_slice = sched_calc_timeslice(t);
}

// Calculate time slice based on interactivity and priority
uint16_t sched_calc_timeslice(thread_t *t) {
    if (!t) return SLICE_MIN;
    
    switch (t->sched_class) {
        case SCHED_REALTIME:
            // RT threads get fixed short slices for responsiveness
            return SLICE_INTERACTIVE;
            
        case SCHED_TIMESHARE:
            // Interactive threads get shorter slices (more responsive)
            // CPU-bound threads get longer slices (less context switch overhead)
            if (t->interactivity > INTERACT_THRESH) {
                return SLICE_INTERACTIVE;
            } else if (t->interactivity < -INTERACT_THRESH) {
                return SLICE_BATCH;
            } else {
                // Middle ground
                return (SLICE_INTERACTIVE + SLICE_BATCH) / 2;
            }
            
        case SCHED_IDLE:
        default:
            return SLICE_MAX;
    }
}

// Check if thread is considered interactive
int sched_is_interactive(thread_t *t) {
    if (!t) return 0;
    return t->interactivity > INTERACT_THRESH;
}

// Get priority boost for interactive thread
int sched_interactivity_boost(thread_t *t) {
    if (!t || t->sched_class != SCHED_TIMESHARE) return 0;
    
    // Interactive threads get a priority boost (lower number = higher priority)
    if (t->interactivity > INTERACT_THRESH) {
        // Up to 5 priority levels boost
        return -(t->interactivity - INTERACT_THRESH) / 20;
    }
    
    // CPU-bound threads get a priority penalty
    if (t->interactivity < -INTERACT_THRESH) {
        return (-t->interactivity - INTERACT_THRESH) / 20;
    }
    
    return 0;
}
