# Kernel Locking Specification (Spinlocks)

## Overview
Spinlocks are the primary synchronization primitive for short-duration mutual exclusion in the Substrate kernel, especially in SMP environments. They use atomic hardware instructions to ensure only one CPU core can hold the lock at a time.

## Design
- **Structure (`spinlock_t`):**
    - `uint32_t locked`: 0 if free, 1 if held.
    - `uint32_t cpu_id`: ID of the core currently holding the lock (for deadlock detection).
    - `const char *name`: Identifier for debugging.
- **Atomic Operations:** Uses the `xchg` instruction with the `lock` prefix (implicit in `xchg` on x86) to atomically swap the lock state.
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
- **Blocking:** Uses `sched_sleep()` when the lock is contested and `sched_wakeup()` upon release.

## API
### `void mutex_init(mutex_t *m, const char *name)`
Initializes a new mutex.

### `void mutex_lock(mutex_t *m)`
Acquires the mutex, blocking the thread if it's already held.

### `void mutex_unlock(mutex_t *m)`
Releases the mutex and wakes up all waiting threads.

### `bool mutex_is_held(mutex_t *m)`
Returns true if the current thread holds the mutex.

## Semaphores
Semaphores are synchronization primitives that maintain a counter. They are used to control access to a shared resource by multiple threads.

## Design
- **Structure (`semaphore_t`):**
    - `int value`: Current value of the semaphore.
    - `spinlock_t lock`: Protects the `value` field.
    - `const char *name`: Identifier for debugging.
- **Wait/Post:** Uses `sched_sleep()` when `value <= 0` and `sched_wakeup()` when incrementing `value`.

## API
### `void sema_init(semaphore_t *s, int value, const char *name)`
Initializes a new semaphore with the given initial value.

### `void sema_wait(semaphore_t *s)`
Decrements the semaphore value. If the value is 0 or less, the thread blocks.

### `void sema_post(semaphore_t *s)`
Increments the semaphore value and wakes up one waiting thread.

### `int sema_getvalue(semaphore_t *s)`
Returns the current value of the semaphore.

## Constraints
- Safe for use in kernel mode.
- Not safe for interrupt context if it leads to blocking.
