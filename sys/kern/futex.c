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

#include <stddef.h>
#include <stdint.h>

#include <arch/i386/pmap.h>
#include <kern/sched.h>
#include <kern/sleepq.h>
#include <kern/time.h>
#include <sys/errno.h>
#include <sys/futex.h>
#include <sys/proc.h>
#include <sys/time.h>
#include <vm/vm_kmem.h>

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

    /* Use copyin() to safely copy from userspace with fault handling */
    if (copyin(uaddr, out, sizeof(struct timespec)) != 0)
        return -EFAULT;

    return 0;
}

/*
 * Get the key for a user virtual address.
 *
 * IMPORTANT: the key is the VIRTUAL address, NOT the physical address.
 *
 * History / why this changed: futexes used to key on the physical address
 * (pmap_extract), the idea being that a futex word in MAP_SHARED memory
 * would resolve to the same key in every process that maps it.  But a
 * physical key is NOT STABLE in substrate: anonymous user pages can change
 * their backing physical frame underneath a parked waiter (COW fault, page
 * migration, mmap/munmap frame reuse).  When that happens, the waiter is
 * enqueued on the sleepq under the OLD physical frame while any subsequent
 * FUTEX_WAKE computes the NEW physical frame (or 0 if the page is transiently
 * unmapped) and finds no waiter -- the wakeup is permanently lost and the
 * thread sleeps forever.  This deadlocked every std::mutex / std::condition_
 * variable / std::shared_mutex heavy multithreaded program (PsyMP3's TagLib
 * metadata path hung with all threads parked, never woken).
 *
 * The fix: key on the virtual address and scope the sleepq by PID (the
 * private-futex path).  A virtual address is stable for the life of a wait
 * regardless of how the page's physical backing moves, so a waiter and its
 * waker -- which run in the same address space with the same uaddr -- always
 * compute the same key.  Cross-process MAP_SHARED futexes (which the old
 * physical scheme aimed at but never actually worked for, since the physical
 * key was unstable anyway) are not used by anything in substrate; if needed
 * they would require pinning the page for the wait's duration.
 *
 * Returns NULL only if the futex word is not currently accessible.
 */
void *futex_get_key(uintptr_t uaddr) {
    if (!current_process || !current_process->pmap) return NULL;

    /* The page must be present (mapped) at wait/wake time, but we key on the
     * virtual address, not the frame, so the key stays valid even if the
     * frame is later remapped. */
    if (pmap_extract(current_process->pmap, uaddr) == 0) return NULL;

    return (void *)uaddr;
}

/*
 * Safe userspace read (without triggering faults here)
 * Returns 0 on success, -EFAULT on failure.
 */
static int futex_read_user(int *uaddr, int *value) {
    if (!validate_uaddr((uintptr_t)uaddr)) return -EFAULT;

    /* Use copyin() for safe userspace access with fault handling */
    if (copyin(uaddr, value, sizeof(int)) != 0)
        return -EFAULT;

    return 0;
}

static int futex_write_user(int *uaddr, int value) {
    if (!validate_uaddr((uintptr_t)uaddr)) return -EFAULT;

    /* Use copyout() for safe userspace access with fault handling */
    if (copyout(&value, uaddr, sizeof(int)) != 0)
        return -EFAULT;

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
    
    *err = 0;
    
    /* Set up fault handler for userspace access */
    current_thread->on_fault = (uintptr_t)&&fault;
    
    /* Inline atomic cmpxchg on userspace memory */
    int prev;
    __asm__ volatile(
        "lock cmpxchgl %2, %1"
        : "=a"(prev), "+m"(*uaddr)
        : "r"(newval), "0"(oldval)
        : "memory"
    );
    
    current_thread->on_fault = 0;
    return prev;

fault:
    current_thread->on_fault = 0;
    *err = -EFAULT;
    return 0;
}

