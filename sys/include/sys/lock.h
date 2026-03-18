#ifndef _SYS_LOCK_H
#define _SYS_LOCK_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    uint32_t locked;
    uint32_t cpu_id;
    const char *name;
} spinlock_t;

#define SPINLOCK_INIT(name) { 0, 0xFFFFFFFF, (name) }

void spinlock_init(spinlock_t *lock, const char *name);
void spinlock_acquire(spinlock_t *lock);
bool spinlock_try_acquire(spinlock_t *lock);
void spinlock_release(spinlock_t *lock);
bool spinlock_is_held(spinlock_t *lock);

struct thread;

// Sleep Mutex
typedef struct mutex {
    uint32_t locked;
    spinlock_t guard; // Protects wait queue/sleep state
    void     *owner; // thread_t*
    const char *name;
    struct mutex *owned_next; // Next mutex in owner thread's held list
} mutex_t;

void mutex_init(mutex_t *m, const char *name);
void mutex_lock(mutex_t *m);
bool mutex_trylock(mutex_t *m);
void mutex_unlock(mutex_t *m);
bool mutex_is_held(mutex_t *m);
int  mutex_release_owned_by_thread(struct thread *owner);

// Semaphore
typedef struct {
    int      value;
    spinlock_t lock; // Protects the value
    const char *name;
} semaphore_t;

void sema_init(semaphore_t *s, int value, const char *name);
void sema_wait(semaphore_t *s);
void sema_post(semaphore_t *s);
int  sema_getvalue(semaphore_t *s);

// Reader/writer lock
typedef struct {
    spinlock_t lock;
    uint32_t readers;
    uint32_t writer;
    uint32_t waiting_writers;
    void *owner; // thread_t* for write ownership
    const char *name;
} rwlock_t;

void rwlock_init(rwlock_t *rw, const char *name);
void rw_rlock(rwlock_t *rw);
bool rw_try_rlock(rwlock_t *rw);
void rw_runlock(rwlock_t *rw);
void rw_wlock(rwlock_t *rw);
bool rw_try_wlock(rwlock_t *rw);
void rw_wunlock(rwlock_t *rw);
bool rw_wowned(rwlock_t *rw);

/*
 * Lockmgr - BSD-style kernel lock manager
 *
 * Provides unified shared/exclusive locking with upgrade, downgrade,
 * drain, and priority inheritance support.
 */

/* Lock operation flags (passed to lockmgr) */
#define LK_SHARED       0x0001  /* Shared lock (multiple readers) */
#define LK_EXCLUSIVE    0x0002  /* Exclusive lock (single writer) */
#define LK_UPGRADE      0x0004  /* Upgrade shared to exclusive */
#define LK_DOWNGRADE    0x0008  /* Downgrade exclusive to shared */
#define LK_RELEASE      0x0040  /* Release the lock */
#define LK_DRAIN        0x0080  /* Wait for all activity to end */
#define LK_NOWAIT       0x0010  /* Don't sleep (return EBUSY) */
#define LK_RETRY        0x0020  /* Retry on failure */

/* Internal lock status flags (stored in lk_flags) */
#define LK_HAVE_EXCL    0x0100  /* Exclusive lock held */
#define LK_WANT_EXCL    0x0200  /* Exclusive lock wanted */
#define LK_WANT_DRAIN   0x0400  /* Drain requested */
#define LK_DRAINED      0x0800  /* Lock has been drained */

struct lock {
    spinlock_t      lk_interlock;       /* Protects lock fields */
    uint32_t        lk_flags;           /* Status flags (LK_HAVE_EXCL, etc.) */
    uint32_t        lk_sharecount;      /* Number of shared holders */
    uint32_t        lk_waitcount;       /* Number of threads waiting */
    uint32_t        lk_exclusivecount;  /* Recursive exclusive count */
    struct thread   *lk_lockholder;     /* Exclusive lock owner */
    int             lk_prio;            /* Priority for sleep */
    const char      *lk_name;           /* Lock name for debugging */
};

void    lockinit(struct lock *lkp, int prio, const char *name, int flags);
void    lockdestroy(struct lock *lkp);
int     lockmgr(struct lock *lkp, uint32_t flags, spinlock_t *interlock);
int     lockstatus(struct lock *lkp);
int     lockcount(struct lock *lkp);

#endif
