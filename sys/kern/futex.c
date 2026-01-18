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
#include <errno.h>
#include <kern/sched.h>
#include <arch/i386/pmap.h>
#include <stddef.h>
#include <stdint.h>

/* Forward declarations for sleepq backend */
extern int sleepq_wake_n(void *chan, int n);
extern int sleepq_requeue(void *src, void *dst, int wake_n, int requeue_n);

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
 * Get physical address key for a user virtual address
 *
 * Futexes use physical addresses as keys so that shared memory
 * futexes work correctly across processes. Private futexes
 * could optimize by using (process_id, vaddr) tuple.
 *
 * Returns NULL if page is not mapped.
 */
static void *futex_get_key(uintptr_t uaddr) {
    if (!current_process || !current_process->pmap) return NULL;
    
    /* Use pmap_extract to get physical address */
    uintptr_t pa = pmap_extract(current_process->pmap, uaddr);
    
    /* Page must be present */
    if (pa == 0) return NULL;
    
    /* Include page offset for correct key */
    return (void *)(pa + (uaddr & 0xFFF));
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
        /* Wake one waiter so they can acquire the lock */
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
void futex_exit_cleanup(void) {
    if (!current_thread || !current_thread->robust_list) return;
    
    struct robust_list_head *head = current_thread->robust_list;
    struct robust_list *entry;
    int count = 0;
    const int MAX_ROBUST_WALK = 4096;  /* Prevent infinite loops */
    
    /* Process pending entry first (in case we died mid-lock/unlock) */
    if (head->list_op_pending) {
        int *futex_addr = (int *)((char *)head->list_op_pending + head->futex_offset);
        int val;
        
        if (futex_read_user(futex_addr, &val) == 0) {
            if ((val & FUTEX_TID_MASK) == (uint32_t)current_thread->tid) {
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
            if ((val & FUTEX_TID_MASK) == (uint32_t)current_thread->tid) {
                futex_handle_dead_owner(futex_addr);
            }
        }
        
        entry = next;
        count++;
    }
    
    /* Clear the robust list */
    current_thread->robust_list = NULL;
    current_thread->robust_list_len = 0;
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
        /* TODO: Lookup thread by TID and verify permissions */
        /* For now, only allow current thread */
        return -ESRCH;
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
    /* int private_flag = op & FUTEX_PRIVATE_FLAG; */ /* TODO: Optimize private futexes */

    void *key = futex_get_key((uintptr_t)uaddr);
    if (!key) {
        return -EFAULT;
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
            
            /* Sleep on the physical address key */
            /* TODO: Honor timeout parameter */
            (void)timeout;
            sched_sleep(key);
            
            /* Return 0 on wake (or -EINTR if interrupted by signal) */
            return 0;
        }

        case FUTEX_WAKE: {
            /*
             * Wake up to 'val' threads waiting on this futex
             */
            int ret = sleepq_wake_n(key, val);
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
            
            void *key2 = futex_get_key((uintptr_t)uaddr2);
            if (!key2) return -EFAULT;
            
            /* 'timeout' is repurposed as val2 (requeue limit) */
            int requeue_limit = (int)(uintptr_t)timeout;
            
            int ret = sleepq_requeue(key, key2, val, requeue_limit);
            return ret;
        }

        case FUTEX_LOCK_PI: {
            /*
             * Priority Inheritance lock acquisition
             * TODO: Full PI implementation with priority boosting
             */
            return futex_lock_pi(uaddr, 0, 0);
        }

        case FUTEX_UNLOCK_PI: {
            /*
             * Priority Inheritance unlock
             */
            return futex_unlock_pi(uaddr);
        }

        case FUTEX_TRYLOCK_PI: {
            /*
             * Non-blocking PI lock attempt
             */
            return futex_lock_pi(uaddr, 0, 1);
        }

        default:
            return -ENOSYS;
    }
}

/*
 * Priority Inheritance futex lock (stub implementation)
 *
 * Full PI requires:
 * 1. Tracking owner in kernel
 * 2. Priority boosting blocked owner to waiter's priority
 * 3. Priority chain propagation
 */
int futex_lock_pi(int *uaddr, int detect, int trylock) {
    (void)detect;
    
    if (!current_thread) return -EFAULT;
    
    int tid = current_thread->tid;
    int err;
    int oldval, newval;
    
    /* Try to acquire: CAS 0 -> our_tid */
    oldval = futex_cmpxchg_user(uaddr, 0, tid, &err);
    if (err) return err;
    
    if (oldval == 0) {
        /* Successfully acquired */
        return 0;
    }
    
    if (trylock) {
        /* Non-blocking and failed */
        return -EWOULDBLOCK;
    }
    
    /* Check for OWNER_DIED - can take over */
    if (oldval & FUTEX_OWNER_DIED) {
        newval = tid | (oldval & FUTEX_WAITERS);
        if (futex_cmpxchg_user(uaddr, oldval, newval, &err) == oldval) {
            return -EOWNERDEAD;  /* Success, but previous owner died */
        }
        if (err) return err;
    }
    
    /* Must wait - set WAITERS bit and sleep */
    void *key = futex_get_key((uintptr_t)uaddr);
    if (!key) return -EFAULT;
    
    for (;;) {
        if (futex_read_user(uaddr, &oldval) != 0) return -EFAULT;
        
        /* Set WAITERS bit if not already set */
        if (!(oldval & FUTEX_WAITERS)) {
            newval = oldval | FUTEX_WAITERS;
            if (futex_cmpxchg_user(uaddr, oldval, newval, &err) != oldval) {
                if (err) return err;
                continue;  /* Retry */
            }
        }
        
        /* TODO: Boost owner priority here */
        
        /* Sleep waiting for unlock */
        sched_sleep(key);
        
        /* Woken up - try to acquire again */
        oldval = futex_cmpxchg_user(uaddr, 0, tid, &err);
        if (err) return err;
        
        if (oldval == 0) {
            return 0;  /* Got it */
        }
        
        if (oldval & FUTEX_OWNER_DIED) {
            newval = tid | (oldval & FUTEX_WAITERS);
            if (futex_cmpxchg_user(uaddr, oldval, newval, &err) == oldval) {
                return -EOWNERDEAD;
            }
        }
        
        /* Lost race, go back to sleep */
    }
}

/*
 * Priority Inheritance futex unlock
 */
int futex_unlock_pi(int *uaddr) {
    if (!current_thread) return -EFAULT;
    
    int tid = current_thread->tid;
    int err;
    int oldval, newval;
    
    /* Read current value */
    if (futex_read_user(uaddr, &oldval) != 0) return -EFAULT;
    
    /* Verify we own it */
    if ((oldval & FUTEX_TID_MASK) != (uint32_t)tid) {
        return -EPERM;
    }
    
    /* If no waiters, just clear */
    if (!(oldval & FUTEX_WAITERS)) {
        if (futex_cmpxchg_user(uaddr, oldval, 0, &err) == oldval) {
            return 0;
        }
        if (err) return err;
        /* Value changed, retry read */
        if (futex_read_user(uaddr, &oldval) != 0) return -EFAULT;
    }
    
    /* Have waiters - clear and wake one */
    newval = 0;
    if (futex_cmpxchg_user(uaddr, oldval, newval, &err) == oldval) {
        void *key = futex_get_key((uintptr_t)uaddr);
        if (key) {
            /* TODO: Wake highest priority waiter and give them lock */
            sleepq_wake_n(key, 1);
        }
        return 0;
    }
    
    return err ? err : -EAGAIN;
}
