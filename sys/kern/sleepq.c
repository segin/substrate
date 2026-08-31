/*
 * sleepq.c - Hashed Sleep Queues
 * 
 * O(1) lookup for sleep/wakeup operations.
 * Based on FreeBSD/Solaris sleep queue design.
 */

#include <stdint.h>
#include <string.h>

#include <kern/sched.h>
#include <kern/sleepq.h>
#include <sys/preempt.h>
#include <sys/proc.h>
#include <vm/vm_kmem.h>

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

/*
 * Sleepq nodes are kmalloc'd on demand.  Freed nodes go on a global
 * free list (protected by pool_lock) so that the typical "park, wake,
 * park again" cycle re-uses storage instead of pounding kmalloc.  The
 * free list has no cap — kfree drains it in proportion to system
 * pressure if we ever start trimming.
 */
static sleepq_t *sleepq_free_list = NULL;
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

// Lock a hash bucket.
//
// IRQ-SAFE (KERN-01): psignal() runs in hard-interrupt context (the timer
// tick delivering SIGALRM, a TTY ^C from the keyboard IRQ) and reaches
// sleepq_remove_thread() -> sq_lock().  If a bucket could be held with local
// IRQs enabled, an interrupt landing on the CPU that already owns that bucket
// would spin here forever waiting for a holder that can never run (the ISR
// preempted it) — a hard CPU lockup.  Disabling local IRQs for the whole
// (short, non-sleeping) critical section guarantees no ISR ever lands while
// this CPU holds a bucket, so the ISR-context acquirer can never collide with
// the interrupted holder.  Returns the caller's saved IRQ state, restored by
// the matching sq_unlock(); nested acquisitions (sleepq_requeue) unwind in
// reverse order so the flags stack correctly.
//
// Disable preemption too (preempt.h contract: a held spinlock must keep
// preempt_count != 0 so the timer-IRQ preemption path won't switch away from
// the holder).  These buckets were raw test_and_set spinlocks that did NOT
// raise preempt_count — so once kernel preemption was enabled a thread could
// be preempted while holding a sleepq bucket, and a peer that then needed the
// same bucket (pipe/mutex ping-pong, etc.) spun on it while the holder sat
// un-runnable: the intermittent pipe/mutex/pty hang.
static inline unsigned long sq_lock(int hash) {
    unsigned long flags;
    __asm__ volatile("pushf; pop %0; cli" : "=r"(flags) :: "memory");
    preempt_disable();
    while (__sync_lock_test_and_set(&sleepq_locks[hash], 1)) {
        while (sleepq_locks[hash])
            __asm__ volatile("pause");
    }
    return flags;
}

// Unlock a hash bucket (restores the IRQ state saved by sq_lock)
static inline void sq_unlock(int hash, unsigned long flags) {
    __sync_lock_release(&sleepq_locks[hash]);
    preempt_enable_noresched();
    __asm__ volatile("push %0; popf" :: "r"(flags) : "memory", "cc");
}

// Allocate a sleep queue (free list first, then kmalloc)
static sleepq_t *sleepq_alloc(void) {
    sleepq_t *sq;

    preempt_disable();
    while (__sync_lock_test_and_set(&pool_lock, 1)) {
        while (pool_lock)
            __asm__ volatile("pause");
    }
    if (sleepq_free_list) {
        sq = sleepq_free_list;
        sleepq_free_list = sq->sq_next;
        __sync_lock_release(&pool_lock);
        preempt_enable_noresched();
        memset(sq, 0, sizeof(*sq));
        return sq;
    }
    __sync_lock_release(&pool_lock);
    preempt_enable_noresched();

    sq = kmalloc(sizeof(*sq));
    if (sq) {
        memset(sq, 0, sizeof(*sq));
    }
    return sq;
}

