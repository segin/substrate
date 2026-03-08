# Kernel Locking Specification

## Overview
Substrate provides a small kernel locking set built from spinlocks, sleepqueue-backed mutexes, counting semaphores, and sleepqueue-backed reader/writer locks.

## Design
- **Structure (`spinlock_t`):**
    - `uint32_t locked`: 0 if free, 1 if held.
    - `uint32_t cpu_id`: ID of the core currently holding the lock (for deadlock detection).
    - `const char *name`: Identifier for debugging.
- **Atomic Operations:** Uses GCC atomic builtins on top of x86 atomic instructions.
- **Deadlock Detection:**
    - Panic if a CPU attempts to acquire a lock it already holds (recursive acquisition).
    - (Future) Support for lock ordering and priority inheritance.

## API
### `void spinlock_init(spinlock_t *lock, const char *name)`
Initializes a new spinlock.

### `void spinlock_acquire(spinlock_t *lock)`
Acquires the lock, spinning until it becomes available.

### `void spinlock_release(spinlock_t *lock)`
Releases the lock.

### `bool spinlock_is_held(spinlock_t *lock)`
Returns true if the lock is held by the current CPU.

## Sleep Mutexes
Mutexes are used for long-duration mutual exclusion. Unlike spinlocks, a thread that fails to acquire a mutex will block (sleep) until the mutex is released.

## Design
- **Structure (`mutex_t`):**
    - `uint32_t locked`: 0 if free, 1 if held.
    - `void *owner`: Pointer to the `thread_t` holding the lock.
    - `const char *name`: Identifier for debugging.
- **Blocking:** Uses `sleepq_add()` to park waiters and `sleepq_wake_one()` on unlock.
- **Fast path:** uncontended acquisition uses CAS without taking the guard spinlock.
- **Adaptive spin:** lock acquisition spins briefly while the owner remains runnable before falling back to sleep.

## API
### `void mutex_init(mutex_t *m, const char *name)`
Initializes a new mutex.

### `void mutex_lock(mutex_t *m)`
Acquires the mutex, blocking the thread if it's already held.

### `void mutex_unlock(mutex_t *m)`
Releases the mutex and wakes one waiting thread.

### `bool mutex_is_held(mutex_t *m)`
Returns true if the current thread holds the mutex.

## Semaphores
Semaphores are synchronization primitives that maintain a counter. They are used to control access to a shared resource by multiple threads.

## Design
- **Structure (`semaphore_t`):**
    - `int value`: Current value of the semaphore.
    - `spinlock_t lock`: Protects the `value` field.
    - `const char *name`: Identifier for debugging.
- **Wait/Post:** Uses `sleepq_add()` when `value <= 0` and `sleepq_wake_one()` after increment.

## API
### `void sema_init(semaphore_t *s, int value, const char *name)`
Initializes a new semaphore with the given initial value.

### `void sema_wait(semaphore_t *s)`
Decrements the semaphore value. If the value is 0 or less, the thread blocks.

### `void sema_post(semaphore_t *s)`
Increments the semaphore value and wakes up one waiting thread.

### `int sema_getvalue(semaphore_t *s)`
Returns the current value of the semaphore.

## Reader/Writer Locks
Reader/writer locks allow concurrent shared readers or one exclusive writer.

## Design
- **Structure (`rwlock_t`):**
    - `spinlock_t lock`: Protects the lock state.
    - `uint32_t readers`: Current shared-reader count.
    - `uint32_t writer`: Nonzero while an exclusive writer holds the lock.
    - `uint32_t waiting_writers`: Count of queued writers.
    - `void *owner`: Pointer to the writer thread while write-held.
    - `const char *name`: Identifier for debugging.
- **Policy:** writer-preferred. New readers block while an exclusive owner is active or queued writers are waiting.
- **Blocking:** readers sleep on the reader wait channel; writers sleep on the writer wait channel.
- **Wakeup:** last reader wakes one queued writer; writer unlock wakes a queued writer first, otherwise wakes all readers.

## API
### `void rwlock_init(rwlock_t *rw, const char *name)`
Initializes a new reader/writer lock.

### `void rw_rlock(rwlock_t *rw)`
Acquires the lock in shared mode, blocking behind active or queued writers.

### `void rw_runlock(rwlock_t *rw)`
Releases one shared reader reference.

### `void rw_wlock(rwlock_t *rw)`
Acquires the lock in exclusive mode.

### `void rw_wunlock(rwlock_t *rw)`
Releases the exclusive writer and wakes the next waiter set according to writer preference.

### `bool rw_try_rlock(rwlock_t *rw)`
Attempts a non-blocking shared acquisition.

### `bool rw_try_wlock(rwlock_t *rw)`
Attempts a non-blocking exclusive acquisition.

### `bool rw_wowned(rwlock_t *rw)`
Returns true if the current thread owns the lock in exclusive mode.

## Constraints
- Safe for use in kernel mode.
- Not safe for interrupt context if it leads to blocking.
- Reader/writer locks currently provide writer preference and exclusive-owner checks, but do not yet include upgrade/downgrade operations.
