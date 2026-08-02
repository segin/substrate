/*
 * turnstile.c - Turnstile Implementation for Priority Inheritance
 * 
 * Prevents priority inversion by boosting lock holder's priority.
 * Based on Solaris/FreeBSD turnstile design.
 */

#include <stdint.h>
#include <string.h>

#include <kern/panic.h>
#include <kern/turnstile.h>
#include <sys/preempt.h>
#include <sys/proc.h>

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
//
// A70: disable preemption for the whole (short, non-sleeping) critical section
// (preempt.h contract: a held spinlock must keep preempt_count != 0).  A raw
// test_and_set that left preempt_count at 0 could be preempted by the timer
// tick while holding turnstile_lock; a peer that then spins here (e.g. a
// lockmgr release path) waits on a holder the scheduler switched away from —
// on a single CPU the holder never runs again (hard livelock).  Turnstiles are
// never touched from hard-interrupt context, so IRQ masking is not required.
static inline void ts_lock(void) {
    preempt_disable();
    while (__sync_lock_test_and_set(&turnstile_lock, 1)) {
        while (turnstile_lock)
            __asm__ volatile("pause");
    }
}

// Unlock the turnstile subsystem
static inline void ts_unlock(void) {
    __sync_lock_release(&turnstile_lock);
    preempt_enable_noresched();
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
            /*
             * A43: turnstile pool exhausted under extreme lock contention.
             * The turnstile exists ONLY for priority inheritance; the waiter
             * still enqueues on the lock's sleepq (in the caller, right after
             * this returns) and is woken normally.  Skip the PI boost for this
             * contended lock rather than panicking, so heavy-but-legitimate
             * filesystem contention (>128 distinct contended locks at once)
             * degrades gracefully instead of taking down the whole kernel.
             */
            ts_unlock();
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

    // A69: if this turnstile carried an inherited boost, recompute the owner's
    // priority from any OTHER turnstiles it still owns instead of dropping it
    // straight to base_priority.  Releasing one contended lock must not discard
    // a boost still owed for a different lock the same thread continues to hold
    // — doing so reintroduces the unbounded priority inversion turnstiles exist
    // to prevent.  ts_owner is the releasing thread running this code, so it is
    // live.
    thread_t *owner = ts->ts_owner;
    int had_boost = (ts->ts_inherited_prio != 0);

    // Remove and recycle turnstile first, so the recompute below does not count
    // the turnstile we are releasing.  Waiters are woken by the
    // sleepq_wake_all() lockmgr() issues immediately after this call; we
    // intentionally do NOT touch waiter->state / waiter->next here (see comment
    // above).
    turnstile_remove(ts);
    turnstile_free_entry(ts);

    if (owner && had_boost) {
        // Lower number = higher priority within SCHED_TIMESHARE; start at base
        // and keep the strongest boost still owed by a remaining turnstile.
        int best = owner->base_priority;
        for (int i = 0; i < TURNSTILE_HASH_SIZE; i++) {
            turnstile_t *t = turnstile_hash[i];
            while (t) {
                if (t->ts_owner == owner && t->ts_inherited_prio != 0 &&
                    t->ts_inherited_prio < best)
                    best = t->ts_inherited_prio;
                t = t->ts_next;
            }
        }
        owner->priority = best;
    }

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