// Return a sleep queue to the free list
static void sleepq_free(sleepq_t *sq) {
    preempt_disable();
    while (__sync_lock_test_and_set(&pool_lock, 1)) {
        while (pool_lock)
            __asm__ volatile("pause");
    }
    sq->sq_next = sleepq_free_list;
    sleepq_free_list = sq;
    __sync_lock_release(&pool_lock);
    preempt_enable_noresched();
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
    unsigned long f = sq_lock(hash);

    sq = sleepq_lookup(chan, type, pid, hash);
    if (!sq) {
        sq_unlock(hash, f);
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
            sq_unlock(hash, f);
            return 1;
        }
        prev = cur;
        cur = cur->next;
    }

    sq_unlock(hash, f);
    return 0;
}

// Insert sleep queue into hash table
static void sleepq_insert(sleepq_t *sq, int hash) {
    sq->sq_next = sleepq_hash[hash];
    sleepq_hash[hash] = sq;
}

// Remove sleep queue from hash table and recycle
static void sleepq_remove(sleepq_t *sq, int hash) {
    sleepq_t **pp = &sleepq_hash[hash];
    while (*pp) {
        if (*pp == sq) {
            *pp = sq->sq_next;
            sq->sq_next = NULL;
            sleepq_free(sq);
            return;
        }
        pp = &(*pp)->sq_next;
    }
}

// Initialize sleep queue subsystem
void sleepq_init(void) {
    memset(sleepq_hash, 0, sizeof(sleepq_hash));
    memset((void*)sleepq_locks, 0, sizeof(sleepq_locks));
    sleepq_free_list = NULL;
}

