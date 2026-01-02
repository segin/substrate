#ifndef _SYS_LOCK_H
#define _SYS_LOCK_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    uint32_t locked;
    uint32_t cpu_id;
    const char *name;
} spinlock_t;

void spinlock_init(spinlock_t *lock, const char *name);
void spinlock_acquire(spinlock_t *lock);
void spinlock_release(spinlock_t *lock);
bool spinlock_is_held(spinlock_t *lock);

// Sleep Mutex
typedef struct {
    uint32_t locked;
    void     *owner; // thread_t*
    const char *name;
} mutex_t;

void mutex_init(mutex_t *m, const char *name);
void mutex_lock(mutex_t *m);
void mutex_unlock(mutex_t *m);
bool mutex_is_held(mutex_t *m);

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

#endif
