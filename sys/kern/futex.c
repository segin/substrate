/*
 * futex.c - Fast Userspace Mutex Implementation
 *
 * Implements Linux-compatible futex operations including:
 * - FUTEX_WAIT/WAKE: Basic sleep/wake operations
 * - FUTEX_REQUEUE: Efficient transfer of waiters
 * - FUTEX_ROBUST_LIST: Owner death cleanup
 * - FUTEX_LOCK_PI/UNLOCK_PI: Priority inheritance (stub)
 *
 * References:
 * - "Futexes Are Tricky" by Ulrich Drepper
 * - Linux kernel Documentation/locking/futex-requeue-pi.txt
 */

#include <sys/futex.h>
#include <sys/proc.h>
#include <sys/errno.h>
#include <sys/time.h>
#include <kern/sched.h>
#include <kern/sleepq.h>
#include <kern/time.h>
#include <arch/i386/pmap.h>
#include <stddef.h>
#include <stdint.h>

/*
 * User address validation
 *
 * Validates that:
 * 1. Address is in user space (< KERNEL_BASE)
 * 2. Address is 4-byte aligned (futex word requirement)
 */
#ifdef __x86_64__
#define USER_SPACE_MAX 0x00007FFFFFFFFFFFULL
#else
#define USER_SPACE_MAX 0xBFFFFFFFU
#endif

static inline int validate_uaddr(uintptr_t addr) {
    return (addr <= USER_SPACE_MAX) && ((addr & 3) == 0);
}

/*
 * Helper to safely read timespec from user space
 */
static int futex_read_timespec(void *uaddr, struct timespec *out) {
    uintptr_t addr = (uintptr_t)uaddr;

    /* Check basic user space bounds */
    if (addr > USER_SPACE_MAX || (addr + sizeof(struct timespec)) > USER_SPACE_MAX) {
        return -EFAULT;
    }

    /* Check alignment (4-byte aligned for 32-bit time_t/long) */
    if (addr & 3) {
        return -EFAULT;
    }

    /* Direct copy since we share address space and validated bounds */
    /* In a full model we'd use copyin() to handle faults */
    *out = *(struct timespec *)uaddr;

    return 0;
}

/*
 * Get physical address key for a user virtual address
 *
 * Futexes use physical addresses as keys so that shared memory
 * futexes work correctly across processes. Private futexes
 * could optimize by using (process_id, vaddr) tuple.
 *
 * Returns NULL if page is not mapped.
 */
void *futex_get_key(uintptr_t uaddr) {
    if (!current_process || !current_process->pmap) return NULL;
    
    /* Use pmap_extract to get physical address */
    uintptr_t pa = pmap_extract(current_process->pmap, uaddr);
    
    /* Page must be present */
    if (pa == 0) return NULL;
    
    /* pmap_extract returns the exact physical address (including offset).
       We use it directly as the key. */
    return (void *)pa;
}

/*
 * Safe userspace read (without triggering faults here)
 * Returns 0 on success, -EFAULT on failure.
 */
static int futex_read_user(int *uaddr, int *value) {
    /* In a real implementation, this would use safe copy functions
     * to handle page faults gracefully. For now, direct access
     * with validation that page is mapped. */
    if (!validate_uaddr((uintptr_t)uaddr)) return -EFAULT;
    
    /* We only need to check mapping presence if we are about to access it.
       If we trust validate_uaddr and pmap, we might skip full key check here
       if performance critical, but for safety lets check mapping exists.
       However, for private futexes we might want to avoid pmap_extract.
       But we still need to know if memory is accessible.
       Let's assume validate_uaddr is enough for VA range,
       and page fault handler handles the rest (if we had copyin).
       Since we dereference directly, we MUST ensure mapping exists.
    */
    void *key = futex_get_key((uintptr_t)uaddr);
    if (!key) return -EFAULT;
    
    *value = *uaddr;
    return 0;
}

/*
 * Safe userspace CMPXCHG for atomic operations
 * Returns old value, or sets *err on failure.
 */
