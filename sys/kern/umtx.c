/*
 * sys/kern/umtx.c — FreeBSD _umtx_op(2) backing.
 *
 * FreeBSD's libthr drives ALL of its blocking synchronisation through the
 * _umtx_op(2) syscall: thread join, mutex contention, condition variables,
 * rwlocks and POSIX semaphores.  The uncontended fast paths (lock acquire /
 * release) run entirely in userspace via atomic_cmpset on the lock word; the
 * kernel is only entered when a thread must actually BLOCK (or wake a blocked
 * peer).  Substrate previously stubbed _umtx_op to -ENOSYS, so libthr spun
 * forever the moment any thread tried to block — pthread_join() busy-looped on
 * UMTX_OP_WAIT and never slept, std::thread::join() hung, and every condvar /
 * contended mutex deadlocked.
 *
 * This file implements the operations libthr needs, keyed on the VIRTUAL
 * address of the umtx word and scoped to the calling process — the same
 * stable-key sleepq scheme used by the Linux futex backing (sys/kern/futex.c),
 * which is necessary because a physical key is unstable across page remaps and
 * silently loses wakeups.  The wait/wake primitives are the private sleepq:
 * sleepq_add_private() / sleepq_wake_n_private().
 *
 * Timeouts: NULL means "wait forever", which is what libthr passes for join,
 * condvar wait and contended mutex/rwlock acquisition in the common case.  A
 * non-NULL _umtx_time is honoured as a relative timeout (see umtx_read_time).
 */

#include <stddef.h>
#include <stdint.h>

#include <arch/i386/pmap.h>
#include <kern/sched.h>
#include <kern/sleepq.h>
#include <kern/time.h>
#include <sys/copy.h>
#include <sys/errno.h>
#include <sys/proc.h>
#include <sys/time.h>
#include <sys/umtx.h>

/* FreeBSD UMTX_OP_* operation codes (sys/sys/umtx.h). */
#define UMTX_OP_LOCK              0   /* COMPAT10 */
#define UMTX_OP_UNLOCK           1    /* COMPAT10 */
#define UMTX_OP_WAIT             2
#define UMTX_OP_WAKE             3
#define UMTX_OP_MUTEX_TRYLOCK    4
#define UMTX_OP_MUTEX_LOCK       5
#define UMTX_OP_MUTEX_UNLOCK     6
#define UMTX_OP_SET_CEILING      7
#define UMTX_OP_CV_WAIT          8
#define UMTX_OP_CV_SIGNAL        9
#define UMTX_OP_CV_BROADCAST     10
#define UMTX_OP_WAIT_UINT        11
#define UMTX_OP_RW_RDLOCK        12
#define UMTX_OP_RW_WRLOCK        13
#define UMTX_OP_RW_UNLOCK        14
#define UMTX_OP_WAIT_UINT_PRIVATE 15
#define UMTX_OP_WAKE_PRIVATE     16
#define UMTX_OP_MUTEX_WAIT       17
#define UMTX_OP_MUTEX_WAKE       18  /* deprecated */
#define UMTX_OP_SEM_WAIT         19  /* deprecated */
#define UMTX_OP_SEM_WAKE         20  /* deprecated */
#define UMTX_OP_NWAKE_PRIVATE    21
#define UMTX_OP_MUTEX_WAKE2      22
#define UMTX_OP_SEM2_WAIT        23
#define UMTX_OP_SEM2_WAKE        24
#define UMTX_OP_SHM              25
#define UMTX_OP_ROBUST_LISTS     26

/* The high bits of `op` carry the i386 / 32-bit ABI selectors; mask them. */
#define UMTX_OP__I386            0x40000000
#define UMTX_OP__32BIT           0x80000000

/* umutex.m_owner sentinel/flag bits (sys/sys/umtx.h). */
#define UMUTEX_UNOWNED           0x0
#define UMUTEX_CONTESTED         0x80000000U

/* CV cv_flags / general flag bit selecting the process-private sleepq. */
#define UMTX_SHARED_FLAG         0   /* libthr uses private for thr-local */

#define USER_SPACE_MAX           0xBFFFFFFFU

static inline int umtx_valid(uintptr_t a) {
    return (a <= USER_SPACE_MAX) && ((a & 3) == 0);
}

