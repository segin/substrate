#pragma once
typedef int spinlock_t;
void spinlock_init(spinlock_t *l, const char *name);
void spinlock_acquire(spinlock_t *l);
void spinlock_release(spinlock_t *l);