static int futex_cmpxchg_user(int *uaddr, int oldval, int newval, int *err) {
    if (!validate_uaddr((uintptr_t)uaddr)) {
        *err = -EFAULT;
        return 0;
    }
    
    void *key = futex_get_key((uintptr_t)uaddr);
    if (!key) {
        *err = -EFAULT;
        return 0;
    }
    
    *err = 0;
    
    /* Inline atomic cmpxchg */
    int prev;
#ifdef __x86_64__
    __asm__ volatile(
        "lock cmpxchgl %2, %1"
        : "=a"(prev), "+m"(*uaddr)
        : "r"(newval), "0"(oldval)
        : "memory"
    );
#else
    __asm__ volatile(
        "lock cmpxchgl %2, %1"
        : "=a"(prev), "+m"(*uaddr)
        : "r"(newval), "0"(oldval)
        : "memory"
    );
#endif
    
    return prev;
}

/* Forward declarations for PI functions */
int futex_lock_pi(int *uaddr, int detect, int trylock, int private);
int futex_unlock_pi(int *uaddr, int private);

/*
 * Mark a futex as having a dead owner
 * Sets FUTEX_OWNER_DIED bit and wakes one waiter
 */
static void futex_handle_dead_owner(int *uaddr) {
    int err, oldval, newval;
    
    do {
        if (futex_read_user(uaddr, &oldval) != 0) return;
        
        /* Set OWNER_DIED, clear TID, keep WAITERS bit */
        newval = (oldval & FUTEX_WAITERS) | FUTEX_OWNER_DIED;
        
    } while (futex_cmpxchg_user(uaddr, oldval, newval, &err) != oldval && err == 0);
    
    if (err == 0) {
        /* Wake one waiter so they can acquire the lock.
           Robust lists don't specify private/shared in the list entry.
           But usually robust mutexes are shared.
           However, we need to know if it's private.
           The robust list entry contains 'futex_offset'.
           It doesn't contain flags.
           Linux kernel assumes shared for robust list cleanup?
           Or maybe it tries both?
           For now, assume shared (safe default).
        */
        void *key = futex_get_key((uintptr_t)uaddr);
        if (key) {
            sleepq_wake_n(key, 1);
        }
    }
}

/*
 * Walk the robust list and cleanup on thread exit
 *
 * For each entry in the list:
 * 1. Check if current thread owns it (TID matches)
 * 2. If so, mark with FUTEX_OWNER_DIED and wake waiters
 */
void futex_thread_exit(thread_t *t) {
    if (!t || !t->robust_list) return;
    
    struct robust_list_head *head = t->robust_list;
    struct robust_list *entry;
    int count = 0;
    const int MAX_ROBUST_WALK = 4096;  /* Prevent infinite loops */
    
    /* Process pending entry first (in case we died mid-lock/unlock) */
    if (head->list_op_pending) {
        int *futex_addr = (int *)((char *)head->list_op_pending + head->futex_offset);
        int val;
        
        if (futex_read_user(futex_addr, &val) == 0) {
            if ((val & FUTEX_TID_MASK) == (uint32_t)t->tid) {
                futex_handle_dead_owner(futex_addr);
            }
        }
    }
    
    /* Walk the circular list */
    entry = head->list.next;
    while (entry != &head->list && count < MAX_ROBUST_WALK) {
        struct robust_list *next;
        
        /* Read next pointer safely before processing */
        if (futex_read_user((int *)&entry->next, (int *)&next) != 0) break;
        
        /* Calculate futex address from entry */
        int *futex_addr = (int *)((char *)entry + head->futex_offset);
        int val;
        
        if (futex_read_user(futex_addr, &val) == 0) {
            /* Check if we own this lock */
            if ((val & FUTEX_TID_MASK) == (uint32_t)t->tid) {
                futex_handle_dead_owner(futex_addr);
            }
        }
        
        entry = next;
        count++;
    }
    
    /* Clear the robust list */
    t->robust_list = NULL;
    t->robust_list_len = 0;
}

