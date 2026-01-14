/*
 * sleepq.c - Hashed Sleep Queues
 * 
 * O(1) lookup for sleep/wakeup operations.
 * Based on FreeBSD/Solaris sleep queue design.
 */

#include <sys/proc.h>
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
        while (sleepq_locks[hash]) __asm__ volatile("pause");
    }
}

// Unlock a hash bucket
static inline void sq_unlock(int hash) {
    __sync_lock_release(&sleepq_locks[hash]);
}

// Allocate a sleep queue
static sleepq_t *sleepq_alloc(void) {
    while (__sync_lock_test_and_set(&pool_lock, 1)) {
        while (pool_lock) __asm__ volatile("pause");
    }
    
    sleepq_t *sq = NULL;
    if (sleepq_pool_next < SLEEPQ_POOL_SIZE) {
        sq = &sleepq_pool[sleepq_pool_next++];
        memset(sq, 0, sizeof(*sq));
    }
    
    __sync_lock_release(&pool_lock);
    return sq;
}

// Find sleep queue for a channel (must hold bucket lock)
static sleepq_t *sleepq_lookup(void *chan, int hash) {
    sleepq_t *sq = sleepq_hash[hash];
    while (sq) {
        if (sq->sq_chan == chan) return sq;
        sq = sq->sq_next;
    }
    return NULL;
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

// Add a thread to sleep queue
void sleepq_add(void *chan, thread_t *t) {
    if (!chan || !t) return;
    
    int hash = sleepq_hash_func(chan);
    sq_lock(hash);
    
    // Find or create sleep queue
    sleepq_t *sq = sleepq_lookup(chan, hash);
    if (!sq) {
        sq = sleepq_alloc();
        if (!sq) {
            sq_unlock(hash);
            return;  // Out of sleep queues
        }
        sq->sq_chan = chan;
        sleepq_insert(sq, hash);
    }
    
    // Add thread to tail (FIFO order)
    t->next = NULL;
    if (sq->sq_tail) {
        sq->sq_tail->next = t;
    } else {
        sq->sq_head = t;
    }
    sq->sq_tail = t;
    sq->sq_count++;
    
    // Mark thread as blocked
    t->wait_chan = chan;
    t->state = THREAD_BLOCKED;
    
    sq_unlock(hash);
}

// Wake one thread from sleep queue
// Returns: woken thread, or NULL if no waiters
thread_t *sleepq_wake_one(void *chan) {
    if (!chan) return NULL;
    
    int hash = sleepq_hash_func(chan);
    sq_lock(hash);
    
    sleepq_t *sq = sleepq_lookup(chan, hash);
    if (!sq || sq->sq_count == 0) {
        sq_unlock(hash);
        return NULL;
    }
    
    // Remove head thread (FIFO)
    thread_t *t = sq->sq_head;
    sq->sq_head = t->next;
    if (!sq->sq_head) {
        sq->sq_tail = NULL;
    }
    sq->sq_count--;
    
    // Clear wait state
    t->next = NULL;
    t->wait_chan = NULL;
    t->state = THREAD_READY;
    
    // Remove sleep queue if empty
    if (sq->sq_count == 0) {
        sleepq_remove(sq, hash);
        // Could return sq to pool here
    }
    
    sq_unlock(hash);
    return t;
}

// Wake all threads from sleep queue
// Returns: number of threads woken
int sleepq_wake_all(void *chan) {
    if (!chan) return 0;
    
    int hash = sleepq_hash_func(chan);
    sq_lock(hash);
    
    sleepq_t *sq = sleepq_lookup(chan, hash);
    if (!sq || sq->sq_count == 0) {
        sq_unlock(hash);
        return 0;
    }
    
    int woken = 0;
    thread_t *t = sq->sq_head;
    while (t) {
        thread_t *next = t->next;
        t->next = NULL;
        t->wait_chan = NULL;
        t->state = THREAD_READY;
        woken++;
        t = next;
    }
    
    // Remove sleep queue
    sleepq_remove(sq, hash);
    // Could return sq to pool
    
    sq_unlock(hash);
    return woken;
}

// Wake up to N threads from sleep queue
int sleepq_wake_n(void *chan, int n) {
    if (!chan || n == 0) return 0;
    if (n < 0) return sleepq_wake_all(chan);
    
    int hash = sleepq_hash_func(chan);
    sq_lock(hash);
    
    sleepq_t *sq = sleepq_lookup(chan, hash);
    if (!sq || sq->sq_count == 0) {
        sq_unlock(hash);
        return 0;
    }
    
    int woken = 0;
    while (sq->sq_head && woken < n) {
        thread_t *t = sq->sq_head;
        sq->sq_head = t->next;
        if (!sq->sq_head) sq->sq_tail = NULL;
        sq->sq_count--;
        
        t->next = NULL;
        t->wait_chan = NULL;
        t->state = THREAD_READY;
        woken++;
    }
    
    // Remove sleep queue if empty
    if (sq->sq_count == 0) {
        sleepq_remove(sq, hash);
    }
    
    sq_unlock(hash);
    return woken;
}

// Check if any threads are waiting on a channel
int sleepq_has_waiters(void *chan) {
    if (!chan) return 0;
    
    int hash = sleepq_hash_func(chan);
    sq_lock(hash);
    
    sleepq_t *sq = sleepq_lookup(chan, hash);
    int has = (sq && sq->sq_count > 0);
    
    sq_unlock(hash);
    return has;
}
