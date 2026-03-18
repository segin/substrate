/*
 * lockmgr.c - BSD-style kernel lock manager
 *
 * Provides unified shared/exclusive locking with upgrade, downgrade,
 * drain, and priority inheritance (via turnstiles).
 */

#include <sys/types.h>
#include <sys/lock.h>
#include <sys/proc.h>
#include <sys/errno.h>
#include <kern/sched.h>
#include <kern/sleepq.h>
#include <kern/panic.h>
#include <string.h>

/* Turnstile support for priority inheritance */
extern void turnstile_block(void *lockobj, struct thread *owner);
extern void turnstile_release(void *lockobj);

/*
 * lockinit - Initialize a lockmgr lock
 */
void
lockinit(struct lock *lkp, int prio, const char *name,
         int flags __attribute__((unused)))
{
    memset(lkp, 0, sizeof(*lkp));
    spinlock_init(&lkp->lk_interlock, name ? name : "lock");
    lkp->lk_flags = 0;
    lkp->lk_sharecount = 0;
    lkp->lk_waitcount = 0;
    lkp->lk_exclusivecount = 0;
    lkp->lk_lockholder = NULL;
    lkp->lk_prio = prio;
    lkp->lk_name = name;
}

/*
 * lockdestroy - Destroy a lockmgr lock
 */
void    lockdestroy(struct lock *lkp)
{
    if (lkp->lk_sharecount != 0 || lkp->lk_exclusivecount != 0)
        panic("lockdestroy: lock still held");
    lkp->lk_flags = 0;
    lkp->lk_lockholder = NULL;
}

/*
 * lockmgr - Unified lock manager
 *
 * Operations:
 *   LK_SHARED    - Acquire shared (reader) lock
 *   LK_EXCLUSIVE - Acquire exclusive (writer) lock
 *   LK_UPGRADE   - Upgrade shared to exclusive
 *   LK_DOWNGRADE - Downgrade exclusive to shared
 *   LK_RELEASE   - Release the lock
 *   LK_DRAIN     - Wait for all holders to release
 *
 * Modifier flags:
 *   LK_NOWAIT    - Return EBUSY instead of blocking
 *
 * If interlock is non-NULL, it is released before sleeping and
 * reacquired after waking.
 *
 * Returns 0 on success, or an error code.
 */