void futex_exit_cleanup(void) {
    futex_thread_exit(current_thread);
}

void futex_wake_exited_thread(int *uaddr) {
    if (!uaddr) return;

    /* We assume the caller is the scheduler and we are still in the pmap
     * of the exiting thread. */
    void *key = futex_get_key((uintptr_t)uaddr);
    if (!key) return;

    /* Write 1 to signify exit */
    *uaddr = 1;

    /* Wake all waiters (should only be one, but safe) */
    sleepq_wake_all(key);
}

/*
 * sys_set_robust_list - Register thread's robust futex list
 */
int sys_set_robust_list(struct robust_list_head *head, size_t len) {
    if (!current_thread) return -EINVAL;
    
    /* Validate size matches expected structure size */
    if (len != sizeof(struct robust_list_head)) {
        return -EINVAL;
    }
    
    /* Validate address is in userspace */
    if (head && !validate_uaddr((uintptr_t)head)) {
        return -EFAULT;
    }
    
    current_thread->robust_list = head;
    current_thread->robust_list_len = len;
    
    return 0;
}

/*
 * sys_get_robust_list - Get a thread's robust futex list
 *
 * If pid == 0, returns current thread's list.
 * Otherwise, requires appropriate permissions (same user or CAP_SYS_PTRACE).
 */
int sys_get_robust_list(int pid, struct robust_list_head **head_ptr, size_t *len_ptr) {
    thread_t *target;
    
    if (pid == 0) {
        target = current_thread;
    } else {
        target = sched_get_thread(pid);
        if (!target) return -ESRCH;

        /* Verify permissions:
         * 1. Target is current thread (handled implicitly by same process check usually, but good for clarity)
         * 2. Target is in same process
         * 3. Target process has same UID as current process EUID
         * 4. Current process is root (euid == 0)
         */
        if (target != current_thread &&
            target->proc != current_process &&
            target->proc->uid != current_process->euid &&
            current_process->euid != 0) {
            return -EPERM;
        }
    }
    
    if (!target) return -ESRCH;
    
    /* Validate output pointers */
    if (!validate_uaddr((uintptr_t)head_ptr) || 
        !validate_uaddr((uintptr_t)len_ptr)) {
        return -EFAULT;
    }
    
    /* Write results to userspace */
    *head_ptr = target->robust_list;
    *len_ptr = target->robust_list_len;
    
    return 0;
}

/*
 * Main futex syscall dispatcher
 */
