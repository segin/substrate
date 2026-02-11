/*
 * sleepq.c - Hashed Sleep Queues
 * 
 * O(1) lookup for sleep/wakeup operations.
 * Based on FreeBSD/Solaris sleep queue design.
 */

#include <sys/proc.h>
#include <kern/sleepq.h>
#include <stdint.h>
#include <string.h>

// Sleep queue hash table size (power of 2 for fast modulo)
#define SLEEPQ_HASH_SIZE 256
#define SLEEPQ_HASH_MASK (SLEEPQ_HASH_SIZE - 1)

// Sleep queue entry
typedef struct sleepq {
    void *sq_chan;              // Wait channel
    thread_t *sq_head;          // Head of waiter list
    thread_t *sq_tail;          // Tail of waiter list
    int sq_count;               // Number of waiters
    struct sleepq *sq_next;     // Hash chain link
    int type;                   // SLEEPQ_TYPE_SHARED or SLEEPQ_TYPE_PRIVATE
    int pid;                    // PID for private queues
} sleepq_t;

// Sleep queue hash table
static sleepq_t *sleepq_hash[SLEEPQ_HASH_SIZE];
// Per-bucket locks for scalability
static volatile uint32_t sleepq_locks[SLEEPQ_HASH_SIZE];

// Pre-allocated sleep queue pool
#define SLEEPQ_POOL_SIZE 128
static sleepq_t sleepq_pool[SLEEPQ_POOL_SIZE];
static int sleepq_pool_next = 0;
static volatile uint32_t pool_lock = 0;

// Hash function for wait channels
static inline int sleepq_hash_func(void *chan) {
    return ((uintptr_t)chan >> 3) & SLEEPQ_HASH_MASK;
}

// Lock a hash bucket
static inline void sq_lock(int hash) {
    while (__sync_lock_test_and_set(&sleepq_locks[hash], 1)) {
        while (sleepq_locks[hash])
            __asm__ volatile("pause");
    }
}

// Unlock a hash bucket
static inline void sq_unlock(int hash) {
    __sync_lock_release(&sleepq_locks[hash]);
}

// Allocate a sleep queue
static sleepq_t *sleepq_alloc(void) {
    while (__sync_lock_test_and_set(&pool_lock, 1)) {
        while (pool_lock)
            __asm__ volatile("pause");
    }
    
    sleepq_t *sq = NULL;
    if (sleepq_pool_next < SLEEPQ_POOL_SIZE) {
        sq = &sleepq_pool[sleepq_pool_next++];
        memset(sq, 0, sizeof(*sq));
    }
    
    __sync_lock_release(&pool_lock);
    return(sq);
}

// Find sleep queue for a channel (must hold bucket lock)
static sleepq_t *sleepq_lookup(void *chan, int hash, int type, int pid) {
    sleepq_t *sq = sleepq_hash[hash];
    while (sq) {
        if (sq->sq_chan == chan && sq->type == type) {
            if (type == SLEEPQ_TYPE_SHARED) return sq;
            if (sq->pid == pid) return sq;
        }
        sq = sq->sq_next;
    }
    return(NULL);
}

// Insert sleep queue into hash table
static void sleepq_insert(sleepq_t *sq, int hash) {
    sq->sq_next = sleepq_hash[hash];
    sleepq_hash[hash] = sq;
}

// Remove sleep queue from hash table
static void sleepq_remove(sleepq_t *sq, int hash) {
    sleepq_t **pp = &sleepq_hash[hash];
    while (*pp) {
        if (*pp == sq) {
            *pp = sq->sq_next;
            sq->sq_next = NULL;
            return;
        }
        pp = &(*pp)->sq_next;
    }
}

// Initialize sleep queue subsystem
void sleepq_init(void) {
    memset(sleepq_hash, 0, sizeof(sleepq_hash));
    memset((void*)sleepq_locks, 0, sizeof(sleepq_locks));
    sleepq_pool_next = 0;
}