int
lockmgr(struct lock *lkp, uint32_t flags, spinlock_t *interlock)
{
    int error = 0;
    struct thread *td = current_thread;
    uint32_t op = flags & (LK_SHARED | LK_EXCLUSIVE | LK_UPGRADE |
                           LK_DOWNGRADE | LK_RELEASE | LK_DRAIN);

    spinlock_acquire(&lkp->lk_interlock);

    switch (op) {

    case LK_SHARED:
        /*
         * Acquire shared lock.
         * Wait if exclusive lock is held or wanted (writer preference).
         */
        while (lkp->lk_flags & (LK_HAVE_EXCL | LK_WANT_EXCL | LK_WANT_DRAIN)) {
            if (flags & LK_NOWAIT) {
                error = EBUSY;
                break;
            }
            lkp->lk_waitcount++;
            if (lkp->lk_lockholder)
                turnstile_block(lkp, lkp->lk_lockholder);
            sleepq_add(lkp, td);
            if (interlock)
                spinlock_release(interlock);
            spinlock_release(&lkp->lk_interlock);
            sched_yield();
            spinlock_acquire(&lkp->lk_interlock);
            if (interlock)
                spinlock_acquire(interlock);
            lkp->lk_waitcount--;
        }
        if (error == 0)
            lkp->lk_sharecount++;
        break;

    case LK_EXCLUSIVE:
        /*
         * Acquire exclusive lock.
         * Wait for all shared and exclusive holders to release.
         */
        if (lkp->lk_lockholder == td) {
            /* Recursive exclusive - increment count */
            lkp->lk_exclusivecount++;
            break;
        }
        lkp->lk_flags |= LK_WANT_EXCL;
        while (lkp->lk_sharecount > 0 ||
               (lkp->lk_flags & LK_HAVE_EXCL)) {
            if (flags & LK_NOWAIT) {
                lkp->lk_flags &= ~LK_WANT_EXCL;
                error = EBUSY;
                break;
            }
            lkp->lk_waitcount++;
            if (lkp->lk_lockholder)
                turnstile_block(lkp, lkp->lk_lockholder);
            sleepq_add(lkp, td);
            if (interlock)
                spinlock_release(interlock);
            spinlock_release(&lkp->lk_interlock);
            sched_yield();
            spinlock_acquire(&lkp->lk_interlock);
            if (interlock)
                spinlock_acquire(interlock);
            lkp->lk_waitcount--;
        }
        if (error == 0) {
            lkp->lk_flags &= ~LK_WANT_EXCL;
            lkp->lk_flags |= LK_HAVE_EXCL;
            lkp->lk_lockholder = td;
            lkp->lk_exclusivecount = 1;
        }
        break;

    case LK_UPGRADE:
        /*
         * Upgrade from shared to exclusive.
         * Only one upgrader is permitted at a time; if another thread
         * is already upgrading, fail with EBUSY (caller must release
         * the shared lock and re-acquire exclusive).
         */
        if (lkp->lk_sharecount == 0) {
            panic("lockmgr: upgrade without shared lock");
        }
        if (lkp->lk_flags & LK_WANT_UPGRADE) {
            /* Another upgrader is already waiting - cannot proceed */
            error = EBUSY;
            break;
        }
        lkp->lk_sharecount--;
        lkp->lk_flags |= LK_WANT_EXCL | LK_WANT_UPGRADE;
        while (lkp->lk_sharecount > 0 ||
               (lkp->lk_flags & LK_HAVE_EXCL)) {
            if (flags & LK_NOWAIT) {
                /* Restore shared hold on failure */
                lkp->lk_sharecount++;
                lkp->lk_flags &= ~(LK_WANT_EXCL | LK_WANT_UPGRADE);
                error = EBUSY;
                break;
            }
            lkp->lk_waitcount++;
            sleepq_add(lkp, td);
            if (interlock)
                spinlock_release(interlock);
            spinlock_release(&lkp->lk_interlock);
            sched_yield();
            spinlock_acquire(&lkp->lk_interlock);
            if (interlock)
                spinlock_acquire(interlock);
            lkp->lk_waitcount--;
        }
        if (error == 0) {
            lkp->lk_flags &= ~(LK_WANT_EXCL | LK_WANT_UPGRADE);
            lkp->lk_flags |= LK_HAVE_EXCL;
            lkp->lk_lockholder = td;
            lkp->lk_exclusivecount = 1;
        }
        break;

    case LK_DOWNGRADE:
        /*
         * Downgrade from exclusive to shared.
         */
        if (!(lkp->lk_flags & LK_HAVE_EXCL) || lkp->lk_lockholder != td) {
            panic("lockmgr: downgrade without exclusive lock");
        }
        lkp->lk_flags &= ~LK_HAVE_EXCL;
        lkp->lk_lockholder = NULL;
        lkp->lk_exclusivecount = 0;
        lkp->lk_sharecount = 1;
        turnstile_release(lkp);
        /* Wake waiters who can now acquire shared */
        if (lkp->lk_waitcount > 0)
            sleepq_wake_all(lkp);
        break;

    case LK_RELEASE:
        if (lkp->lk_flags & LK_HAVE_EXCL) {
            /* Releasing exclusive lock */
            if (lkp->lk_lockholder != td) {
                panic("lockmgr: release by non-owner");
            }
            lkp->lk_exclusivecount--;
            if (lkp->lk_exclusivecount == 0) {
                lkp->lk_flags &= ~LK_HAVE_EXCL;
                lkp->lk_lockholder = NULL;
                turnstile_release(lkp);
                if (lkp->lk_waitcount > 0)
                    sleepq_wake_all(lkp);
            }
        } else if (lkp->lk_sharecount > 0) {
            /* Releasing shared lock */
            lkp->lk_sharecount--;
            if (lkp->lk_sharecount == 0 && lkp->lk_waitcount > 0)
                sleepq_wake_all(lkp);
        } else {
            panic("lockmgr: release of unlocked lock");
        }

        /* Check if drain is satisfied */
        if ((lkp->lk_flags & LK_WANT_DRAIN) &&
            lkp->lk_sharecount == 0 && lkp->lk_exclusivecount == 0) {
            lkp->lk_flags &= ~LK_WANT_DRAIN;
            lkp->lk_flags |= LK_DRAINED;
            sleepq_wake_all(lkp);
        }
        break;

    case LK_DRAIN:
        /*
         * Wait for all activity to end.
         * Used during vnode reclamation.
         */
        lkp->lk_flags |= LK_WANT_DRAIN;
        while (lkp->lk_sharecount > 0 || lkp->lk_exclusivecount > 0) {
            if (flags & LK_NOWAIT) {
                lkp->lk_flags &= ~LK_WANT_DRAIN;
                error = EBUSY;
                break;
            }
            lkp->lk_waitcount++;
            sleepq_add(lkp, td);
            if (interlock)
                spinlock_release(interlock);
            spinlock_release(&lkp->lk_interlock);
            sched_yield();
            spinlock_acquire(&lkp->lk_interlock);
            if (interlock)
                spinlock_acquire(interlock);
            lkp->lk_waitcount--;
        }
        if (error == 0) {
            lkp->lk_flags &= ~LK_WANT_DRAIN;
            lkp->lk_flags |= LK_DRAINED;
        }
        break;

    default:
        panic("lockmgr: unknown op");
    }

    spinlock_release(&lkp->lk_interlock);
    return error;
}

/*
 * lockstatus - Query lock status
 *
 * Returns:
 *   LK_EXCLUSIVE if exclusively locked
 *   LK_SHARED    if share-locked
 *   0            if unlocked
 */
int
lockstatus(struct lock *lkp)
{
    int status;

    spinlock_acquire(&lkp->lk_interlock);
    if (lkp->lk_flags & LK_HAVE_EXCL)
        status = LK_EXCLUSIVE;
    else if (lkp->lk_sharecount > 0)
        status = LK_SHARED;
    else
        status = 0;
    spinlock_release(&lkp->lk_interlock);

    return status;
}

/*
 * lockcount - Return total lock hold count
 */
int
lockcount(struct lock *lkp)
{
    int count;

    spinlock_acquire(&lkp->lk_interlock);
    count = (int)(lkp->lk_sharecount + lkp->lk_exclusivecount);
    spinlock_release(&lkp->lk_interlock);

    return count;
}