int sys_futex(int *uaddr, int op, int val, void *timeout, int *uaddr2, int val3) {
    if (!uaddr || !validate_uaddr((uintptr_t)uaddr)) {
        return -EFAULT;
    }

    /* Extract operation and flags */
    int cmd = op & FUTEX_CMD_MASK;
    int private_flag = (op & FUTEX_PRIVATE_FLAG) != 0;

    void *key;
    if (private_flag) {
        key = (void *)uaddr;
    } else {
        key = futex_get_key((uintptr_t)uaddr);
        if (!key) {
            return -EFAULT;
        }
    }

    switch (cmd) {
        case FUTEX_WAIT: {
            /*
             * Atomic compare-and-sleep:
             * 1. Read current value
             * 2. If != expected val, return -EAGAIN
             * 3. Otherwise, sleep on the futex key
             */
            int current_val;
            if (futex_read_user(uaddr, &current_val) != 0) {
                return -EFAULT;
            }
            
            if (current_val != val) {
                return -EAGAIN;
            }
            
            /* Sleep on the key */
            if (timeout) {
                struct timespec ts;
                if (futex_read_timespec(timeout, &ts) != 0) {
                    return -EFAULT;
                }

                if (ts.tv_nsec < 0 || ts.tv_nsec >= 1000000000) {
                    return -EINVAL;
                }

                uint64_t hz = get_hz();
                uint64_t ticks = (uint64_t)ts.tv_sec * hz;
                ticks += ((uint64_t)ts.tv_nsec * hz) / 1000000000ULL;

                uint64_t deadline = get_ticks() + ticks;
                current_thread->sleep_expiry = deadline;
                current_thread->sleep_status = 0;
            }

            if (private_flag)
                sleepq_add_private(key, current_thread);
            else
                sleepq_add(key, current_thread);

            sched_yield();

            if (timeout) {
                current_thread->sleep_expiry = 0;
                if (current_thread->sleep_status == -ETIMEDOUT)
                    return -ETIMEDOUT;
            }

            /* Check for signal interruption */
            if ((current_thread->flags & THREAD_F_INTERRUPTIBLE) &&
                (current_thread->sig_pending & ~current_thread->sig_mask)) {
                return -EINTR;
            }
            
            /* Return 0 on wake */
            return 0;
        }

        case FUTEX_WAKE: {
            /*
             * Wake up to 'val' threads waiting on this futex
             */
            int ret;
            if (private_flag)
                ret = sleepq_wake_n_private(key, val);
            else
                ret = sleepq_wake_n(key, val);
            return ret;
        }

        case FUTEX_REQUEUE:
        case FUTEX_CMP_REQUEUE: {
            /*
             * Requeue operation:
             * 1. Wake 'val' threads from uaddr
             * 2. Move 'val2' threads from uaddr to uaddr2
             * 
             * CMP variant first checks *uaddr == val3
             */
            if (cmd == FUTEX_CMP_REQUEUE) {
                int current_val;
                if (futex_read_user(uaddr, &current_val) != 0) {
                    return -EFAULT;
                }
                if (current_val != val3) {
                    return -EAGAIN;
                }
            }
            
            if (!uaddr2 || !validate_uaddr((uintptr_t)uaddr2)) {
                return -EFAULT;
            }
            
            void *key2;
            if (private_flag) {
                key2 = (void *)uaddr2;
            } else {
                key2 = futex_get_key((uintptr_t)uaddr2);
                if (!key2) return -EFAULT;
            }
            
            /* 'timeout' is repurposed as val2 (requeue limit) */
            int requeue_limit = (int)(uintptr_t)timeout;
            
            int ret;
            if (private_flag)
                ret = sleepq_requeue_private(key, key2, val, requeue_limit);
            else
                ret = sleepq_requeue(key, key2, val, requeue_limit);
            return ret;
        }

        case FUTEX_LOCK_PI: {
            /*
             * Priority Inheritance lock acquisition
             */
            return futex_lock_pi(uaddr, 0, 0, private_flag);
        }

        case FUTEX_UNLOCK_PI: {
            /*
             * Priority Inheritance unlock
             */
            return futex_unlock_pi(uaddr, private_flag);
        }

        case FUTEX_TRYLOCK_PI: {
            /*
             * Non-blocking PI lock attempt
             */
            return futex_lock_pi(uaddr, 0, 1, private_flag);
        }

        default:
            return -ENOSYS;
    }
}

/*
 * ============================================================
 * Priority Inheritance (PI) Futex Implementation
 * ============================================================
 *
 * PI futexes prevent priority inversion:
 * - When a high-priority thread blocks on a mutex held by a
 *   low-priority thread, the low-priority owner is temporarily
 *   boosted to the waiter's priority.
 * - Priority boosting propagates through lock chains.
 *
 * State tracked per PI futex:
 * - Owner TID in user word (FUTEX_TID_MASK)
 * - Kernel tracks waiters with their priorities
 */

/* PI waiter state (kernel-side tracking) */
typedef struct pi_waiter {
    thread_t          *task;       /* Waiting thread */
    int                priority;   /* Priority at time of wait */
    struct pi_waiter  *next;       /* Next in waiter list */
    void              *key;        /* Futex key */
} pi_waiter_t;