// Internal helper to add thread
static void sleepq_add_internal(void *chan, thread_t *t, int type, int pid) {
    if (!chan || !t)
        return;
    
    int hash = sleepq_hash_func(chan);
    sq_lock(hash);
    
    // Find or create sleep queue
    sleepq_t *sq = sleepq_lookup(chan, hash, type, pid);
    if (!sq) {
        sq = sleepq_alloc();
        if (!sq) {
            sq_unlock(hash);
            return;  // Out of sleep queues
        }
        sq->sq_chan = chan;
        sq->type = type;
        sq->pid = pid;
        sleepq_insert(sq, hash);
    }
    
    // Add thread to tail (FIFO order)
    t->next = NULL;
    if (sq->sq_tail)
        sq->sq_tail->next = t;
    else
        sq->sq_head = t;
    
    sq->sq_tail = t;
    sq->sq_count++;
    
    // Mark thread as blocked
    t->wait_chan = chan;
    t->state = THREAD_BLOCKED;
    
    sq_unlock(hash);
}

// Add a thread to shared sleep queue
void sleepq_add(void *chan, thread_t *t) {
    sleepq_add_internal(chan, t, SLEEPQ_TYPE_SHARED, 0);
}

// Add a thread to private sleep queue
void sleepq_add_private(void *chan, thread_t *t) {
    if (!current_process) return;
    sleepq_add_internal(chan, t, SLEEPQ_TYPE_PRIVATE, current_process->pid);
}

// Wake thread(s) internal helper
static int sleepq_wake_internal(void *chan, int n, int type, int pid) {
    if (!chan || n == 0)
        return(0);

    int hash = sleepq_hash_func(chan);
    sq_lock(hash);
    
    sleepq_t *sq = sleepq_lookup(chan, hash, type, pid);
    if (!sq || sq->sq_count == 0) {
        sq_unlock(hash);
        return(0);
    }
    
    // Determine how many to wake
    int wake_all = (n < 0);
    int woken = 0;

    if (wake_all) {
        thread_t *t = sq->sq_head;
        while (t) {
            thread_t *next = t->next;
            t->next = NULL;
            t->wait_chan = NULL;
            t->state = THREAD_READY;
            woken++;
            t = next;
        }
    } else {
        while (sq->sq_head && woken < n) {
            thread_t *t = sq->sq_head;
            sq->sq_head = t->next;
            if (!sq->sq_head)
                sq->sq_tail = NULL;
            sq->sq_count--;

            t->next = NULL;
            t->wait_chan = NULL;
            t->state = THREAD_READY;
            woken++;
        }
    }
    
    // Remove sleep queue if empty
    if (sq->sq_count == 0 || wake_all) { // wake_all empties it completely
        sleepq_remove(sq, hash);
        // Could return sq to pool
    }
    
    sq_unlock(hash);
    return(woken);
}

// Wake one thread from shared sleep queue
thread_t *sleepq_wake_one(void *chan) {
    // Note: This API returns thread_t*, but internal helper returns count.
    // Existing API expects specific thread return.
    // We reimplement simplified version matching helper logic.

    if (!chan) return NULL;
    
    int hash = sleepq_hash_func(chan);
    sq_lock(hash);
    
    sleepq_t *sq = sleepq_lookup(chan, hash, SLEEPQ_TYPE_SHARED, 0);
    if (!sq || sq->sq_count == 0) {
        sq_unlock(hash);
        return NULL;
    }
    
    thread_t *t = sq->sq_head;
    sq->sq_head = t->next;
    if (!sq->sq_head)
        sq->sq_tail = NULL;
    sq->sq_count--;

    t->next = NULL;
    t->wait_chan = NULL;
    t->state = THREAD_READY;
    
    if (sq->sq_count == 0)
        sleepq_remove(sq, hash);

    sq_unlock(hash);
    return t;
}

// Wake all threads from shared sleep queue
int sleepq_wake_all(void *chan) {
    return sleepq_wake_internal(chan, -1, SLEEPQ_TYPE_SHARED, 0);
}

