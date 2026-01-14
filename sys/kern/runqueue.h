/*
 * runqueue.h - Multilevel Feedback Queue Runqueue Structures
 * 
 * Implements SCHED_REALTIME, SCHED_TIMESHARE, and SCHED_IDLE queues.
 */

#ifndef _KERN_RUNQUEUE_H
#define _KERN_RUNQUEUE_H

#include <sys/proc.h>
#include <stdint.h>

// Priority levels within each class
#define RQ_REALTIME_LEVELS  32      // RT priorities 0-31
#define RQ_TIMESHARE_LEVELS 40      // Nice -20 to +19 mapped to 0-39
#define RQ_IDLE_LEVELS      1       // Single idle queue

#define RQ_TOTAL_LEVELS     (RQ_REALTIME_LEVELS + RQ_TIMESHARE_LEVELS + RQ_IDLE_LEVELS)

// Queue indices
#define RQ_REALTIME_BASE    0
#define RQ_TIMESHARE_BASE   RQ_REALTIME_LEVELS
#define RQ_IDLE_BASE        (RQ_TIMESHARE_BASE + RQ_TIMESHARE_LEVELS)

// Single priority queue (doubly-linked list)
typedef struct runqueue_level {
    thread_t *head;
    thread_t *tail;
    uint32_t count;
} runqueue_level_t;

// Per-CPU runqueue
typedef struct runqueue {
    // Multilevel queues
    runqueue_level_t queues[RQ_TOTAL_LEVELS];
    
    // Bitmap for O(1) queue selection (64 bits covers 64 levels)
    uint64_t bitmap_lo;     // Levels 0-63
    uint64_t bitmap_hi;     // Levels 64-72 (unused currently)
    
    // Statistics
    uint32_t total_threads;
    uint32_t realtime_count;
    uint32_t timeshare_count;
    uint32_t idle_count;
    
    // Load tracking
    uint32_t load;          // Weighted load average
    uint64_t last_update;   // Timestamp of last load update
    
    // Lock (for SMP)
    volatile uint32_t lock;
    
    // CPU ID owning this runqueue
    uint32_t cpu_id;
} runqueue_t;

// Initialize a runqueue
void runqueue_init(runqueue_t *rq, uint32_t cpu_id);

// Add thread to runqueue
void runqueue_add(runqueue_t *rq, thread_t *t);

// Remove thread from runqueue
void runqueue_remove(runqueue_t *rq, thread_t *t);

// Get highest priority ready thread (does not remove)
thread_t *runqueue_peek(runqueue_t *rq);

// Get and remove highest priority ready thread
thread_t *runqueue_pop(runqueue_t *rq);

// Map thread to queue level based on class and priority
int runqueue_level_for_thread(thread_t *t);

// Get count of threads in runqueue
uint32_t runqueue_count(runqueue_t *rq);

// Check if runqueue is empty
static inline int runqueue_empty(runqueue_t *rq) {
    return rq->bitmap_lo == 0 && rq->bitmap_hi == 0;
}

#endif /* _KERN_RUNQUEUE_H */
