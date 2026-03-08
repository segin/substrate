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

#define SLEEPQ_TYPE_SHARED 0
#define SLEEPQ_TYPE_PRIVATE 1

// Sleep queue hash table size (power of 2 for fast modulo)
#define SLEEPQ_HASH_SIZE 256
#define SLEEPQ_HASH_MASK (SLEEPQ_HASH_SIZE - 1)

// Sleep queue entry
typedef struct sleepq {
    void *sq_chan;              // Wait channel
    int sq_type;                // SLEEPQ_TYPE_SHARED or SLEEPQ_TYPE_PRIVATE
    int sq_pid;                 // PID for private queues
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
static inline int sleepq_hash_func(void *chan, int type, int pid) {
    if (type == SLEEPQ_TYPE_PRIVATE) {
        /* Mix PID into hash to distribute private futexes */
        return (((uintptr_t)chan >> 3) ^ pid) & SLEEPQ_HASH_MASK;
    }
    return ((uintptr_t)chan >> 3) & SLEEPQ_HASH_MASK;
}

static inline int sleepq_current_private_pid(void) {
    if (!current_process) {
        return -1;
    }
    return current_process->pid;
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
static sleepq_t *sleepq_lookup(void *chan, int type, int pid, int hash) {
    sleepq_t *sq = sleepq_hash[hash];
    while (sq) {
        if (sq->sq_chan == chan && sq->sq_type == type) {
            if (type == SLEEPQ_TYPE_SHARED) return sq;
            if (sq->sq_pid == pid) return sq;
        }
        sq = sq->sq_next;
    }
    return NULL;
}

static void sleepq_remove(sleepq_t *sq, int hash);

static int sleepq_remove_thread_internal(thread_t *t, int type, int pid) {
    void *chan;
    int hash;
    sleepq_t *sq;
    thread_t *prev;

    if (!t || !t->wait_chan) {
        return 0;
    }

    chan = t->wait_chan;
    hash = sleepq_hash_func(chan, type, pid);
    sq_lock(hash);

    sq = sleepq_lookup(chan, type, pid, hash);
    if (!sq) {
        sq_unlock(hash);
        return 0;
    }

    prev = NULL;
    thread_t *cur = sq->sq_head;
    while (cur) {
        if (cur == t) {
            if (prev) {
                prev->next = cur->next;
            } else {
                sq->sq_head = cur->next;
            }
            if (sq->sq_tail == cur) {
                sq->sq_tail = prev;
            }
            sq->sq_count--;
            cur->next = NULL;
            cur->wait_chan = NULL;
            if (sq->sq_count == 0) {
                sleepq_remove(sq, hash);
            }
            sq_unlock(hash);
            return 1;
        }
        prev = cur;
        cur = cur->next;
    }

    sq_unlock(hash);
    return 0;
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
    
    int hash = sleepq_hash_func(chan, type, pid);
    sq_lock(hash);
    
    // Find or create sleep queue
    sleepq_t *sq = sleepq_lookup(chan, type, pid, hash);
    if (!sq) {
        sq = sleepq_alloc();
        if (!sq) {
            sq_unlock(hash);
            return;  // Out of sleep queues
        }
        sq->sq_chan = chan;
        sq->sq_type = type;
        sq->sq_pid = pid;
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
    int pid = sleepq_current_private_pid();
    if (pid < 0) return;
    sleepq_add_internal(chan, t, SLEEPQ_TYPE_PRIVATE, pid);
}

// Wake thread(s) internal helper
static thread_t *sleepq_wake_one_internal(void *chan, int type, int pid) {
    if (!chan)
        return(NULL);
    
    int hash = sleepq_hash_func(chan, type, pid);
    sq_lock(hash);
    
    sleepq_t *sq = sleepq_lookup(chan, type, pid, hash);
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
    
    // Remove sleep queue if empty
    if (sq->sq_count == 0) {
        sleepq_remove(sq, hash);
    }
    
    sq_unlock(hash);
    return(t);
}

thread_t *sleepq_wake_one(void *chan) {
    return sleepq_wake_one_internal(chan, SLEEPQ_TYPE_SHARED, 0);
}

thread_t *sleepq_wake_one_private(void *chan) {
    int pid = sleepq_current_private_pid();
    if (pid < 0) return NULL;
    return sleepq_wake_one_internal(chan, SLEEPQ_TYPE_PRIVATE, pid);
}

static int sleepq_wake_all_internal(void *chan, int type, int pid) {
    if (!chan)
        return(0);
    
    int hash = sleepq_hash_func(chan, type, pid);
    sq_lock(hash);
    
    sleepq_t *sq = sleepq_lookup(chan, type, pid, hash);
    if (!sq || sq->sq_count == 0) {
        sq_unlock(hash);
        return(0);
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
    
    sq_unlock(hash);
    return(woken);
}

int sleepq_wake_all(void *chan) {
    return sleepq_wake_all_internal(chan, SLEEPQ_TYPE_SHARED, 0);
}

int sleepq_wake_all_private(void *chan) {
    int pid = sleepq_current_private_pid();
    if (pid < 0) return 0;
    return sleepq_wake_all_internal(chan, SLEEPQ_TYPE_PRIVATE, pid);
}

static int sleepq_wake_n_internal(void *chan, int n, int type, int pid) {
    if (!chan || n == 0)
        return(0);
    if (n < 0)
        return(sleepq_wake_all_internal(chan, type, pid));
    
    int hash = sleepq_hash_func(chan, type, pid);
    sq_lock(hash);
    
    sleepq_t *sq = sleepq_lookup(chan, type, pid, hash);
    if (!sq || sq->sq_count == 0) {
        sq_unlock(hash);
        return(0);
    }
    
    int woken = 0;
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
    
    // Remove sleep queue if empty
    if (sq->sq_count == 0)
        sleepq_remove(sq, hash);
    
    sq_unlock(hash);
    return(woken);
}

int sleepq_wake_n(void *chan, int n) {
    return sleepq_wake_n_internal(chan, n, SLEEPQ_TYPE_SHARED, 0);
}

int sleepq_wake_n_private(void *chan, int n) {
    int pid = sleepq_current_private_pid();
    if (pid < 0) return 0;
    return sleepq_wake_n_internal(chan, n, SLEEPQ_TYPE_PRIVATE, pid);
}

static int sleepq_has_waiters_internal(void *chan, int type, int pid) {
    if (!chan)
        return(0);
    
    int hash = sleepq_hash_func(chan, type, pid);
    sq_lock(hash);
    
    sleepq_t *sq = sleepq_lookup(chan, type, pid, hash);
    int has = (sq && sq->sq_count > 0);
    
    sq_unlock(hash);
    return(has);
}

int sleepq_has_waiters(void *chan) {
    return sleepq_has_waiters_internal(chan, SLEEPQ_TYPE_SHARED, 0);
}

int sleepq_has_waiters_private(void *chan) {
    int pid = sleepq_current_private_pid();
    if (pid < 0) return 0;
    return sleepq_has_waiters_internal(chan, SLEEPQ_TYPE_PRIVATE, pid);
}

static int sleepq_requeue_internal(void *src_chan, void *dst_chan, int wake_n, int requeue_n, int type, int pid) {
    if (!src_chan || !dst_chan)
        return(0);
    
    int src_hash = sleepq_hash_func(src_chan, type, pid);
    int dst_hash = sleepq_hash_func(dst_chan, type, pid);
    
    // Lock ordering to prevent deadlock (lower hash first)
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
    sleepq_t *src_sq = sleepq_lookup(src_chan, type, pid, src_hash);
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
        sleepq_t *dst_sq = sleepq_lookup(dst_chan, type, pid, dst_hash);
        if (!dst_sq) {
            dst_sq = sleepq_alloc();
            if (dst_sq) {
                dst_sq->sq_chan = dst_chan;
                dst_sq->sq_type = type;
                dst_sq->sq_pid = pid;
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

int sleepq_requeue(void *src_chan, void *dst_chan, int wake_n, int requeue_n) {
    return sleepq_requeue_internal(src_chan, dst_chan, wake_n, requeue_n, SLEEPQ_TYPE_SHARED, 0);
}

int sleepq_requeue_private(void *src_chan, void *dst_chan, int wake_n, int requeue_n) {
    int pid = sleepq_current_private_pid();
    if (pid < 0) return 0;
    return sleepq_requeue_internal(src_chan, dst_chan, wake_n, requeue_n, SLEEPQ_TYPE_PRIVATE, pid);
}

int sleepq_remove_thread(thread_t *t) {
    int pid;

    if (!t || !t->wait_chan) {
        return 0;
    }

    if (sleepq_remove_thread_internal(t, SLEEPQ_TYPE_SHARED, 0)) {
        return 1;
    }

    pid = (t->proc) ? t->proc->pid : -1;
    if (pid < 0) {
        return 0;
    }

    return sleepq_remove_thread_internal(t, SLEEPQ_TYPE_PRIVATE, pid);
}