// Wake N threads from shared sleep queue
int sleepq_wake_n(void *chan, int n) {
    return sleepq_wake_internal(chan, n, SLEEPQ_TYPE_SHARED, 0);
}

// Wake N threads from private sleep queue
int sleepq_wake_n_private(void *chan, int n) {
    if (!current_process) return 0;
    return sleepq_wake_internal(chan, n, SLEEPQ_TYPE_PRIVATE, current_process->pid);
}

// Check if any threads are waiting on a channel
int sleepq_has_waiters(void *chan) {
    if (!chan)
        return(0);
    
    int hash = sleepq_hash_func(chan);
    sq_lock(hash);
    
    // We assume shared check for generic API, or we check both?
    // Existing API assumes shared.
    sleepq_t *sq = sleepq_lookup(chan, hash, SLEEPQ_TYPE_SHARED, 0);
    int has = (sq && sq->sq_count > 0);
    
    sq_unlock(hash);
    return(has);
}

// Requeue waiters from src_chan to dst_chan
// Supports both shared and private via flag.
int sleepq_requeue(void *src_chan, void *dst_chan, int wake_n, int requeue_n, int private_op) {
    if (!src_chan || !dst_chan)
        return(0);

    int type = private_op ? SLEEPQ_TYPE_PRIVATE : SLEEPQ_TYPE_SHARED;
    int pid = private_op && current_process ? current_process->pid : 0;
    
    int src_hash = sleepq_hash_func(src_chan);
    int dst_hash = sleepq_hash_func(dst_chan);
    
    // Lock ordering
    if (src_hash < dst_hash) {
        sq_lock(src_hash);
        sq_lock(dst_hash);
    } else if (src_hash > dst_hash) {
        sq_lock(dst_hash);
        sq_lock(src_hash);
    } else {
        sq_lock(src_hash);
    }
    
    // 1. Wake phase
    sleepq_t *src_sq = sleepq_lookup(src_chan, src_hash, type, pid);
    int woken_count = 0;
    
    if (src_sq && src_sq->sq_count > 0) {
        while (src_sq->sq_head && woken_count < wake_n) {
            thread_t *t = src_sq->sq_head;
            src_sq->sq_head = t->next;
            if (!src_sq->sq_head)
                src_sq->sq_tail = NULL;
            src_sq->sq_count--;
            
            t->next = NULL;
            t->wait_chan = NULL;
            t->state = THREAD_READY;
            woken_count++;
        }
    }
    
    // 2. Requeue phase
    if (src_sq && src_sq->sq_count > 0 && requeue_n > 0) {
        // Prepare destination queue
        sleepq_t *dst_sq = sleepq_lookup(dst_chan, dst_hash, type, pid);
        if (!dst_sq) {
            dst_sq = sleepq_alloc();
            if (dst_sq) {
                dst_sq->sq_chan = dst_chan;
                dst_sq->type = type;
                dst_sq->pid = pid;
                sleepq_insert(dst_sq, dst_hash);
            }
        }
        
        if (dst_sq) {
            int moved_count = 0;
            while (src_sq->sq_head && moved_count < requeue_n) {
                thread_t *t = src_sq->sq_head;
                src_sq->sq_head = t->next;
                if (!src_sq->sq_head)
                    src_sq->sq_tail = NULL;
                src_sq->sq_count--;
                
                // Add to dst queue
                t->next = NULL;
                if (dst_sq->sq_tail)
                    dst_sq->sq_tail->next = t;
                else
                    dst_sq->sq_head = t;
                
                dst_sq->sq_tail = t;
                dst_sq->sq_count++;
                
                // Update thread wait channel
                t->wait_chan = dst_chan;
                
                moved_count++;
            }
        }
    }
    
    // Cleanup empty src queue
    if (src_sq && src_sq->sq_count == 0)
        sleepq_remove(src_sq, src_hash);
    
    // Unlock
    if (src_hash != dst_hash) {
        sq_unlock(dst_hash);
        sq_unlock(src_hash);
    } else {
        sq_unlock(src_hash);
    }
    
    return(woken_count);
}