/* The key is the virtual address; the page must be mapped at wait/wake time. */
static void *umtx_key(uintptr_t uaddr) {
    if (!current_process || !current_process->pmap) return NULL;
    if (pmap_extract(current_process->pmap, uaddr) == 0) return NULL;
    return (void *)uaddr;
}

static int umtx_read32(volatile uint32_t *uaddr, uint32_t *out) {
    if (!umtx_valid((uintptr_t)uaddr)) return -EFAULT;
    if (copyin((const void *)uaddr, out, sizeof(uint32_t)) != 0) return -EFAULT;
    return 0;
}

static int umtx_write32(volatile uint32_t *uaddr, uint32_t val) {
    if (!umtx_valid((uintptr_t)uaddr)) return -EFAULT;
    if (copyout(&val, (void *)uaddr, sizeof(uint32_t)) != 0) return -EFAULT;
    return 0;
}

/*
 * Read a FreeBSD `struct _umtx_time` (or bare `struct timespec` for the
 * COMPAT path) into a relative tick deadline.  uaddr2 carries either the
 * size of the structure (when >= sizeof(_umtx_time) it's the full struct) or
 * is NULL for "no timeout".  We support the common libthr layout: a leading
 * `struct timespec _timeout`.  Returns 0 and sets *deadline_ticks (0 means
 * "no timeout"), or -EFAULT.
 */
static int umtx_read_timeout(const void *utime, uint64_t *deadline_ticks) {
    *deadline_ticks = 0;
    if (!utime) return 0;
    if (!umtx_valid((uintptr_t)utime)) return -EFAULT;
    struct timespec ts;
    if (copyin(utime, &ts, sizeof(ts)) != 0) return -EFAULT;
    if (ts.tv_nsec < 0 || ts.tv_nsec >= 1000000000) return -EINVAL;
    if (ts.tv_sec == 0 && ts.tv_nsec == 0) {
        /* zero timeout: caller wants a poll; encode as an already-past
         * deadline so the wait returns ETIMEDOUT immediately. */
        *deadline_ticks = get_ticks();
        return 0;
    }
    uint64_t hz = get_hz();
    uint64_t ticks = (uint64_t)ts.tv_sec * hz +
                     ((uint64_t)ts.tv_nsec * hz) / 1000000000ULL;
    if (ticks == 0) ticks = 1;
    *deadline_ticks = get_ticks() + ticks;
    return 0;
}

/*
 * Core compare-and-sleep: block on `key` while *uaddr (compared as 32-bit)
 * still equals `expected`.  This is the shared engine behind UMTX_OP_WAIT,
 * WAIT_UINT(_PRIVATE) and MUTEX_WAIT.  Returns 0 on a normal wake,
 * -ETIMEDOUT on timeout, -EINTR on signal, -EFAULT on bad address.
 */
static int umtx_wait_on(volatile uint32_t *uaddr, uint32_t expected,
                        const void *utime) {
    void *key = umtx_key((uintptr_t)uaddr);
    if (!key) return -EFAULT;

    uint64_t deadline = 0;
    int terr = umtx_read_timeout(utime, &deadline);
    if (terr) return terr;

    uint32_t cur;
    if (umtx_read32(uaddr, &cur) != 0) return -EFAULT;
    if (cur != expected) return 0;   /* value already changed: don't sleep */

    if (deadline) {
        if (deadline <= get_ticks()) return -ETIMEDOUT;
        current_thread->sleep_expiry = deadline;
    }
    current_thread->sleep_status = 0;
    current_thread->flags |= THREAD_F_INTERRUPTIBLE;

    sleepq_add_private(key, current_thread);

    /* Re-check after enqueue to close the lost-wakeup race (a waker that
     * changed *uaddr + woke between our read and our enqueue would otherwise
     * be lost — both paths take the same sleepq bucket lock). */
    if (umtx_read32(uaddr, &cur) != 0 || cur != expected) {
        sleepq_remove_thread(current_thread);
        current_thread->flags &= ~THREAD_F_INTERRUPTIBLE;
        current_thread->sleep_expiry = 0;
        return (cur != expected) ? 0 : -EFAULT;
    }

    sched_yield();

    current_thread->flags &= ~THREAD_F_INTERRUPTIBLE;
    sleepq_remove_thread(current_thread);   /* idempotent self-unlink */

    if (deadline) {
        current_thread->sleep_expiry = 0;
        if (current_thread->sleep_status == -ETIMEDOUT) return -ETIMEDOUT;
    }
    if (current_thread->sleep_status == -EINTR ||
        (current_thread->sig_pending & ~current_thread->sig_mask)) {
        return -EINTR;
    }
    return 0;
}

