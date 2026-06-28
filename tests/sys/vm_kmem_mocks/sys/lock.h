#ifndef _SYS_LOCK_H
#define _SYS_LOCK_H

/*
 * Host-test mock of <sys/lock.h>.
 *
 * The real header's spinlock_acquire_irq()/spinlock_release_irq()
 * helpers execute `cli`/`pushfl`/`popfl` inline asm — privileged
 * instructions that fault (SIGSEGV) when run in a host userspace
 * process and don't even assemble in the host's 64-bit mode.  This
 * mock keeps the identical types and signatures but replaces the
 * IRQ-save bodies with host-safe no-ops so vm_kmem.c can be
 * exercised on the host.  All other declarations mirror the real
 * header verbatim.
 */

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    uint32_t locked;
    uint32_t cpu_id;
    const char *name;
    uintptr_t last_acquire_eip;
} spinlock_t;

#define SPINLOCK_INIT(name) { 0, 0xFFFFFFFF, (name), 0 }

void spinlock_init(spinlock_t *lock, const char *name);
void spinlock_acquire(spinlock_t *lock);
bool spinlock_try_acquire(spinlock_t *lock);
void spinlock_release(spinlock_t *lock);
bool spinlock_is_held(spinlock_t *lock);

/* Host-safe IRQ-save spinlocks: no cli/pushfl/popfl. */
static inline unsigned long spinlock_acquire_irq(spinlock_t *lock) {
    spinlock_acquire(lock);
    return 0;
}

static inline void spinlock_release_irq(spinlock_t *lock, unsigned long flags) {
    (void)flags;
    spinlock_release(lock);
}

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

/* Lockmgr - BSD-style kernel lock manager */
#define LK_SHARED       0x0001
#define LK_EXCLUSIVE    0x0002
#define LK_UPGRADE      0x0004
#define LK_DOWNGRADE    0x0008
#define LK_RELEASE      0x0040
#define LK_DRAIN        0x0080
#define LK_NOWAIT       0x0010
#define LK_RETRY        0x0020

#define LK_HAVE_EXCL    0x0100
#define LK_WANT_EXCL    0x0200
#define LK_WANT_DRAIN   0x0400
#define LK_DRAINED      0x0800
#define LK_WANT_UPGRADE 0x1000

struct lock {
    spinlock_t      lk_interlock;
    uint32_t        lk_flags;
    uint32_t        lk_sharecount;
    uint32_t        lk_waitcount;
    uint32_t        lk_exclusivecount;
    struct thread   *lk_lockholder;
    int             lk_prio;
    const char      *lk_name;
};

void    lockinit(struct lock *lkp, int prio, const char *name, int flags);
void    lockdestroy(struct lock *lkp);
int     lockmgr(struct lock *lkp, uint32_t flags, spinlock_t *interlock);
int     lockstatus(struct lock *lkp);
int     lockcount(struct lock *lkp);

#endif
