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

#endif