/* Forward declarations for PI functions */
int futex_lock_pi(int *uaddr, int detect, int trylock, int private, void *timeout);
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
            /* Futexes are keyed by virtual address and scoped per-process
             * (see futex_get_key / sys_futex); wake on the matching private
             * sleepq so robust-list cleanup actually reaches the waiter. */
            sleepq_wake_n_private(key, 1);
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
    
    /* Copy the robust_list_head from userspace into a kernel-stack local
     * to avoid direct dereferences of the userspace pointer */
    struct robust_list_head *uhead = t->robust_list;
    struct robust_list_head khead;
    if (copyin(uhead, &khead, sizeof(khead)) != 0) {
        t->robust_list = NULL;
        t->robust_list_len = 0;
        return;
    }
    
    struct robust_list *entry;
    int count = 0;
    const int MAX_ROBUST_WALK = 256;  /* Bound exit-path work for malformed robust lists */
    
    /* Process pending entry first (in case we died mid-lock/unlock) */
    if (khead.list_op_pending) {
        int *futex_addr = (int *)((char *)khead.list_op_pending + khead.futex_offset);
        int val;
        
        if (futex_read_user(futex_addr, &val) == 0) {
            if ((val & FUTEX_TID_MASK) == (uint32_t)t->tid) {
                futex_handle_dead_owner(futex_addr);
            }
        }
    }
    
    /* Walk the circular list.
     * The list head sentinel is at &uhead->list (userspace address),
     * so we compare against that for list termination. */
    entry = khead.list.next;
    while (entry != &uhead->list && count < MAX_ROBUST_WALK) {
        struct robust_list *next;
        
        /* Read next pointer safely before processing */
        if (futex_read_user((int *)&entry->next, (int *)&next) != 0) break;
        
        /* Calculate futex address from entry */
        int *futex_addr = (int *)((char *)entry + khead.futex_offset);
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

    /* Wake all waiters on the per-process private sleepq (futexes key by
     * virtual address scoped to the process — see futex_get_key/sys_futex).
     * This is the join / CLONE_CHILD_CLEARTID wakeup; using the shared
     * sleepq here would miss the joiner now that waiters park private. */
    sleepq_wake_all_private(key);
}

/*
 * sys_set_tid_address - register the calling thread's clear_child_tid.
 *
 * Linux returns the caller's TID; glibc reads that as the main thread's tid
 * at startup.  The pointer is stored as exit_tid_ptr, which the scheduler
 * zeroes and futex-wakes when the thread exits (sched_context_switch) -- the
 * same mechanism CLONE_CHILD_CLEARTID uses for pthread_join.
 */
