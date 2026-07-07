/*
 * turnstile.c - Turnstile Implementation for Priority Inheritance
 * 
 * Prevents priority inversion by boosting lock holder's priority.
 * Based on Solaris/FreeBSD turnstile design.
 */

#include <stdint.h>
#include <string.h>

#include <sys/proc.h>
#include <kern/panic.h>
#include <kern/turnstile.h>

// Turnstile structure
typedef struct turnstile {
    void *ts_lockobj;           // Lock object this turnstile is for
    thread_t *ts_owner;         // Thread currently holding the lock
    thread_t *ts_waiters;       // List of threads waiting
    int ts_waiter_count;        // Number of waiters
    int ts_inherited_prio;      // Inherited priority (highest of waiters)
    struct turnstile *ts_next;  // Hash chain link
} turnstile_t;

// Turnstile hash table
#define TURNSTILE_HASH_SIZE 256
static turnstile_t *turnstile_hash[TURNSTILE_HASH_SIZE];
static volatile uint32_t turnstile_lock = 0;

// Pre-allocated turnstile pool
#define TURNSTILE_POOL_SIZE 128
static turnstile_t turnstile_pool[TURNSTILE_POOL_SIZE];
static int turnstile_pool_next = 0;
/* Free list for recycled turnstiles */
static turnstile_t *turnstile_free_list = NULL;

// Hash function for lock objects
static inline int turnstile_hash_func(void *lockobj) {
    return((uintptr_t)lockobj >> 3) % TURNSTILE_HASH_SIZE;
}

// Lock the turnstile subsystem
static inline void ts_lock(void) {
    while (__sync_lock_test_and_set(&turnstile_lock, 1)) {
        while (turnstile_lock)
            __asm__ volatile("pause");
    }
}

// Unlock the turnstile subsystem
static inline void ts_unlock(void) {
    __sync_lock_release(&turnstile_lock);
}

// Allocate a turnstile
static turnstile_t *turnstile_alloc(void) {
    /* Try free list first (recycled entries) */
    if (turnstile_free_list) {
        turnstile_t *ts = turnstile_free_list;
        turnstile_free_list = ts->ts_next;
        memset(ts, 0, sizeof(*ts));
        return ts;
    }
    if (turnstile_pool_next >= TURNSTILE_POOL_SIZE)
        return(NULL);
    turnstile_t *ts = &turnstile_pool[turnstile_pool_next++];
    memset(ts, 0, sizeof(*ts));
    return(ts);
}

// Return a turnstile to the free list
static void turnstile_free_entry(turnstile_t *ts) {
    ts->ts_next = turnstile_free_list;
    turnstile_free_list = ts;
}

// Find turnstile for a lock object
static turnstile_t *turnstile_lookup(void *lockobj) {
    int hash = turnstile_hash_func(lockobj);
    turnstile_t *ts = turnstile_hash[hash];
    while (ts) {
        if (ts->ts_lockobj == lockobj)
            return(ts);
        ts = ts->ts_next;
    }
    return(NULL);
}

// Insert turnstile into hash table
static void turnstile_insert(turnstile_t *ts) {
    int hash = turnstile_hash_func(ts->ts_lockobj);
    ts->ts_next = turnstile_hash[hash];
    turnstile_hash[hash] = ts;
}

// Remove turnstile from hash table
static void turnstile_remove(turnstile_t *ts) {
    int hash = turnstile_hash_func(ts->ts_lockobj);
    turnstile_t **pp = &turnstile_hash[hash];
    while (*pp) {
        if (*pp == ts) {
            *pp = ts->ts_next;
            ts->ts_next = NULL;
            return;
        }
        pp = &(*pp)->ts_next;
    }
}

// Initialize turnstile subsystem
void turnstile_init(void) {
    memset(turnstile_hash, 0, sizeof(turnstile_hash));
    turnstile_pool_next = 0;
}

