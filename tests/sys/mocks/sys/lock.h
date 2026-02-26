#ifndef _SYS_LOCK_H
#define _SYS_LOCK_H
#include <stdint.h>
typedef struct {
    int locked;
    uint32_t cpu_id;
    const char *name;
} spinlock_t;
void spinlock_init(spinlock_t *lock, const char *name);
void spinlock_acquire(spinlock_t *lock);
int spinlock_try_acquire(spinlock_t *lock);
void spinlock_release(spinlock_t *lock);
int spinlock_is_held(spinlock_t *lock);
#endif