/* PI futex state (kernel-side) */
typedef struct pi_state {
    void              *key;        /* Futex key (physical addr) */
    int                type;       /* 0=Shared, 1=Private */
    int                pid;        /* PID for private */
    thread_t          *owner;      /* Current owner thread */
    int                owner_prio; /* Owner's original priority */
    pi_waiter_t       *waiters;    /* Priority-sorted waiter list */
    int                boosted_prio; /* Current boosted priority */
    struct pi_state   *next;       /* Hash chain link */
} pi_state_t;

#define PI_HASH_SIZE 64
#define PI_HASH_MASK (PI_HASH_SIZE - 1)
static pi_state_t *pi_hash[PI_HASH_SIZE];
static volatile uint32_t pi_lock = 0;

#define PI_POOL_SIZE 32
static pi_state_t pi_pool[PI_POOL_SIZE];
static pi_waiter_t waiter_pool[PI_POOL_SIZE * 4];
static int pi_pool_idx = 0;
static int waiter_pool_idx = 0;

static inline void pi_spinlock(void) {
    while (__sync_lock_test_and_set(&pi_lock, 1))
        while (pi_lock) __asm__ volatile("pause");
}

static inline void pi_unlock(void) {
    __sync_lock_release(&pi_lock);
}

static inline int pi_hash_func(void *key, int type, int pid) {
    if (type == 1) { // Private
        return (((uintptr_t)key >> 3) ^ pid) & PI_HASH_MASK;
    }
    return ((uintptr_t)key >> 3) & PI_HASH_MASK;
}

static pi_state_t *pi_state_alloc(void) {
    if (pi_pool_idx >= PI_POOL_SIZE) return NULL;
    pi_state_t *ps = &pi_pool[pi_pool_idx++];
    ps->key = NULL;
    ps->type = 0;
    ps->pid = 0;
    ps->owner = NULL;
    ps->owner_prio = 0;
    ps->waiters = NULL;
    ps->boosted_prio = 0;
    ps->next = NULL;
    return ps;
}

static pi_waiter_t *pi_waiter_alloc(void) {
    if (waiter_pool_idx >= PI_POOL_SIZE * 4) return NULL;
    pi_waiter_t *pw = &waiter_pool[waiter_pool_idx++];
    pw->task = NULL;
    pw->priority = 0;
    pw->next = NULL;
    pw->key = NULL;
    return pw;
}

static pi_state_t *pi_lookup(void *key, int type, int pid) {
    int hash = pi_hash_func(key, type, pid);
    pi_state_t *ps = pi_hash[hash];
    while (ps) {
        if (ps->key == key && ps->type == type) {
            if (type == 0 || ps->pid == pid)
                return ps;
        }
        ps = ps->next;
    }
    return NULL;
}

static pi_state_t *pi_get_or_create(void *key, int type, int pid) {
    int hash = pi_hash_func(key, type, pid);
    pi_state_t *ps = pi_lookup(key, type, pid);
    if (ps) return ps;
    
    ps = pi_state_alloc();
    if (!ps) return NULL;
    
    ps->key = key;
    ps->type = type;
    ps->pid = pid;
    ps->next = pi_hash[hash];
    pi_hash[hash] = ps;
    return ps;
}

/* Insert waiter in priority order (highest first) */
static void pi_insert_waiter(pi_state_t *ps, pi_waiter_t *pw) {
    if (!ps->waiters || pw->priority > ps->waiters->priority) {
        pw->next = ps->waiters;
        ps->waiters = pw;
    } else {
        pi_waiter_t *cur = ps->waiters;
        while (cur->next && cur->next->priority >= pw->priority) {
            cur = cur->next;
        }
        pw->next = cur->next;
        cur->next = pw;
    }
}

/* Remove a waiter from the list */
static void pi_remove_waiter(pi_state_t *ps, thread_t *t) {
    pi_waiter_t **pp = &ps->waiters;
    while (*pp) {
        if ((*pp)->task == t) {
            *pp = (*pp)->next;
            return;
        }
        pp = &(*pp)->next;
    }
}

/* Get highest priority among waiters */
static int pi_top_waiter_prio(pi_state_t *ps) {
    if (!ps->waiters) return 0;
    return ps->waiters->priority;
}