// Called when thread blocks on a lock
// lockobj: the lock being waited on
// owner: the thread currently holding the lock
void turnstile_block(void *lockobj, thread_t *owner) {
    if (!current_thread || !owner)
        return;
    
    ts_lock();
    
    // Find or create turnstile
    turnstile_t *ts = turnstile_lookup(lockobj);
    if (!ts) {
        ts = turnstile_alloc();
        if (!ts) {
            ts_unlock();
            panic("turnstile_block: pool exhausted");
            return;
        }
        ts->ts_lockobj = lockobj;
        ts->ts_owner = owner;
        turnstile_insert(ts);
    }
    
    // Add current thread to waiters
    current_thread->next = ts->ts_waiters;
    ts->ts_waiters = current_thread;
    ts->ts_waiter_count++;
    
    // Priority Inheritance: boost owner to highest waiter priority
    // Within timeshare class: lower number = higher priority
    if (current_thread->sched_class == SCHED_TIMESHARE && 
        owner->sched_class == SCHED_TIMESHARE) {
        if (current_thread->priority < owner->priority) {
            // Waiter has higher priority than owner - inherit it
            if (ts->ts_inherited_prio == 0 || 
                current_thread->priority < ts->ts_inherited_prio) {
                ts->ts_inherited_prio = current_thread->priority;
                owner->priority = current_thread->priority;
            }
        }
    }
    
    ts_unlock();
}

// Called when lock holder releases lock.
//
// The turnstile exists ONLY for priority inheritance.  It deliberately does
// NOT wake waiters: every lockmgr() waiter that calls turnstile_block() also
// enqueues itself on the lock's sleepq (sleepq_add), and every
// turnstile_release() caller immediately follows with sleepq_wake_all() — the
// authoritative wake path, which already guards THREAD_STOPPED *and*
// THREAD_ZOMBIE before flipping a waiter to THREAD_READY.
//
// Historically this function ALSO walked ts_waiters and forced each to
// THREAD_READY.  That was redundant with the sleepq wake AND unsafe:
//   - turnstile_block() links waiters through thread->next, the SAME field
//     sleepq_add() then overwrites, so the ts_waiters chain is aliased/stale.
//   - there is no turnstile_remove_thread(), so proc_exit() (which pulls an
//     exiting thread off its sleepqs, sys/pm/process.c) leaves a dying thread
//     on ts_waiters.  A later release then flipped a THREAD_ZOMBIE waiter back
//     to READY (this walk lacked the ZOMBIE guard) or, once wait4() had reaped
//     it, wrote through a FREED thread_t.  Either way a dead/garbage thread
//     became THREAD_READY; the scheduler backstop only catches proc->state ==
//     SZOMB (not the SDYING window), so it got picked and arch_switch_to()
//     returned into a stale kernel stack / proc_exit()'s preempt-disabled
//     post-yield spin — the silent full-suite-only wedge at OPTS
//     signals/sigaction/9-1.  Leaving the wake to sleepq_wake_all() removes
//     both hazards.
void turnstile_release(void *lockobj) {
    ts_lock();

    turnstile_t *ts = turnstile_lookup(lockobj);
    if (!ts) {
        ts_unlock();
        return;
    }

    // Restore original priority if we inherited.  ts_owner is the releasing
    // thread (the lock holder) running this code, so it is live.
    if (ts->ts_owner && ts->ts_inherited_prio != 0)
        ts->ts_owner->priority = ts->ts_owner->base_priority;

    // Remove and recycle turnstile.  Waiters are woken by the sleepq_wake_all()
    // lockmgr() issues immediately after this call; we intentionally do NOT
    // touch waiter->state / waiter->next here (see comment above).
    turnstile_remove(ts);
    turnstile_free_entry(ts);

    ts_unlock();
}

// Get inherited priority for a thread
int turnstile_get_inherited_priority(thread_t *t) {
    if (!t)
        return(0);
    
    ts_lock();
    
    // Find any turnstile where this thread is owner
    for (int i = 0; i < TURNSTILE_HASH_SIZE; i++) {
        turnstile_t *ts = turnstile_hash[i];
        while (ts) {
            if (ts->ts_owner == t && ts->ts_inherited_prio != 0) {
                int prio = ts->ts_inherited_prio;
                ts_unlock();
                return(prio);
            }
            ts = ts->ts_next;
        }
    }
    
    ts_unlock();
    return(0);
}