int sys_set_tid_address(int *tidptr) {
    if (!current_thread) return 0;
    current_thread->exit_tid_ptr = tidptr;
    return current_thread->tid;
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
    
    /* Write results to userspace via copyout */
    if (copyout(&target->robust_list, head_ptr, sizeof(*head_ptr)) != 0)
        return -EFAULT;
    if (copyout(&target->robust_list_len, len_ptr, sizeof(*len_ptr)) != 0)
        return -EFAULT;
    
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

    /*
     * Every futex is keyed by VIRTUAL address and scoped to the calling
     * process (i.e. always the "private" sleepq path), whether or not the
     * caller set FUTEX_PRIVATE_FLAG.  A virtual key is stable across physical
     * page remaps (see futex_get_key); a physical key is not, and an unstable
     * key silently loses wakeups.  Scoping by PID keeps two processes that use
     * the same virtual address from colliding on one sleepq.  True
     * cross-process MAP_SHARED futexes are not used in substrate; were they
     * needed, the shared page would have to be pinned for the wait.
     */
    int private_flag = 1;

    /* key is the virtual address; futex_get_key validates the page is mapped
     * for WAIT-class ops (WAKE doesn't strictly need it, but the check is
     * cheap and rejects garbage addresses early).  WAKE on an unmapped word
     * legitimately finds no waiters, so don't hard-fail it on a NULL key. */
    void *key = (void *)uaddr;
    if (cmd == FUTEX_WAIT || cmd == FUTEX_WAIT_BITSET ||
        cmd == FUTEX_LOCK_PI || cmd == FUTEX_TRYLOCK_PI) {
        if (!futex_get_key((uintptr_t)uaddr))
            return -EFAULT;
    }

    switch (cmd) {
        case FUTEX_WAIT:
        case FUTEX_WAIT_BITSET: {
            /*
             * Atomic compare-and-sleep:
             * 1. Read current value
             * 2. If != expected val, return -EAGAIN
             * 3. Otherwise, sleep on the futex key
             *
             * FUTEX_WAIT_BITSET additionally registers a wait mask (val3)
             * so a FUTEX_WAKE_BITSET only wakes us if our bits overlap its
             * mask; its timeout is ABSOLUTE (vs FUTEX_WAIT's relative one).
             * A plain FUTEX_WAIT registers FUTEX_BITSET_MATCH_ANY so any
             * FUTEX_WAKE_BITSET (and every plain FUTEX_WAKE) can wake it.
             */
            int is_bitset = (cmd == FUTEX_WAIT_BITSET);
            uint32_t bitset = FUTEX_BITSET_MATCH_ANY;
            if (is_bitset) {
                bitset = (uint32_t)val3;
                if (bitset == 0)
                    return -EINVAL;
            }

            int current_val;
            if (futex_read_user(uaddr, &current_val) != 0) {
                return -EFAULT;
            }

            if (current_val != val) {
                return -EAGAIN;
            }

            current_thread->futex_bitset = bitset;

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
                uint64_t deadline;
                if (is_bitset) {
                    /*
                     * Absolute deadline.  Convert (abs_sec, abs_nsec) into a
                     * tick count relative to now; if already in the past the
                     * wait times out immediately.
                     */
                    time_t now_sec = get_time();
                    int64_t delta_sec = (int64_t)ts.tv_sec - (int64_t)now_sec;
                    int64_t ticks = delta_sec * (int64_t)hz +
                                    ((int64_t)ts.tv_nsec * (int64_t)hz) / 1000000000LL;
                    if (ticks <= 0) {
                        current_thread->futex_bitset = 0;
                        return -ETIMEDOUT;
                    }
                    deadline = get_ticks() + (uint64_t)ticks;
                } else {
                    uint64_t ticks = (uint64_t)ts.tv_sec * hz;
                    ticks += ((uint64_t)ts.tv_nsec * hz) / 1000000000ULL;
                    deadline = get_ticks() + ticks;
                }
                current_thread->sleep_expiry = deadline;
                current_thread->sleep_status = 0;
            }

            /*
             * Reset the sleep status for every wait (the timed branch above
             * only covers timeouts) so a stale -EINTR/-ETIMEDOUT from this
             * thread's previous sleep can't make us return spuriously, and
             * mark the sleep interruptible so a signal aborts it (matching
             * the sysv-semaphore wait pattern).
             */
            current_thread->sleep_status = 0;
            current_thread->flags |= THREAD_F_INTERRUPTIBLE;

            if (private_flag)
                sleepq_add_private(key, current_thread);
            else
                sleepq_add(key, current_thread);

            /*
             * Re-validate *uaddr now that we are on the sleep queue.  This
             * closes the lost-wakeup race: a waker on another CPU changes
             * *uaddr and then issues FUTEX_WAKE, which takes the same
             * sleepq bucket lock as sleepq_add() above.  Our first read
             * (way above) happened BEFORE we enqueued, so a wake landing in
             * that window would have found no waiter and been lost.  By
             * reading a second time only after sleepq_add(), we guarantee a
             * total order: either we observe the new value here and bail
             * with EAGAIN (we never sleep, so nothing is lost), or the
             * value is still 'val' -- in which case the waker's change (and
             * its wake) necessarily come after our enqueue and will find
             * us.  The read is done unlocked so a fault just unwinds.
             */
            {
                int recheck;
                int rr = futex_read_user(uaddr, &recheck);
                if (rr != 0 || recheck != val) {
                    sleepq_remove_thread(current_thread);
                    current_thread->flags &= ~THREAD_F_INTERRUPTIBLE;
                    if (timeout)
                        current_thread->sleep_expiry = 0;
                    return (rr != 0) ? -EFAULT : -EAGAIN;
                }
            }

            /*
             * A signal already pending (unmasked) before we sleep must abort
             * the wait now — otherwise we park forever.  This closes a
             * signal-before-block race: a directed signal (thr_kill /
             * pthread_cancel's SIGCANCEL) posted after the caller's user-space
             * lock-word check but before this point sets the pending bit and
             * runs signal_wake_thread() while we are NOT yet blocked, so no
             * wake is delivered.  Returning EINTR here lets the syscall-return
             * path run the handler (e.g. async cancel of a mutex/cond futex
             * waiter).  Only pending UNMASKED signals trip this, so the normal
             * (no-signal) fast path is unaffected.
             */
            if (current_thread->sig_pending & ~current_thread->sig_mask) {
                sleepq_remove_thread(current_thread);
                current_thread->flags &= ~THREAD_F_INTERRUPTIBLE;
                if (timeout)
                    current_thread->sleep_expiry = 0;
                return -EINTR;
            }

            sched_yield();

            current_thread->flags &= ~THREAD_F_INTERRUPTIBLE;

            /*
             * Self-remove from the sleepq on wake.  A real FUTEX_WAKE
             * already dequeued us, but a timeout (sched_tick) or signal
             * wake only flips us THREAD_READY and leaves the stale sleepq
             * entry linked -- sched_tick runs in the timer IRQ and cannot
             * take the bucket lock, so per the sleepq contract the waiter
             * unlinks itself using wait_chan.  Without this, a timed-out
             * waiter's dangling entry (keyed on this uaddr) is consumed by
             * the next FUTEX_WAKE on the same address, which then returns
             * "woke 1" while the real waiter is never woken and blocks for
             * its full timeout.  Idempotent if FUTEX_WAKE already removed us.
             */
            sleepq_remove_thread(current_thread);

            if (timeout) {
                current_thread->sleep_expiry = 0;
                if (current_thread->sleep_status == -ETIMEDOUT)
                    return -ETIMEDOUT;
            }

            /*
             * Signal interruption: signal_interrupt_thread() wakes us with
             * sleep_status == -EINTR; also honour any pending unblocked
             * signal directly (the interruptible flag was set above).
             */
            if (current_thread->sleep_status == -EINTR ||
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

        case FUTEX_WAKE_BITSET: {
            /*
             * Wake up to 'val' waiters whose registered wait mask overlaps
             * val3.  A val3 of 0 is invalid; FUTEX_BITSET_MATCH_ANY wakes
             * every waiter (a plain FUTEX_WAIT registers MATCH_ANY).
             */
            uint32_t mask = (uint32_t)val3;
            if (mask == 0)
                return -EINVAL;
            int ret;
            if (private_flag)
                ret = sleepq_wake_bitset_private(key, val, mask);
            else
                ret = sleepq_wake_bitset(key, val, mask);
            return ret;
        }

        case FUTEX_WAKE_OP: {
            /*
             * 1. Wake up to 'val' waiters on uaddr.
             * 2. oldval = *uaddr2; *uaddr2 = oldval <op> oparg.
             * 3. If oldval <cmp> cmparg, additionally wake up to 'val2'
             *    waiters on uaddr2.
             * Returns the total number of waiters woken from both addresses.
             * 'timeout' is repurposed as val2 (the uaddr2 wake limit).
             */
            if (!uaddr2 || !validate_uaddr((uintptr_t)uaddr2))
                return -EFAULT;

            int val2 = (int)(uintptr_t)timeout;

            unsigned int encoded = (unsigned int)val3;
            int op     = FUTEX_OP_OP(encoded);
            int cmp    = FUTEX_OP_CMP(encoded);
            int oparg  = FUTEX_OP_OPARG(encoded);
            int cmparg = FUTEX_OP_CMPARG(encoded);

            if (op & FUTEX_OP_OPARG_SHIFT) {
                op &= ~FUTEX_OP_OPARG_SHIFT;
                if (oparg < 0 || oparg > 31)
                    return -EINVAL;
                oparg = 1 << oparg;
            }

            int oldval;
            if (futex_read_user(uaddr2, &oldval) != 0)
                return -EFAULT;

            int newval;
            switch (op) {
                case FUTEX_OP_SET:  newval = oparg;            break;
                case FUTEX_OP_ADD:  newval = oldval + oparg;   break;
                case FUTEX_OP_OR:   newval = oldval | oparg;   break;
                case FUTEX_OP_ANDN: newval = oldval & ~oparg;  break;
                case FUTEX_OP_XOR:  newval = oldval ^ oparg;   break;
                default:            return -ENOSYS;
            }

            if (futex_write_user(uaddr2, newval) != 0)
                return -EFAULT;

            int cmp_result;
            switch (cmp) {
                case FUTEX_OP_CMP_EQ: cmp_result = (oldval == cmparg); break;
                case FUTEX_OP_CMP_NE: cmp_result = (oldval != cmparg); break;
                case FUTEX_OP_CMP_LT: cmp_result = (oldval <  cmparg); break;
                case FUTEX_OP_CMP_LE: cmp_result = (oldval <= cmparg); break;
                case FUTEX_OP_CMP_GT: cmp_result = (oldval >  cmparg); break;
                case FUTEX_OP_CMP_GE: cmp_result = (oldval >= cmparg); break;
                default:              return -ENOSYS;
            }

            void *key2;
            if (private_flag) {
                key2 = (void *)uaddr2;
            } else {
                key2 = futex_get_key((uintptr_t)uaddr2);
                if (!key2)
                    return -EFAULT;
            }

            int woken;
            if (private_flag)
                woken = sleepq_wake_n_private(key, val);
            else
                woken = sleepq_wake_n(key, val);

            if (cmp_result) {
                if (private_flag)
                    woken += sleepq_wake_n_private(key2, val2);
                else
                    woken += sleepq_wake_n(key2, val2);
            }

            return woken;
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
             * Priority Inheritance lock acquisition.  The FUTEX_LOCK_PI
             * timeout is absolute (CLOCK_REALTIME); NULL means block forever.
             */
            return futex_lock_pi(uaddr, 0, 0, private_flag, timeout);
        }

        case FUTEX_UNLOCK_PI: {
            /*
             * Priority Inheritance unlock
             */
            return futex_unlock_pi(uaddr, private_flag);
        }

        case FUTEX_TRYLOCK_PI: {
            /*
             * Non-blocking PI lock attempt (never sleeps, so no timeout).
             */
            return futex_lock_pi(uaddr, 0, 1, private_flag, NULL);
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
    thread_t          *owner;      /* Current owner thread (unheld cache) */
    int                owner_tid;  /* KERN-09: owner tid for safe re-lookup */
    int                owner_prio; /* Owner's original priority */
    pi_waiter_t       *waiters;    /* Priority-sorted waiter list */
    int                boosted_prio; /* Current boosted priority */
    struct pi_state   *next;       /* Hash chain link */
} pi_state_t;

#define PI_HASH_SIZE 64
#define PI_HASH_MASK (PI_HASH_SIZE - 1)
static pi_state_t *pi_hash[PI_HASH_SIZE];
static volatile uint32_t pi_lock = 0;

/*
 * PI state and waiter structs are kmalloc'd on demand.  The previous
 * implementation bump-allocated from fixed-size pools (32 states,
 * 128 waiters) and never freed — after enough distinct PI futex
 * addresses or contended waits the allocator silently returned NULL.
 * Now: kmalloc on alloc, kfree on the matching free path.
 */

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
    pi_state_t *ps = kmalloc(sizeof(*ps));
    if (!ps) return NULL;
    ps->key = NULL;
    ps->type = 0;
    ps->pid = 0;
    ps->owner = NULL;
    ps->owner_tid = 0;                 /* KERN-09 */
    ps->owner_prio = 0;
    ps->waiters = NULL;
    ps->boosted_prio = -1;            /* KERN-11: -1 = not boosted (0 is a
                                       * valid, highest, TIMESHARE priority) */
    ps->next = NULL;
    return ps;
}

static void pi_state_free(pi_state_t *ps) {
    if (ps) kfree(ps, sizeof(*ps));
}

static pi_waiter_t *pi_waiter_alloc(void) {
    pi_waiter_t *pw = kmalloc(sizeof(*pw));
    if (!pw) return NULL;
    pw->task = NULL;
    pw->priority = 0;
    pw->next = NULL;
    pw->key = NULL;
    return pw;
}

static void pi_waiter_free(pi_waiter_t *pw) {
    if (pw) kfree(pw, sizeof(*pw));
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

/*
 * KERN-11: true if priority number `a` outranks `b` (higher scheduling
 * precedence) under scheduling class `cls`.
 *
 * The two classes order oppositely (see runqueue_level_for_thread()):
 *   SCHED_REALTIME      - numerically larger  = higher priority
 *   SCHED_TIMESHARE/IDLE- numerically smaller = higher priority (0 is best)
 */
static int pi_prio_better(sched_class_t cls, int a, int b) {
    if (cls == SCHED_REALTIME)
        return a > b;
    return a < b;
}

/* Insert waiter in priority order (most favourable first) */
static void pi_insert_waiter(pi_state_t *ps, pi_waiter_t *pw) {
    /* KERN-11: rank by the waiter's own scheduling class so a TIMESHARE
     * waiter (lower number = higher priority) is not mis-ordered. */
    sched_class_t cls = pw->task ? pw->task->sched_class : SCHED_TIMESHARE;
    if (!ps->waiters || pi_prio_better(cls, pw->priority, ps->waiters->priority)) {
        pw->next = ps->waiters;
        ps->waiters = pw;
    } else {
        pi_waiter_t *cur = ps->waiters;
        while (cur->next && !pi_prio_better(cls, pw->priority, cur->next->priority)) {
            cur = cur->next;
        }
        pw->next = cur->next;
        cur->next = pw;
    }
}

/*
 * Remove a waiter from the list and free its storage.
 *
 * If after removal the pi_state has no waiters and no owner, it's
 * idle — unlink it from the hash and free it as well, so a long-lived
 * system doesn't accumulate one pi_state per ever-used PI futex
 * address.
 */
static void pi_remove_waiter(pi_state_t *ps, thread_t *t) {
    pi_waiter_t **pp = &ps->waiters;
    while (*pp) {
        if ((*pp)->task == t) {
            pi_waiter_t *gone = *pp;
            *pp = gone->next;
            pi_waiter_free(gone);
            break;
        }
        pp = &(*pp)->next;
    }

    if (ps->waiters || ps->owner) {
        return;
    }

    /* pi_state is idle — unlink from hash and free. */
    int hash = pi_hash_func(ps->key, ps->type, ps->pid);
    pi_state_t **link = &pi_hash[hash];
    while (*link) {
        if (*link == ps) {
            *link = ps->next;
            break;
        }
        link = &(*link)->next;
    }
    pi_state_free(ps);
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
    /*
     * KERN-09: ps->owner is an unheld cached pointer — the owner may have
     * exited and been reaped (robust/OWNER_DIED).  Re-resolve it by tid so we
     * never dereference freed thread storage.
     */
    thread_t *owner = ps->owner_tid ? sched_get_thread(ps->owner_tid) : NULL;
    if (!owner) { ps->owner = NULL; return; }
    ps->owner = owner;

    if (!ps->waiters) return;

    int top_prio = pi_top_waiter_prio(ps);
    sched_class_t cls = owner->sched_class;

    /* KERN-11: boost only when the top waiter is more favourable than the
     * boost already in effect, using this class' ordering sense. */
    if (ps->boosted_prio != -1 && !pi_prio_better(cls, top_prio, ps->boosted_prio))
        return;

    /* Save original priority on first boost */
    if (ps->boosted_prio == -1) {
        ps->owner_prio = owner->priority;
    }

    /* Set boosted priority */
    ps->boosted_prio = top_prio;
    sched_set_priority(owner->tid, cls, top_prio);
}

/*
 * Priority Inheritance futex lock
 *
 * 1. Try atomic acquire (CAS 0 -> TID)
 * 2. If contended, add to PI waiters and boost owner
 * 3. Sleep until woken
 * 4. On wake, retry acquire
 */
int futex_lock_pi(int *uaddr, int detect, int trylock, int private, void *timeout) {
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

    /*
     * KERN-05: resolve the (absolute, CLOCK_REALTIME) timeout into a tick
     * deadline before registering as a waiter, so a malformed timespec fails
     * cleanly and an already-expired deadline returns ETIMEDOUT without ever
     * sleeping.  NULL timeout => block indefinitely (but still interruptibly).
     */
    uint64_t deadline = 0;
    int has_timeout = 0;
    if (timeout) {
        struct timespec ts;
        if (futex_read_timespec(timeout, &ts) != 0)
            return -EFAULT;
        if (ts.tv_nsec < 0 || ts.tv_nsec >= 1000000000)
            return -EINVAL;
        uint64_t hz = get_hz();
        time_t now_sec = get_time();
        int64_t delta_sec = (int64_t)ts.tv_sec - (int64_t)now_sec;
        int64_t ticks = delta_sec * (int64_t)hz +
                        ((int64_t)ts.tv_nsec * (int64_t)hz) / 1000000000LL;
        if (ticks <= 0)
            return -ETIMEDOUT;
        deadline = get_ticks() + (uint64_t)ticks;
        has_timeout = 1;
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
            ps->owner_tid = owner_tid;   /* KERN-09 */
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
        
        /*
         * KERN-05: interruptible, timed sleep with a post-enqueue re-read.
         *
         * Arm the deadline and the interruptible flag BEFORE enqueuing so a
         * signal or timeout can abort the wait — without this a racing
         * futex_unlock_pi whose wake fires before we block parks this thread
         * forever (an unkillable PI-mutex hang).
         */
        current_thread->sleep_status = 0;
        if (has_timeout)
            current_thread->sleep_expiry = deadline;
        current_thread->flags |= THREAD_F_INTERRUPTIBLE;

        if (private)
            sleepq_add_private(key, current_thread);
        else
            sleepq_add(key, current_thread);

        /*
         * Re-read the lock word now that we are queued.  futex_unlock_pi
         * writes 0 to *uaddr and issues the wake under the same sleepq bucket
         * lock as sleepq_add() above, so a release that raced our enqueue is
         * observed here: if the word is free we skip the sleep and retry the
         * acquire below (nothing lost); otherwise the release necessarily
         * follows our enqueue and its wake will find us.
         */
        int should_sleep = 1;
        {
            int recheck;
            int rr = futex_read_user(uaddr, &recheck);
            if (rr != 0) {
                sleepq_remove_thread(current_thread);
                current_thread->flags &= ~THREAD_F_INTERRUPTIBLE;
                if (has_timeout)
                    current_thread->sleep_expiry = 0;
                pi_spinlock();
                pi_remove_waiter(ps, current_thread);
                pi_unlock();
                return -EFAULT;
            }
            if (recheck == 0)
                should_sleep = 0;   /* lock free — retry acquire, don't block */
        }

        /*
         * An unmasked signal already pending before we block must abort the
         * wait now, so pthread_cancel / thr_kill reaches a PI-mutex waiter
         * instead of hanging until the (possibly infinite) timeout.
         */
        if (should_sleep &&
            (current_thread->sig_pending & ~current_thread->sig_mask)) {
            sleepq_remove_thread(current_thread);
            current_thread->flags &= ~THREAD_F_INTERRUPTIBLE;
            if (has_timeout)
                current_thread->sleep_expiry = 0;
            pi_spinlock();
            pi_remove_waiter(ps, current_thread);
            pi_unlock();
            return -EINTR;
        }

        if (should_sleep)
            sched_yield();

        current_thread->flags &= ~THREAD_F_INTERRUPTIBLE;
        /*
         * Self-remove from the sleepq on wake.  A real wake already dequeued
         * us, but a timeout (sched_tick, IRQ context) or signal wake only
         * flips us READY and leaves the stale bucket entry — unlink it here
         * (idempotent if already dequeued).
         */
        sleepq_remove_thread(current_thread);

        if (has_timeout) {
            current_thread->sleep_expiry = 0;
            if (current_thread->sleep_status == -ETIMEDOUT) {
                pi_spinlock();
                pi_remove_waiter(ps, current_thread);
                pi_unlock();
                return -ETIMEDOUT;
            }
        }

        if (current_thread->sleep_status == -EINTR ||
            (current_thread->sig_pending & ~current_thread->sig_mask)) {
            pi_spinlock();
            pi_remove_waiter(ps, current_thread);
            pi_unlock();
            return -EINTR;
        }

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
            ps->owner_tid = tid;              /* KERN-09 */
            ps->owner_prio = current_thread->priority;
            ps->boosted_prio = -1;           /* KERN-11 */
            pi_unlock();
            return 0;
        }
        
        if (oldval & FUTEX_OWNER_DIED) {
            newval = tid | (oldval & FUTEX_WAITERS);
            if (futex_cmpxchg_user(uaddr, oldval, newval, &err) == oldval) {
                pi_spinlock();
                pi_remove_waiter(ps, current_thread);
                ps->owner = current_thread;
                ps->owner_tid = tid;              /* KERN-09 */
                ps->owner_prio = current_thread->priority;
                ps->boosted_prio = -1;           /* KERN-11 */
                pi_unlock();
                return -EOWNERDEAD;
            }
        }
        
        /* Lost race, loop back and re-boost if needed */
        pi_spinlock();
        int owner_tid = oldval & FUTEX_TID_MASK;
        /* KERN-09: compare against the stored tid, never deref the stale
         * cached ps->owner (it may have been reaped). */
        if (owner_tid && ps->owner_tid != owner_tid) {
            thread_t *new_owner = sched_get_thread(owner_tid);
            if (new_owner) {
                ps->owner = new_owner;
                ps->owner_tid = owner_tid;
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
    
    /*
     * Once the owner drops the PI futex, any inherited priority for that
     * ownership instance must be removed immediately. Remaining waiters will
     * be considered against the next owner after wakeup/acquisition.
     */
    if (ps) {
        if (ps->boosted_prio != -1) {
            /* KERN-09: re-resolve the owner by tid before touching it. */
            thread_t *owner = ps->owner_tid ? sched_get_thread(ps->owner_tid) : NULL;
            if (owner)
                sched_set_priority(owner->tid, owner->sched_class, ps->owner_prio);
            ps->boosted_prio = -1;           /* KERN-11 */
        }
        ps->owner = NULL;
        ps->owner_tid = 0;                    /* KERN-09 */

        /* If there are no remaining waiters either, the pi_state is
         * idle — unlink and free it.  Done here under pi_lock so a
         * concurrent contender doesn't fish a half-freed entry out
         * of the hash. */
        if (!ps->waiters) {
            int h = pi_hash_func(ps->key, ps->type, ps->pid);
            pi_state_t **link = &pi_hash[h];
            while (*link) {
                if (*link == ps) {
                    *link = ps->next;
                    break;
                }
                link = &(*link)->next;
            }
            pi_state_free(ps);
        }
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