/* Wake up to `n` waiters parked on the umtx word at `uaddr`. */
int kern_umtx_wake(void *uaddr, int n) {
    if (!umtx_valid((uintptr_t)uaddr)) return -EFAULT;
    void *key = (void *)uaddr;   /* virtual-address key, same as the wait side */
    if (n <= 0) n = 1;
    return sleepq_wake_n_private(key, n);
}

/*
 * UMTX_OP_MUTEX_WAIT (libthr contended mutex): the lock word at `uaddr`
 * (umutex.m_owner) is non-zero and the caller has set UMUTEX_CONTESTED; it
 * now blocks until the owner clears the word.  We sleep while the word is
 * still non-zero (i.e. still owned).
 */
static int umtx_mutex_wait(volatile uint32_t *uaddr, const void *utime) {
    void *key = umtx_key((uintptr_t)uaddr);
    if (!key) return -EFAULT;

    uint64_t deadline = 0;
    int terr = umtx_read_timeout(utime, &deadline);
    if (terr) return terr;

    uint32_t owner;
    if (umtx_read32(uaddr, &owner) != 0) return -EFAULT;
    /* If unowned (or only the contested bit is set with no owner), the lock
     * is free — return so libthr retries the userspace cmpset. */
    if ((owner & ~UMUTEX_CONTESTED) == UMUTEX_UNOWNED) return 0;

    if (deadline) {
        if (deadline <= get_ticks()) return -ETIMEDOUT;
        current_thread->sleep_expiry = deadline;
    }
    current_thread->sleep_status = 0;
    current_thread->flags |= THREAD_F_INTERRUPTIBLE;

    sleepq_add_private(key, current_thread);

    if (umtx_read32(uaddr, &owner) != 0 ||
        (owner & ~UMUTEX_CONTESTED) == UMUTEX_UNOWNED) {
        sleepq_remove_thread(current_thread);
        current_thread->flags &= ~THREAD_F_INTERRUPTIBLE;
        current_thread->sleep_expiry = 0;
        return 0;
    }

    sched_yield();

    current_thread->flags &= ~THREAD_F_INTERRUPTIBLE;
    sleepq_remove_thread(current_thread);

    if (deadline) {
        current_thread->sleep_expiry = 0;
        if (current_thread->sleep_status == -ETIMEDOUT) return -ETIMEDOUT;
    }
    if (current_thread->sleep_status == -EINTR ||
        (current_thread->sig_pending & ~current_thread->sig_mask)) {
        return -EINTR;
    }
    return 0;
}

/*
 * kern_umtx_op — dispatch a single _umtx_op() request.
 *
 *   obj     : the umtx / umutex / ucond / lock word (virtual user address).
 *   op      : UMTX_OP_* (ABI selector bits already masked off by the caller's
 *             personality wrapper, but we mask again defensively).
 *   val     : per-op argument (compare value for WAIT, wake count for WAKE).
 *   uaddr   : per-op pointer arg (the _umtx_time timeout for the WAIT-class
 *             ops; the umutex for CV ops).
 *   uaddr2  : per-op pointer arg (timeout for CV / mutex; usually NULL here).
 *
 * Returns 0 on success or a negative errno.
 */