/*
 * Boost owner's priority to at least waiter's level
 */
static void pi_boost_owner(pi_state_t *ps) {
    if (!ps->owner) return;
    
    int top_prio = pi_top_waiter_prio(ps);
    if (top_prio <= ps->boosted_prio) return;
    
    /* Save original priority on first boost */
    if (ps->boosted_prio == 0) {
        ps->owner_prio = ps->owner->priority;
    }
    
    /* Set boosted priority */
    ps->boosted_prio = top_prio;
    sched_set_priority(ps->owner->tid, ps->owner->sched_class, top_prio);
}

/*
 * Restore owner's original priority
 */
static void pi_deboost_owner(pi_state_t *ps) {
    if (!ps->owner) return;
    if (ps->boosted_prio == 0) return;
    
    /* Check if still need boost from remaining waiters */
    int top_prio = pi_top_waiter_prio(ps);
    if (top_prio > 0 && top_prio > ps->owner_prio) {
        /* Still need some boost */
        ps->boosted_prio = top_prio;
        sched_set_priority(ps->owner->tid, ps->owner->sched_class, top_prio);
    } else {
        /* Restore original */
        sched_set_priority(ps->owner->tid, ps->owner->sched_class, ps->owner_prio);
        ps->boosted_prio = 0;
    }
}

/*
 * Priority Inheritance futex lock
 *
 * 1. Try atomic acquire (CAS 0 -> TID)
 * 2. If contended, add to PI waiters and boost owner
 * 3. Sleep until woken
 * 4. On wake, retry acquire
 */
int futex_lock_pi(int *uaddr, int detect, int trylock, int private) {
    (void)detect;
    
    if (!current_thread) return -EFAULT;
    
    int tid = current_thread->tid;
    int err;
    int oldval, newval;
    
    /* Fast path: try uncontended acquire */
    oldval = futex_cmpxchg_user(uaddr, 0, tid, &err);
    if (err) return err;
    
    if (oldval == 0) {
        /* Successfully acquired - no contention */
        return 0;
    }
    
    if (trylock) {
        return -EWOULDBLOCK;
    }
    
    /* Check for OWNER_DIED - can take over */
    if (oldval & FUTEX_OWNER_DIED) {
        newval = tid | (oldval & FUTEX_WAITERS);
        if (futex_cmpxchg_user(uaddr, oldval, newval, &err) == oldval) {
            return -EOWNERDEAD;
        }
        if (err) return err;
    }
    
    /* Slow path: contended - need PI handling */
    void *key;
    if (private) {
        key = (void *)uaddr;
    } else {
        key = futex_get_key((uintptr_t)uaddr);
        if (!key) return -EFAULT;
    }

    int pid = private ? current_process->pid : 0;
    
    pi_spinlock();
    
    pi_state_t *ps = pi_get_or_create(key, private, pid);
    if (!ps) {
        pi_unlock();
        return -ENOMEM;
    }
    
    /* Track owner if not already tracked */
    if (!ps->owner) {
        int owner_tid = oldval & FUTEX_TID_MASK;
        thread_t *owner = sched_get_thread(owner_tid);
        if (owner) {
            ps->owner = owner;
            ps->owner_prio = owner->priority;
        }
    }
    
    /* Create waiter entry */
    pi_waiter_t *pw = pi_waiter_alloc();
    if (!pw) {
        pi_unlock();
        return -ENOMEM;
    }
    
    pw->task = current_thread;
    pw->priority = current_thread->priority;
    pw->key = key;
    
    pi_insert_waiter(ps, pw);
    
    /* Boost owner's priority */
    pi_boost_owner(ps);
    
    pi_unlock();
    
    /* Loop until we acquire */
    for (;;) {
        if (futex_read_user(uaddr, &oldval) != 0) {
            pi_spinlock();
            pi_remove_waiter(ps, current_thread);
            pi_unlock();
            return -EFAULT;
        }
        
        /* Ensure WAITERS bit is set */
        if (!(oldval & FUTEX_WAITERS)) {
            newval = oldval | FUTEX_WAITERS;
            if (futex_cmpxchg_user(uaddr, oldval, newval, &err) != oldval) {
                if (err) {
                    pi_spinlock();
                    pi_remove_waiter(ps, current_thread);
                    pi_unlock();
                    return err;
                }
                continue;
            }
        }
        
        /* Sleep using sleepq mechanism */
        if (private)
            sleepq_add_private(key, current_thread);
        else
            sleepq_add(key, current_thread);
        sched_yield();
        
        /* Woken: try to acquire */
        oldval = futex_cmpxchg_user(uaddr, 0, tid, &err);
        if (err) {
            pi_spinlock();
            pi_remove_waiter(ps, current_thread);
            pi_unlock();
            return err;
        }
        
        if (oldval == 0) {
            /* Got it! */
            pi_spinlock();
            pi_remove_waiter(ps, current_thread);
            ps->owner = current_thread;
            ps->owner_prio = current_thread->priority;
            ps->boosted_prio = 0;
            pi_unlock();
            return 0;
        }
        
        if (oldval & FUTEX_OWNER_DIED) {
            newval = tid | (oldval & FUTEX_WAITERS);
            if (futex_cmpxchg_user(uaddr, oldval, newval, &err) == oldval) {
                pi_spinlock();
                pi_remove_waiter(ps, current_thread);
                ps->owner = current_thread;
                ps->owner_prio = current_thread->priority;
                ps->boosted_prio = 0;
                pi_unlock();
                return -EOWNERDEAD;
            }
        }
        
        /* Lost race, loop back and re-boost if needed */
        pi_spinlock();
        int owner_tid = oldval & FUTEX_TID_MASK;
        if (owner_tid && (!ps->owner || ps->owner->tid != owner_tid)) {
            thread_t *new_owner = sched_get_thread(owner_tid);
            if (new_owner) {
                ps->owner = new_owner;
                ps->owner_prio = new_owner->priority;
                pi_boost_owner(ps);
            }
        }
        pi_unlock();
    }
}

