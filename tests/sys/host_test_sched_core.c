#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/proc.h>
#include <sys/lock.h>
#include <kern/runqueue.h>
#include <kern/sched.h>
#include <kern/turnstile.h>
#include <kern/sleepq.h>

process_t *current_process;
thread_t *current_thread;

void spinlock_init(spinlock_t *lock, const char *name) {
    lock->locked = 0;
    lock->cpu_id = 0xFFFFFFFFu;
    lock->name = name;
}

void spinlock_acquire(spinlock_t *lock) {
    lock->locked = 1;
    lock->cpu_id = 0;
}

void spinlock_release(spinlock_t *lock) {
    lock->locked = 0;
    lock->cpu_id = 0xFFFFFFFFu;
}

void panic(const char *fmt, ...) {
    (void)fmt;
    abort();
}

#include "../../sys/kern/runqueue.c"
#include "../../sys/kern/turnstile.c"
#include "../../sys/kern/sleepq.c"
uint16_t sched_calc_timeslice(thread_t *t);
#include "../../sys/kern/sched_interactivity.c"

static thread_t *make_thread(thread_t *t, int tid, sched_class_t sched_class,
                             int priority, int base_priority) {
    memset(t, 0, sizeof(*t));
    t->tid = tid;
    t->sched_class = sched_class;
    t->priority = priority;
    t->base_priority = base_priority;
    t->state = THREAD_READY;
    return t;
}

static void test_runqueue_priority_ordering(void) {
    runqueue_t rq;
    thread_t idle;
    thread_t ts_high;
    thread_t ts_low;
    thread_t rt;

    runqueue_init(&rq, 0);

    runqueue_add(&rq, make_thread(&ts_low, 1, SCHED_TIMESHARE, 20, 20));
    runqueue_add(&rq, make_thread(&idle, 2, SCHED_IDLE, 0, 0));
    runqueue_add(&rq, make_thread(&rt, 3, SCHED_REALTIME, 31, 31));
    runqueue_add(&rq, make_thread(&ts_high, 4, SCHED_TIMESHARE, 0, 0));

    assert(runqueue_count(&rq) == 4);
    assert(runqueue_peek(&rq) == &rt);
    assert(runqueue_pop(&rq) == &rt);
    assert(runqueue_pop(&rq) == &ts_high);
    assert(runqueue_pop(&rq) == &ts_low);
    assert(runqueue_pop(&rq) == &idle);
    assert(runqueue_empty(&rq));
}

static void test_turnstile_priority_inheritance_restore(void) {
    int lockobj = 0;
    thread_t owner;
    thread_t waiter_lo;
    thread_t waiter_hi;

    turnstile_init();

    make_thread(&owner, 10, SCHED_TIMESHARE, 20, 20);
    make_thread(&waiter_lo, 11, SCHED_TIMESHARE, 12, 12);
    make_thread(&waiter_hi, 12, SCHED_TIMESHARE, 5, 5);

    current_thread = &waiter_lo;
    turnstile_block(&lockobj, &owner);
    assert(owner.priority == 12);
    assert(turnstile_get_inherited_priority(&owner) == 12);

    current_thread = &waiter_hi;
    turnstile_block(&lockobj, &owner);
    assert(owner.priority == 5);
    assert(turnstile_get_inherited_priority(&owner) == 5);

    waiter_lo.state = THREAD_BLOCKED;
    waiter_hi.state = THREAD_BLOCKED;
    turnstile_release(&lockobj);

    assert(owner.priority == owner.base_priority);
    assert(waiter_lo.state == THREAD_READY);
    assert(waiter_hi.state == THREAD_READY);
    assert(turnstile_get_inherited_priority(&owner) == 0);
}

static void test_sleepq_hash_distribution(void) {
    bool shared_buckets[256];
    bool private_buckets[256];
    int shared_unique = 0;
    int private_unique = 0;

    memset(shared_buckets, 0, sizeof(shared_buckets));
    memset(private_buckets, 0, sizeof(private_buckets));

    for (int i = 0; i < 128; i++) {
        void *chan = (void *)(uintptr_t)((i + 1) * 8);
        int shared = sleepq_hash_func(chan, 0, 0);
        int priv = sleepq_hash_func(chan, 1, (i + 1) << 8);

        shared_buckets[shared] = true;
        private_buckets[priv] = true;
    }

    for (int i = 0; i < 256; i++) {
        shared_unique += shared_buckets[i] ? 1 : 0;
        private_unique += private_buckets[i] ? 1 : 0;
    }

    assert(shared_unique >= 120);
    assert(private_unique >= 64);
    assert(sleepq_hash_func((void *)0x4000, 1, 100) !=
           sleepq_hash_func((void *)0x4000, 1, 101));
}

static void test_sched_interactivity_io_boost(void) {
    thread_t io_bound;
    thread_t cpu_bound;

    make_thread(&io_bound, 20, SCHED_TIMESHARE, 10, 10);
    io_bound.sleep_time = 200;
    io_bound.run_time = 10;
    sched_interactivity_on_run(&io_bound);
    assert(sched_is_interactive(&io_bound));

    sched_interactivity_on_wakeup(&io_bound, 50);
    assert(io_bound.time_slice == SLICE_INTERACTIVE);
    assert(sched_interactivity_boost(&io_bound) < 0);

    make_thread(&cpu_bound, 21, SCHED_TIMESHARE, 10, 10);
    cpu_bound.interactivity = -100;
    cpu_bound.time_slice = sched_calc_timeslice(&cpu_bound);
    assert(cpu_bound.time_slice == SLICE_BATCH);

    for (int i = 0; i < 20; i++) {
        sched_interactivity_tick(&io_bound);
    }
    assert(io_bound.interactivity < INTERACT_MAX - 1);
}

int main(void) {
    process_t proc;

    memset(&proc, 0, sizeof(proc));
    proc.pid = 1;
    current_process = &proc;
    current_thread = NULL;

    test_runqueue_priority_ordering();
    test_turnstile_priority_inheritance_restore();
    test_sleepq_hash_distribution();
    test_sched_interactivity_io_boost();

    puts("host_test_sched_core: PASS");
    return 0;
}
