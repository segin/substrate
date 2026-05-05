#include <assert.h>
#include <setjmp.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <sys/lock.h>
#include <sys/proc.h>
#include <kern/sched.h>

thread_t threads[MAX_THREADS];
thread_t *current_thread;
process_t *current_process;

static jmp_buf panic_jmp;
static const char *last_panic;
static int wake_one_calls;
static int wake_all_calls;
static void (*yield_callback)(void);

void spinlock_init(spinlock_t *lock, const char *name) {
    lock->locked = 0;
    lock->cpu_id = 0xFFFFFFFFu;
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

void sched_yield(void) {
    if (yield_callback) {
        yield_callback();
    }
}
void sleepq_add(void *chan, thread_t *t) { (void)chan; t->state = THREAD_BLOCKED; }
thread_t *sleepq_wake_one(void *chan) { (void)chan; wake_one_calls++; return NULL; }
int sleepq_wake_all(void *chan) { (void)chan; wake_all_calls++; return 0; }

void panic(const char *msg) {
    last_panic = msg;
    longjmp(panic_jmp, 1);
}

#include "../../sys/kern/rwlock.c"

static void reset_env(void) {
    memset(threads, 0, sizeof(threads));
    for (int i = 0; i < MAX_THREADS; i++) {
        threads[i].tid = -1;
    }
    last_panic = NULL;
    wake_one_calls = 0;
    wake_all_calls = 0;
    yield_callback = NULL;
}

static thread_t *init_thread(int slot, int tid) {
    thread_t *t = &threads[slot];
    memset(t, 0, sizeof(*t));
    t->tid = tid;
    t->state = THREAD_RUNNING;
    return t;
}

static void test_rwlock_init(void) {
    rwlock_t rw;

    reset_env();

    /* Initialize with garbage to ensure rwlock_init overwrites everything */
    memset(&rw, 0xAA, sizeof(rw));

    rwlock_init(&rw, "my_test_lock");

    assert(rw.lock.locked == 0);
    assert(rw.lock.cpu_id == 0xFFFFFFFFu);
    assert(strcmp(rw.lock.name, "rwlock") == 0);

    assert(rw.readers == 0);
    assert(rw.writer == 0);
    assert(rw.waiting_writers == 0);
    assert(rw.owner == NULL);
    assert(strcmp(rw.name, "my_test_lock") == 0);
}

static void test_rwlock_reader_and_writer_paths(void) {
    rwlock_t rw;
    thread_t *writer;

    reset_env();
    writer = init_thread(0, 1);
    current_thread = writer;

    rwlock_init(&rw, "rw");
    rw_rlock(&rw);
    assert(rw.readers == 1);
    assert(!rw_wowned(&rw));
    rw_runlock(&rw);
    assert(rw.readers == 0);

    assert(rw_try_wlock(&rw));
    assert(rw.writer == 1);
    assert(rw_wowned(&rw));
    rw_wunlock(&rw);
    assert(rw.writer == 0);
    assert(wake_all_calls == 1);
}

static void test_rwlock_writer_preference_and_reader_wakeup(void) {
    rwlock_t rw;
    thread_t *reader;
    thread_t *writer;
    thread_t *blocked_reader;

    reset_env();
    reader = init_thread(0, 1);
    writer = init_thread(1, 2);
    blocked_reader = init_thread(2, 3);

    rwlock_init(&rw, "pref");

    current_thread = reader;
    rw_rlock(&rw);
    assert(rw.readers == 1);

    rw.waiting_writers = 1;
    current_thread = blocked_reader;
    assert(!rw_try_rlock(&rw));

    current_thread = reader;
    rw_runlock(&rw);
    assert(wake_one_calls == 1);

    rw.waiting_writers = 0;
    current_thread = writer;
    rw_wlock(&rw);
    assert(rw.writer == 1);
    rw_wunlock(&rw);
    assert(wake_all_calls >= 1);
}

static void test_rwlock_try_wlock(void) {
    rwlock_t rw;
    thread_t *t1;
    thread_t *t2;

    reset_env();
    t1 = init_thread(0, 1);
    t2 = init_thread(1, 2);

    rwlock_init(&rw, "try_wlock");

    // Success case
    current_thread = t1;
    assert(rw_try_wlock(&rw) == true);
    assert(rw.writer == 1);
    assert(rw.owner == t1);

    // Failure case 1: Writer holds the lock
    current_thread = t2;
    assert(rw_try_wlock(&rw) == false);

    // Release writer lock
    current_thread = t1;
    rw_wunlock(&rw);

    // Failure case 2: Reader holds the lock
    current_thread = t2;
    rw_rlock(&rw);

    current_thread = t1;
    assert(rw_try_wlock(&rw) == false);
    assert(rw.writer == 0);

    // Clean up
    current_thread = t2;
    rw_runlock(&rw);
}

static void test_rwlock_non_owner_panics(void) {
    rwlock_t rw;
    thread_t *owner;
    thread_t *other;

    reset_env();
    owner = init_thread(0, 1);
    other = init_thread(1, 2);

    rwlock_init(&rw, "panic");
    current_thread = owner;
    rw_wlock(&rw);

    current_thread = other;
    if (setjmp(panic_jmp) == 0) {
        rw_wunlock(&rw);
        assert(!"expected panic");
    }

    assert(last_panic != NULL);
    assert(strstr(last_panic, "non-owner") != NULL);
}

static void test_rwlock_recursive_wlock_panics(void) {
    rwlock_t rw;
    thread_t *me;

    reset_env();
    me = init_thread(0, 1);
    current_thread = me;

    rwlock_init(&rw, "recursive");
    rw_wlock(&rw);
    assert(rw.writer == 1);
    assert(rw.owner == me);

    if (setjmp(panic_jmp) == 0) {
        rw_wlock(&rw);
        assert(!"expected panic");
    }

    assert(last_panic != NULL);
    assert(strstr(last_panic, "recursive") != NULL);
}

static rwlock_t *yield_rw;
static void yield_clear_writer(void) {
    yield_rw->writer = 0;
    yield_rw->owner = NULL;
}

static void test_rwlock_wlock_blocks_on_writer(void) {
    rwlock_t rw;
    thread_t *me;
    thread_t *other;

    reset_env();
    me = init_thread(0, 1);
    other = init_thread(1, 2);
    current_thread = me;

    rwlock_init(&rw, "block_writer");

    // Simulate another thread holding the lock
    rw.writer = 1;
    rw.owner = other;

    yield_rw = &rw;
    yield_callback = yield_clear_writer;

    rw_wlock(&rw);

    assert(rw.writer == 1);
    assert(rw.owner == me);
    assert(rw.waiting_writers == 0);
}

static void yield_clear_readers(void) {
    yield_rw->readers = 0;
}

static void test_rwlock_wlock_blocks_on_reader(void) {
    rwlock_t rw;
    thread_t *me;

    reset_env();
    me = init_thread(0, 1);
    current_thread = me;

    rwlock_init(&rw, "block_reader");

    // Simulate readers holding the lock
    rw.readers = 2;

    yield_rw = &rw;
    yield_callback = yield_clear_readers;

    rw_wlock(&rw);

    assert(rw.writer == 1);
    assert(rw.owner == me);
    assert(rw.readers == 0);
    assert(rw.waiting_writers == 0);
}

int main(void) {
    test_rwlock_init();
    test_rwlock_reader_and_writer_paths();
    test_rwlock_writer_preference_and_reader_wakeup();
    test_rwlock_try_wlock();
    test_rwlock_non_owner_panics();
    test_rwlock_recursive_wlock_panics();
    test_rwlock_wlock_blocks_on_writer();
    test_rwlock_wlock_blocks_on_reader();
    puts("host_test_rwlock: PASS");
    return 0;
}