/*
 * Priority Inheritance futex unlock
 *
 * 1. Verify ownership
 * 2. Release lock atomically
 * 3. Deboost priority
 * 4. Wake highest priority waiter
 */
int futex_unlock_pi(int *uaddr, int private) {
    if (!current_thread) return -EFAULT;
    
    int tid = current_thread->tid;
    int err;
    int oldval, newval;
    
    if (futex_read_user(uaddr, &oldval) != 0) return -EFAULT;
    
    /* Verify ownership */
    if ((oldval & FUTEX_TID_MASK) != (uint32_t)tid) {
        return -EPERM;
    }
    
    void *key;
    if (private) {
        key = (void *)uaddr;
    } else {
        key = futex_get_key((uintptr_t)uaddr);
        if (!key) return -EFAULT;
    }

    int pid = private ? current_process->pid : 0;
    
    pi_spinlock();
    
    pi_state_t *ps = pi_lookup(key, private, pid);
    
    /* Deboost before releasing */
    if (ps) {
        pi_deboost_owner(ps);
        ps->owner = NULL;
    }
    
    pi_unlock();
    
    /* Release the lock */
    if (!(oldval & FUTEX_WAITERS)) {
        /* No waiters, simple clear */
        if (futex_cmpxchg_user(uaddr, oldval, 0, &err) == oldval) {
            return 0;
        }
        if (err) return err;
        /* Retry with fresh value */
        if (futex_read_user(uaddr, &oldval) != 0) return -EFAULT;
    }
    
    /* Have waiters: clear lock and wake one */
    newval = 0;
    if (futex_cmpxchg_user(uaddr, oldval, newval, &err) == oldval) {
        /* Wake highest priority waiter */
        if (private)
            sleepq_wake_n_private(key, 1);
        else
            sleepq_wake_n(key, 1);
        return 0;
    }
    
    return err ? err : -EAGAIN;
}