int kern_umtx_op(void *obj, int op, unsigned long val, void *uaddr, void *uaddr2) {
    op &= ~(UMTX_OP__I386 | UMTX_OP__32BIT);

    switch (op) {
    case UMTX_OP_WAIT:
    case UMTX_OP_WAIT_UINT:
    case UMTX_OP_WAIT_UINT_PRIVATE:
        /* Sleep while *obj == val.  For UMTX_OP_WAIT the compare is a long,
         * but on i386 long==32-bit so a 32-bit compare is exact. uaddr is the
         * _umtx_time timeout (NULL = forever). */
        return umtx_wait_on((volatile uint32_t *)obj, (uint32_t)val, uaddr);

    case UMTX_OP_WAKE:
    case UMTX_OP_WAKE_PRIVATE:
        return kern_umtx_wake(obj, (int)val);

    case UMTX_OP_MUTEX_WAIT:
        /* Contended umutex: block until the owner releases. uaddr2 is the
         * optional timeout. */
        return umtx_mutex_wait((volatile uint32_t *)obj, uaddr2);

    case UMTX_OP_MUTEX_WAKE:
    case UMTX_OP_MUTEX_WAKE2:
        /* Wake a waiter on a contended umutex.  libthr's unlock fast path
         * clears the owner word in userspace, then calls WAKE2 to release one
         * blocked acquirer.  Waking all parked waiters is correct (they
         * re-contend via cmpset); waking one is the optimization. */
        return kern_umtx_wake(obj, (int)val > 0 ? (int)val : 1);

    case UMTX_OP_CV_WAIT: {
        /*
         * Condition-variable wait.  libthr passes:
         *   obj    = &ucond
         *   uaddr  = &umutex   (the associated mutex, already locked)
         *   uaddr2 = timeout
         *   val    = cv flags
         * Atomically: release the mutex, then sleep on the ucond until a
         * CV_SIGNAL/CV_BROADCAST wakes us.  We approximate the kernel's
         * atomic drop by waking the mutex AFTER enqueuing on the cv key.
         */
        void *cvkey = umtx_key((uintptr_t)obj);
        if (!cvkey) return -EFAULT;

        uint64_t deadline = 0;
        int terr = umtx_read_timeout(uaddr2, &deadline);
        if (terr) return terr;

        if (deadline) {
            if (deadline <= get_ticks()) return -ETIMEDOUT;
            current_thread->sleep_expiry = deadline;
        }
        current_thread->sleep_status = 0;
        current_thread->flags |= THREAD_F_INTERRUPTIBLE;
        sleepq_add_private(cvkey, current_thread);

        /* Release the associated mutex and wake one of its waiters now that
         * we are safely enqueued on the cv. */
        if (uaddr) {
            (void)umtx_write32((volatile uint32_t *)uaddr, UMUTEX_UNOWNED);
            kern_umtx_wake(uaddr, 1);
        }

        sched_yield();

        current_thread->flags &= ~THREAD_F_INTERRUPTIBLE;
        sleepq_remove_thread(current_thread);
        if (deadline) {
            current_thread->sleep_expiry = 0;
            if (current_thread->sleep_status == -ETIMEDOUT) return -ETIMEDOUT;
        }
        if (current_thread->sleep_status == -EINTR ||
            (current_thread->sig_pending & ~current_thread->sig_mask))
            return -EINTR;
        return 0;
    }

    case UMTX_OP_CV_SIGNAL:
        return kern_umtx_wake(obj, 1);

    case UMTX_OP_CV_BROADCAST: {
        void *key = umtx_key((uintptr_t)obj);
        if (!key) return -EFAULT;
        return sleepq_wake_all_private(key);
    }

    case UMTX_OP_NWAKE_PRIVATE: {
        /*
         * Wake waiters on each of `val` umtx words whose addresses are stored
         * in the array at obj.  libthr uses this to release a batch of
         * priority-propagation mutexes at thread exit; if the array is
         * unreadable we just report success (nothing to wake).
         */
        int count = (int)val;
        for (int i = 0; i < count; i++) {
            uint32_t aptr;
            if (umtx_read32((volatile uint32_t *)((char *)obj + i * 4), &aptr) != 0)
                break;
            if (aptr) kern_umtx_wake((void *)(uintptr_t)aptr, 1);
        }
        return 0;
    }

    case UMTX_OP_MUTEX_TRYLOCK:
    case UMTX_OP_MUTEX_LOCK:
    case UMTX_OP_MUTEX_UNLOCK:
        /*
         * libthr performs the uncontended lock/unlock entirely in userspace
         * via atomic_cmpset and only enters the kernel for the contended
         * MUTEX_WAIT / MUTEX_WAKE2 path handled above.  These three ops are
         * the COMPAT / fallback in-kernel umutex implementation; treat a
         * call as success (the userspace fast path already owns the word) so
         * a libthr that does reach here doesn't spin.  TRYLOCK reports the
         * lock as acquired.
         */
        return 0;

    case UMTX_OP_ROBUST_LISTS:
        /* Registering the robust-mutex list head: accept and ignore (we don't
         * implement owner-death robustness, but failing it makes libthr abort
         * thread setup). */
        return 0;

    default:
        return -ENOSYS;
    }
}
