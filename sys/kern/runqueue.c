/*
 * runqueue.c - Multilevel Feedback Queue Implementation
 * 
 * O(1) scheduler with separate queues for RT, Timeshare, and Idle classes.
 */

#include <kern/runqueue.h>
#include <sys/lock.h>
#include <string.h>

// Find first set bit (1-indexed, 0 if none)
static inline int ffs64(uint64_t x) {
#ifdef __GNUC__
    uint32_t lo = (uint32_t)x;
    if (lo) return __builtin_ffs(lo);
    uint32_t hi = (uint32_t)(x >> 32);
    if (hi) return __builtin_ffs(hi) + 32;
    return 0;
#else
    if (x == 0) return 0;
    int n = 1;
    if ((x & 0xFFFFFFFF) == 0) { n += 32; x >>= 32; }
    if ((x & 0xFFFF) == 0) { n += 16; x >>= 16; }
    if ((x & 0xFF) == 0) { n += 8; x >>= 8; }
    if ((x & 0xF) == 0) { n += 4; x >>= 4; }
    if ((x & 0x3) == 0) { n += 2; x >>= 2; }
    if ((x & 0x1) == 0) { n += 1; }
    return n;
#endif
}

void runqueue_init(runqueue_t *rq, uint32_t cpu_id) {
    memset(rq, 0, sizeof(*rq));
    rq->cpu_id = cpu_id;
    spinlock_init(&rq->lock, "runqueue");
    
    for (int i = 0; i < RQ_TOTAL_LEVELS; i++) {
        rq->queues[i].head = NULL;
        rq->queues[i].tail = NULL;
        rq->queues[i].count = 0;
    }
}

// Map thread to queue level
int runqueue_level_for_thread(thread_t *t) {
    switch (t->sched_class) {
        case SCHED_REALTIME:
            // RT priority 0-31 maps to levels 0-31
            // Higher priority number = higher priority = lower level index
            return RQ_REALTIME_BASE + (31 - (t->priority & 31));
            
        case SCHED_TIMESHARE:
            // Nice -20 to +19 maps to levels 32-71
            // Lower nice = higher priority = lower level index
            // priority is stored as 0-39, where 0 is highest
            return RQ_TIMESHARE_BASE + (t->priority & 39);
            
        case SCHED_IDLE:
        default:
            return RQ_IDLE_BASE;
    }
}

// Set bitmap bit for level
static inline void bitmap_set(runqueue_t *rq, int level) {
    if (level < 64) {
        rq->bitmap_lo |= (1ULL << level);
    } else {
        rq->bitmap_hi |= (1ULL << (level - 64));
    }
}

// Clear bitmap bit for level
static inline void bitmap_clear(runqueue_t *rq, int level) {
    if (level < 64) {
        rq->bitmap_lo &= ~(1ULL << level);
    } else {
        rq->bitmap_hi &= ~(1ULL << (level - 64));
    }
}

void runqueue_add(runqueue_t *rq, thread_t *t) {
    int level = runqueue_level_for_thread(t);
    runqueue_level_t *q = &rq->queues[level];
    
    // Add to tail of queue (FIFO within priority)
    t->rq_next = NULL;
    t->rq_prev = q->tail;
    t->current_queue = rq;
    
    if (q->tail) {
        q->tail->rq_next = t;
    } else {
        q->head = t;
    }
    q->tail = t;
    q->count++;
    
    // Update bitmap
    bitmap_set(rq, level);
    
    // Update stats
    rq->total_threads++;
    if (t->sched_class == SCHED_REALTIME) rq->realtime_count++;
    else if (t->sched_class == SCHED_TIMESHARE) rq->timeshare_count++;
    else rq->idle_count++;
}

void runqueue_remove(runqueue_t *rq, thread_t *t) {
    int level = runqueue_level_for_thread(t);
    runqueue_level_t *q = &rq->queues[level];
    
    // Unlink from queue
    if (t->rq_prev) {
        t->rq_prev->rq_next = t->rq_next;
    } else {
        q->head = t->rq_next;
    }
    
    if (t->rq_next) {
        t->rq_next->rq_prev = t->rq_prev;
    } else {
        q->tail = t->rq_prev;
    }
    
    t->rq_next = NULL;
    t->rq_prev = NULL;
    t->current_queue = NULL;
    q->count--;
    
    // Clear bitmap if queue empty
    if (q->count == 0) {
        bitmap_clear(rq, level);
    }
    
    // Update stats
    rq->total_threads--;
    if (t->sched_class == SCHED_REALTIME) rq->realtime_count--;
    else if (t->sched_class == SCHED_TIMESHARE) rq->timeshare_count--;
    else rq->idle_count--;
}

thread_t *runqueue_peek(runqueue_t *rq) {
    // Find first non-empty queue (lowest level = highest priority)
    int level = ffs64(rq->bitmap_lo);
    if (level == 0) {
        level = ffs64(rq->bitmap_hi);
        if (level == 0) return NULL;
        level += 64;
    }
    level--;  // ffs is 1-indexed
    
    return rq->queues[level].head;
}

thread_t *runqueue_pop(runqueue_t *rq) {
    thread_t *t = runqueue_peek(rq);
    if (t) {
        runqueue_remove(rq, t);
    }
    return t;
}

uint32_t runqueue_count(runqueue_t *rq) {
    return rq->total_threads;
}
