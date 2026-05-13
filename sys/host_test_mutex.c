#include <assert.h>
#include <setjmp.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <sys/lock.h>
#include <sys/proc.h>
#include <kern/sched.h>

/* Host test only — kernel proper no longer has a static thread array. */
#define HOST_TEST_HOST_TEST_MAX_THREADS 64
static thread_t threads[HOST_TEST_HOST_TEST_MAX_THREADS];
thread_t *current_thread;

static int wake_one_calls;
static jmp_buf panic_jmp;

void spinlock_init(spinlock_t *lock, const char *name) {
    lock->locked = 0;
    lock->cpu_id = 0xFFFFFFFFu;
    lock->name = name;
}
void spinlock_acquire(spinlock_t *lock) { lock->locked = 1; }
void spinlock_release(spinlock_t *lock) { lock->locked = 0; }
bool spinlock_is_held(spinlock_t *lock) { return lock->locked != 0; }

void sched_yield(void) {}
void sleepq_add(void *chan, thread_t *t) { (void)chan; t->state = THREAD_BLOCKED; }
thread_t *sleepq_wake_one(void *chan) { (void)chan; wake_one_calls++; return NULL; }

void panic(const char *msg) {
    (void)msg;
    longjmp(panic_jmp, 1);
}

#include "../sys/kern/mutex.c"

static void reset_env(void) {
    memset(threads, 0, sizeof(threads));
    for (int i = 0; i < HOST_TEST_MAX_THREADS; i++) {
        threads[i].tid = -1;
    }
    wake_one_calls = 0;
}

static thread_t *init_thread(int slot, int tid) {
    thread_t *t = &threads[slot];
    memset(t, 0, sizeof(*t));
    t->tid = tid;
    t->state = THREAD_RUNNING;
    return t;
}

static void test_mutex_release_owned_by_thread(void) {
    mutex_t m1, m2, m3;
    thread_t *t1;

    reset_env();
    t1 = init_thread(0, 1);
    current_thread = t1;

    mutex_init(&m1, "m1");
    mutex_init(&m2, "m2");
    mutex_init(&m3, "m3");

    mutex_lock(&m1);
    mutex_lock(&m2);
    mutex_lock(&m3);

    assert(t1->held_mutexes != NULL);
    assert(m1.owner == t1);
    assert(m2.owner == t1);
    assert(m3.owner == t1);

    int released = mutex_release_owned_by_thread(t1);

    assert(released == 3);
    assert(t1->held_mutexes == NULL);
    assert(m1.owner == NULL);
    assert(m2.owner == NULL);
    assert(m3.owner == NULL);
    assert(!m1.locked);
    assert(!m2.locked);
    assert(!m3.locked);
}

static void test_mutex_release_owned_by_thread_multiple_threads(void) {
    mutex_t m1, m2, m3;
    thread_t *t1;
    thread_t *t2;

    reset_env();
    t1 = init_thread(0, 1);
    t2 = init_thread(1, 2);

    mutex_init(&m1, "m1");
    mutex_init(&m2, "m2");
    mutex_init(&m3, "m3");

    current_thread = t1;
    mutex_lock(&m1);
    mutex_lock(&m3);

    current_thread = t2;
    mutex_lock(&m2);

    assert(t1->held_mutexes != NULL);
    assert(t2->held_mutexes != NULL);

    int released = mutex_release_owned_by_thread(t1);

    assert(released == 2);
    assert(t1->held_mutexes == NULL);
    assert(m1.owner == NULL);
    assert(m2.owner == t2);
    assert(m3.owner == NULL);
    assert(!m1.locked);
    assert(m2.locked);
    assert(!m3.locked);

    released = mutex_release_owned_by_thread(t2);
    assert(released == 1);
    assert(t2->held_mutexes == NULL);
    assert(m2.owner == NULL);
    assert(!m2.locked);
}

static void test_mutex_release_owned_by_thread_null(void) {
    reset_env();
    int released = mutex_release_owned_by_thread(NULL);
    assert(released == 0);
}

static void test_mutex_release_owned_by_thread_empty(void) {
    thread_t *t1;

    reset_env();
    t1 = init_thread(0, 1);

    int released = mutex_release_owned_by_thread(t1);
    assert(released == 0);
}

int main(void) {
    test_mutex_release_owned_by_thread();
    test_mutex_release_owned_by_thread_multiple_threads();
    test_mutex_release_owned_by_thread_null();
    test_mutex_release_owned_by_thread_empty();
    puts("host_test_mutex: PASS");
    return 0;
}
