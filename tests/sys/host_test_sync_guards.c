#include <assert.h>
#include <setjmp.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <sys/lock.h>
#include <kern/sched.h>

/* The kernel's global process/thread tables are gone, and MAX_PROCS /
 * MAX_THREADS went with them; anything sized by them here is this file's
 * own storage.  Values match the other host tests. */
#ifndef MAX_THREADS
#define MAX_THREADS 64
#endif

thread_t threads[MAX_THREADS];
thread_t *current_thread;
process_t *current_process;

static jmp_buf panic_jmp;
static const char *last_panic;
static int wake_one_calls;

void spinlock_init(spinlock_t *lock, const char *name) {
    lock->locked = 0;
    lock->cpu_id = 0xFFFFFFFF;
    lock->name = name;
}
void spinlock_acquire(spinlock_t *lock) { lock->locked = 1; }
bool spinlock_try_acquire(spinlock_t *lock) {
    if (lock->locked) return false;
    lock->locked = 1;
    return true;
}
void spinlock_release(spinlock_t *lock) { lock->locked = 0; }
bool spinlock_is_held(spinlock_t *lock) { return lock->locked != 0; }

void sched_yield(void) {}
void sleepq_add(void *chan, thread_t *t) { (void)chan; t->state = THREAD_BLOCKED; }
thread_t *sleepq_wake_one(void *chan) { (void)chan; wake_one_calls++; return NULL; }
int sleepq_wake_all(void *chan) { (void)chan; return 0; }

void panic(const char *msg) {
    last_panic = msg;
    longjmp(panic_jmp, 1);
}

#include "../../sys/kern/mutex.c"
#include "../../sys/kern/semaphore.c"

static void reset_env(void) {
    memset(threads, 0, sizeof(threads));
    for (int i = 0; i < MAX_THREADS; i++) {
        threads[i].tid = -1;
    }
    last_panic = NULL;
    wake_one_calls = 0;
}

static thread_t *init_thread(int slot, int tid) {
    thread_t *t = &threads[slot];
    memset(t, 0, sizeof(*t));
    t->tid = tid;
    t->state = THREAD_RUNNING;
    return t;
}

static void test_mutex_unlock_by_non_owner_panics(void) {
    mutex_t m;

    reset_env();
    thread_t *owner = init_thread(0, 1);
    thread_t *other = init_thread(1, 2);

    current_thread = owner;
    mutex_init(&m, "guard");
    mutex_lock(&m);

    current_thread = other;
    if (setjmp(panic_jmp) == 0) {
        mutex_unlock(&m);
        assert(!"expected panic");
    }

    assert(last_panic != NULL);
    assert(strstr(last_panic, "non-owner") != NULL);
}

static void test_mutex_recursive_lock_panics(void) {
    mutex_t m;

    reset_env();
    current_thread = init_thread(0, 1);

    mutex_init(&m, "recursive");
    mutex_lock(&m);

    if (setjmp(panic_jmp) == 0) {
        mutex_lock(&m);
        assert(!"expected panic");
    }

    assert(last_panic != NULL);
    assert(strstr(last_panic, "recursive mutex_lock") != NULL);
}

static void test_semaphore_negative_init_panics(void) {
    semaphore_t s;

    reset_env();
    if (setjmp(panic_jmp) == 0) {
        sema_init(&s, -1, "bad");
        assert(!"expected panic");
    }

    assert(last_panic != NULL);
    assert(strstr(last_panic, "negative value") != NULL);
}

static void test_semaphore_post_wakes_waiter_path(void) {
    semaphore_t s;

    reset_env();
    sema_init(&s, 0, "ok");
    sema_post(&s);
    assert(sema_getvalue(&s) == 1);
    assert(wake_one_calls == 1);
}

static void test_mutex_untrack_owner_scenarios(void) {
    mutex_t m1, m2, m3, m_not_in_list;
    thread_t *owner;

    reset_env();
    owner = init_thread(0, 1);

    // Test null parameters (should not crash)
    mutex_untrack_owner(NULL, owner);
    mutex_untrack_owner(&m1, NULL);
    mutex_untrack_owner(NULL, NULL);

    // Setup: link them manually
    // m1 -> m2 -> m3 -> NULL
    owner->held_mutexes = &m1;
    m1.owned_next = &m2;
    m2.owned_next = &m3;
    m3.owned_next = NULL;

    // 1. Remove from middle (m2)
    mutex_untrack_owner(&m2, owner);
    assert(owner->held_mutexes == &m1);
    assert(m1.owned_next == &m3);
    assert(m2.owned_next == NULL);

    // 2. Remove from end (m3)
    mutex_untrack_owner(&m3, owner);
    assert(owner->held_mutexes == &m1);
    assert(m1.owned_next == NULL);
    assert(m3.owned_next == NULL);

    // Reset list to m1 -> m2 -> NULL
    owner->held_mutexes = &m1;
    m1.owned_next = &m2;
    m2.owned_next = NULL;

    // 3. Remove from beginning (m1)
    mutex_untrack_owner(&m1, owner);
    assert(owner->held_mutexes == &m2);
    assert(m1.owned_next == NULL);

    // 4. Try removing mutex not in list
    mutex_untrack_owner(&m_not_in_list, owner);
    assert(owner->held_mutexes == &m2); // Should remain unchanged
    assert(m2.owned_next == NULL);

    // 5. Remove the last remaining mutex
    mutex_untrack_owner(&m2, owner);
    assert(owner->held_mutexes == NULL);
    assert(m2.owned_next == NULL);
}

static void test_mutex_force_release_wakes_waiter(void) {
    mutex_t m;
    thread_t *owner;
    thread_t *waiter;

    reset_env();
    owner = init_thread(0, 1);
    waiter = init_thread(1, 2);

    current_thread = owner;
    mutex_init(&m, "exit-release");
    mutex_lock(&m);
    assert(owner->held_mutexes == &m);

    sleepq_add(&m, waiter);
    wake_one_calls = 0;

    assert(mutex_release_owned_by_thread(owner) == 1);
    assert(m.locked == 0);
    assert(m.owner == NULL);
    assert(owner->held_mutexes == NULL);
    assert(wake_one_calls == 1);
}

static void test_mutex_track_owner_basic(void) {
    mutex_t m1, m2;
    thread_t *owner;

    reset_env();
    owner = init_thread(0, 1);

    mutex_init(&m1, "m1");
    mutex_init(&m2, "m2");

    // Test null checks
    mutex_track_owner(NULL, owner);
    mutex_track_owner(&m1, NULL);
    assert(owner->held_mutexes == NULL);

    // Test single track
    mutex_track_owner(&m1, owner);
    assert(owner->held_mutexes == &m1);
    assert(m1.owned_next == NULL);

    // Test multiple track
    mutex_track_owner(&m2, owner);
    assert(owner->held_mutexes == &m2);
    assert(m2.owned_next == &m1);
    assert(m1.owned_next == NULL);

    // Test untrack (clean up)
    mutex_untrack_owner(&m1, owner);
    assert(owner->held_mutexes == &m2);
    assert(m2.owned_next == NULL);

    mutex_untrack_owner(&m2, owner);
    assert(owner->held_mutexes == NULL);
}

int main(void) {
    test_mutex_unlock_by_non_owner_panics();
    test_mutex_recursive_lock_panics();
    test_semaphore_negative_init_panics();
    test_semaphore_post_wakes_waiter_path();
    test_mutex_force_release_wakes_waiter();
    test_mutex_untrack_owner_scenarios();
    puts("host_test_sync_guards: PASS");
    return 0;
}