// Internal helper to add thread
static void sleepq_add_internal(void *chan, thread_t *t, int type, int pid) {
    if (!chan || !t)
        return;

    int hash = sleepq_hash_func(chan, type, pid);
    unsigned long f = sq_lock(hash);

    // Find or create sleep queue
    sleepq_t *sq = sleepq_lookup(chan, type, pid, hash);
    if (!sq) {
        sq = sleepq_alloc();
        if (!sq) {
            sq_unlock(hash, f);
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
    /* Reflect the block in the BSD process-level state for ps(1)/procfs: a
     * thread going to sleep makes its process SSLEEP, unless it is explicitly
     * stopped/zombie/dying. */
    if (t->proc) {
        uint8_t pst = t->proc->state;
        if (pst != SSTOP && pst != SZOMB && pst != SDYING)
            t->proc->state = SSLEEP;
    }

    sq_unlock(hash, f);
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
    unsigned long f = sq_lock(hash);

    sleepq_t *sq = sleepq_lookup(chan, type, pid, hash);
    if (!sq || sq->sq_count == 0) {
        sq_unlock(hash, f);
        return NULL;
    }

    /* Dequeue-but-don't-resurrect guards.  A wake unlinks the head waiter
     * from the queue unconditionally, but only flips it back to READY if it
     * was genuinely sleeping:
     *   - THREAD_STOPPED: a SIGTSTP raced the enqueue; it stays stopped
     *     until SIGCONT.
     *   - THREAD_ZOMBIE: the waiter's process is exiting.  A thread killed
     *     while parked in an interruptible sleepq wait (e.g. a looping
     *     FUTEX_WAIT, or a pipe read) can re-block on a sleepq during its
     *     own teardown and then be marked THREAD_ZOMBIE by proc_exit while
     *     still linked here.  Resurrecting a dead thread to READY makes the
     *     scheduler pick it, arch_switch_to returns into proc_exit()'s
     *     post-sched_yield while(1), and — with preemption disabled there —
     *     the CPU busy-spins forever.  Under a storm of concurrent SIGKILL
     *     reaps (a busy long-running system) this wedges the whole guest.
     *     Same guard in every wake path below.
     *
     * A dead/stopped head must NOT consume the wake: single-wake consumers
     * (mutex_unlock, sema_post, rw_*unlock, futex PI unlock) fire exactly one
     * wake and ignore the result, so swallowing it here would strand the next
     * eligible waiter on a now-free lock forever.  Skip past dead heads until
     * a genuinely-sleeping waiter is resurrected (or the queue drains). */
    thread_t *woken = NULL;
    thread_t *t;
    while ((t = sq->sq_head) != NULL) {
        sq->sq_head = t->next;
        if (!sq->sq_head)
            sq->sq_tail = NULL;
        sq->sq_count--;

        t->next = NULL;
        t->wait_chan = NULL;
        if (t->state != THREAD_STOPPED && t->state != THREAD_ZOMBIE) {
            t->state = THREAD_READY;
            woken = t;
            break;
        }
        /* dead/stopped: unlinked, but keep looking for a live waiter */
    }

    // Remove sleep queue if empty
    if (sq->sq_count == 0) {
        sleepq_remove(sq, hash);
    }

    sq_unlock(hash, f);
    return(woken);
}

/*
 * NOTE: the single-waiter wakes deliberately do NOT call
 * sched_poll_wake_pollers().  Every caller of sleepq_wake_one[_private] is an
 * internal synchronisation primitive being released -- mutex_unlock,
 * kernel/POSIX semaphore post, rwlock -- none of which changes any pollable
 * fd's readiness.  Broadcasting to every poll()/select() sleeper on each of
 * those (mutex_unlock alone fires on essentially every locked kernel
 * operation) produced a self-sustaining thundering-herd storm: pollers woke
 * each other via the poll handlers' own mutex releases and the CPU never
 * reached the hlt idle path (~100% kernel time).  Real fd readiness changes go
 * through sleepq_wake_all() (pipe, af_unix) or sched_wakeup() (tty, tcp,
 * af_inet), both of which still kick pollers -- so no wakeup is lost.
 */
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
    unsigned long f = sq_lock(hash);

    sleepq_t *sq = sleepq_lookup(chan, type, pid, hash);
    if (!sq || sq->sq_count == 0) {
        sq_unlock(hash, f);
        return(0);
    }

    int woken = 0;
    thread_t *t = sq->sq_head;
    while (t) {
        thread_t *next = t->next;
        t->next = NULL;
        t->wait_chan = NULL;
        if (t->state != THREAD_STOPPED && t->state != THREAD_ZOMBIE) {
            t->state = THREAD_READY;
        }
        woken++;
        t = next;
    }

    // Remove sleep queue
    sleepq_remove(sq, hash);

    sq_unlock(hash, f);
    return(woken);
}

int sleepq_wake_all(void *chan) {
    int n = sleepq_wake_all_internal(chan, SLEEPQ_TYPE_SHARED, 0);
    /* Wake the poll()/select() sleepers registered on THIS channel (AF_UNIX
     * rx/tx, accept, futex, ...).  Targeted per-channel via the poll registry
     * (replaces the old system-wide g_poll_wake_chan fan-out). */
    poll_notify(chan);
    return n;
}

int sleepq_wake_all_private(void *chan) {
    int pid = sleepq_current_private_pid();
    if (pid < 0) return 0;
    int n = sleepq_wake_all_internal(chan, SLEEPQ_TYPE_PRIVATE, pid);
    poll_notify(chan);
    return n;
}

static int sleepq_wake_n_internal(void *chan, int n, int type, int pid) {
    if (!chan || n == 0)
        return(0);
    if (n < 0)
        return(sleepq_wake_all_internal(chan, type, pid));

    int hash = sleepq_hash_func(chan, type, pid);
    unsigned long f = sq_lock(hash);

    sleepq_t *sq = sleepq_lookup(chan, type, pid, hash);
    if (!sq || sq->sq_count == 0) {
        sq_unlock(hash, f);
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
        /* Only a genuinely-sleeping waiter counts toward n; a dead/stopped
         * head is unlinked but must not consume one of the n wakes, else a
         * wake_n(chan,1) release swallows the wake and strands the real
         * waiter (see sleepq_wake_one_internal). */
        if (t->state != THREAD_STOPPED && t->state != THREAD_ZOMBIE) {
            t->state = THREAD_READY;
            woken++;
        }
    }

    // Remove sleep queue if empty
    if (sq->sq_count == 0)
        sleepq_remove(sq, hash);

    sq_unlock(hash, f);
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

/*
 * Bitset-filtered wake (FUTEX_WAKE_BITSET).  Wakes up to 'n' waiters
 * whose futex_bitset shares at least one bit with 'mask'.  Unlike the
 * plain wake, non-matching waiters must be skipped, so this walks the
 * bucket list with a prev pointer and unlinks only the matches.
 */
static int sleepq_wake_bitset_internal(void *chan, int n, int type, int pid,
                                       uint32_t mask) {
    if (!chan || n == 0 || mask == 0)
        return(0);

    int hash = sleepq_hash_func(chan, type, pid);
    unsigned long f = sq_lock(hash);

    sleepq_t *sq = sleepq_lookup(chan, type, pid, hash);
    if (!sq || sq->sq_count == 0) {
        sq_unlock(hash, f);
        return(0);
    }

    int woken = 0;
    thread_t *prev = NULL;
    thread_t *t = sq->sq_head;
    while (t && (n < 0 || woken < n)) {
        thread_t *next = t->next;
        if (t->futex_bitset & mask) {
            /* unlink t */
            if (prev)
                prev->next = next;
            else
                sq->sq_head = next;
            if (sq->sq_tail == t)
                sq->sq_tail = prev;
            sq->sq_count--;

            t->next = NULL;
            t->wait_chan = NULL;
            /* A matching but dead/stopped waiter is unlinked but does not
             * consume a wake slot (don't count it toward n). */
            if (t->state != THREAD_STOPPED && t->state != THREAD_ZOMBIE) {
                t->state = THREAD_READY;
                woken++;
            }
            /* prev unchanged -- t was removed */
        } else {
            prev = t;
        }
        t = next;
    }

    if (sq->sq_count == 0)
        sleepq_remove(sq, hash);

    sq_unlock(hash, f);
    return(woken);
}

int sleepq_wake_bitset(void *chan, int n, uint32_t mask) {
    return sleepq_wake_bitset_internal(chan, n, SLEEPQ_TYPE_SHARED, 0, mask);
}

int sleepq_wake_bitset_private(void *chan, int n, uint32_t mask) {
    int pid = sleepq_current_private_pid();
    if (pid < 0) return 0;
    return sleepq_wake_bitset_internal(chan, n, SLEEPQ_TYPE_PRIVATE, pid, mask);
}

static int sleepq_has_waiters_internal(void *chan, int type, int pid) {
    if (!chan)
        return(0);
    
    int hash = sleepq_hash_func(chan, type, pid);
    unsigned long f = sq_lock(hash);

    sleepq_t *sq = sleepq_lookup(chan, type, pid, hash);
    int has = (sq && sq->sq_count > 0);

    sq_unlock(hash, f);
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
    
    // Lock ordering to prevent deadlock (lower hash first).  f_first saves
    // the caller's real IRQ state (before any cli); f_second is the already
    // IRQ-disabled state and must be restored FIRST on unwind so IRQs stay
    // masked until the outermost bucket is released (see sq_lock/sq_unlock).
    unsigned long f_first, f_second = 0;
    if (src_hash < dst_hash) {
        f_first = sq_lock(src_hash);
        f_second = sq_lock(dst_hash);
    } else if (src_hash > dst_hash) {
        f_first = sq_lock(dst_hash);
        f_second = sq_lock(src_hash);
    } else {
        f_first = sq_lock(src_hash);
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
            /* Dead/stopped waiter: unlink but don't count it as a wake, so
             * the requeue's wake phase delivers wake_n live wakes. */
            if (t->state != THREAD_STOPPED && t->state != THREAD_ZOMBIE) {
                t->state = THREAD_READY;
                woken_count++;
            }
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
    
    // Unlock in reverse acquisition order (second-acquired first) so the
    // outermost sq_unlock restores the caller's original IRQ state last.
    if (src_hash < dst_hash) {
        sq_unlock(dst_hash, f_second);
        sq_unlock(src_hash, f_first);
    } else if (src_hash > dst_hash) {
        sq_unlock(src_hash, f_second);
        sq_unlock(dst_hash, f_first);
    } else {
        sq_unlock(src_hash, f_first);
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
