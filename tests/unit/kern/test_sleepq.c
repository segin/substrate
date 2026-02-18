#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/proc.h>
#include <kern/sleepq.h>
#include <kern/sched.h>

// Helper to create a dummy thread
static thread_t *create_dummy_thread(int id) {
    static char stacks[10][4096];
    if (id < 0 || id >= 10) return NULL;
    // Using current_process as a base, mocked or real
    return sched_create_thread(current_process, (void*)(uintptr_t)id, stacks[id] + 4096, NULL);
}

bool test_sleepq_basic(void) {
    sleepq_init();
    sched_init();
    int chan = 0;

    thread_t *t1 = create_dummy_thread(1);
    current_thread = t1;
    t1->state = THREAD_RUNNING;

    // Add to sleepq
    sleepq_add(&chan, t1);

    if (t1->state != THREAD_BLOCKED) {
        printf("FAIL: thread not blocked\n");
        return false;
    }
    if (t1->wait_chan != &chan) {
        printf("FAIL: wait_chan mismatch\n");
        return false;
    }
    if (!sleepq_has_waiters(&chan)) {
        printf("FAIL: has_waiters false\n");
        return false;
    }

    // Wake up
    thread_t *woken = sleepq_wake_one(&chan);
    if (woken != t1) {
        printf("FAIL: woken != t1\n");
        return false;
    }
    if (t1->state != THREAD_READY) {
        printf("FAIL: state != READY\n");
        return false;
    }
    if (sleepq_has_waiters(&chan)) {
        printf("FAIL: has_waiters true after wake\n");
        return false;
    }

    return true;
}

bool test_sleepq_fifo(void) {
    sleepq_init();
    sched_init();
    int chan = 0;

    thread_t *t1 = create_dummy_thread(1);
    thread_t *t2 = create_dummy_thread(2);

    current_thread = t1; t1->state = THREAD_RUNNING; sleepq_add(&chan, t1);
    current_thread = t2; t2->state = THREAD_RUNNING; sleepq_add(&chan, t2);

    // Wake one - should be t1 (FIFO)
    thread_t *woken = sleepq_wake_one(&chan);
    if (woken != t1) {
        printf("FAIL: FIFO violation (got %p, expected %p)\n", woken, t1);
        return false;
    }

    // Wake another - should be t2
    woken = sleepq_wake_one(&chan);
    if (woken != t2) {
        printf("FAIL: FIFO violation 2 (got %p, expected %p)\n", woken, t2);
        return false;
    }

    return true;
}

bool test_sleepq_wake_all(void) {
    sleepq_init();
    sched_init();
    int chan = 0;

    thread_t *t1 = create_dummy_thread(1);
    thread_t *t2 = create_dummy_thread(2);
    thread_t *t3 = create_dummy_thread(3);

    current_thread = t1; t1->state = THREAD_RUNNING; sleepq_add(&chan, t1);
    current_thread = t2; t2->state = THREAD_RUNNING; sleepq_add(&chan, t2);
    current_thread = t3; t3->state = THREAD_RUNNING; sleepq_add(&chan, t3);

    int count = sleepq_wake_all(&chan);
    if (count != 3) {
        printf("FAIL: wake_all count %d != 3\n", count);
        return false;
    }

    if (t1->state != THREAD_READY || t2->state != THREAD_READY || t3->state != THREAD_READY) {
        printf("FAIL: not all threads ready\n");
        return false;
    }

    if (sleepq_has_waiters(&chan)) {
        printf("FAIL: has_waiters true after wake_all\n");
        return false;
    }

    return true;
}

bool test_sleepq_wake_n(void) {
    sleepq_init();
    sched_init();
    int chan = 0;

    thread_t *t1 = create_dummy_thread(1);
    thread_t *t2 = create_dummy_thread(2);
    thread_t *t3 = create_dummy_thread(3);

    current_thread = t1; t1->state = THREAD_RUNNING; sleepq_add(&chan, t1);
    current_thread = t2; t2->state = THREAD_RUNNING; sleepq_add(&chan, t2);
    current_thread = t3; t3->state = THREAD_RUNNING; sleepq_add(&chan, t3);

    // Wake 2
    int count = sleepq_wake_n(&chan, 2);
    if (count != 2) {
        printf("FAIL: wake_n count %d != 2\n", count);
        return false;
    }

    if (t1->state != THREAD_READY) {
        printf("FAIL: t1 not ready\n");
        return false;
    }
    if (t2->state != THREAD_READY) {
        printf("FAIL: t2 not ready\n");
        return false;
    }
    if (t3->state != THREAD_BLOCKED) {
        printf("FAIL: t3 not blocked\n");
        return false;
    }

    return true;
}

bool test_sleepq_private(void) {
    sleepq_init();
    sched_init();
    int chan = 0;

    // Mock processes
    process_t p1, p2;
    memset(&p1, 0, sizeof(p1)); p1.pid = 100;
    memset(&p2, 0, sizeof(p2)); p2.pid = 200;

    thread_t *t1 = create_dummy_thread(1);
    thread_t *t2 = create_dummy_thread(2);

    // t1 in p1
    current_process = &p1;
    current_thread = t1;
    t1->state = THREAD_RUNNING;
    sleepq_add_private(&chan, t1);

    // t2 in p2
    current_process = &p2;
    current_thread = t2;
    t2->state = THREAD_RUNNING;
    sleepq_add_private(&chan, t2);

    // Switch to p1 and wake private
    current_process = &p1;
    if (!sleepq_has_waiters_private(&chan)) {
        printf("FAIL: p1 has no waiters\n");
        return false;
    }

    // Should wake t1 only
    thread_t *woken = sleepq_wake_one_private(&chan);
    if (woken != t1) {
        printf("FAIL: woken != t1 (got %p)\n", woken);
        return false;
    }

    // t2 should still be waiting
    if (t2->state != THREAD_BLOCKED) {
        printf("FAIL: t2 not blocked\n");
        return false;
    }

    // Switch to p2 and check
    current_process = &p2;
    if (!sleepq_has_waiters_private(&chan)) {
        printf("FAIL: p2 has no waiters\n");
        return false;
    }
    woken = sleepq_wake_one_private(&chan);
    if (woken != t2) {
        printf("FAIL: woken != t2 (got %p)\n", woken);
        return false;
    }

    return true;
}

bool test_sleepq_collisions(void) {
    sleepq_init();
    sched_init();
    // Use addresses that collide
    // Hash is (addr >> 3) & 0xFF
    // chan1: 0x1000 -> 0x200 -> 0x00
    // chan2: 0x1800 -> 0x300 -> 0x00
    void *chan1 = (void*)0x1000;
    void *chan2 = (void*)0x1800;

    thread_t *t1 = create_dummy_thread(1);
    thread_t *t2 = create_dummy_thread(2);

    current_thread = t1; t1->state = THREAD_RUNNING; sleepq_add(chan1, t1);
    current_thread = t2; t2->state = THREAD_RUNNING; sleepq_add(chan2, t2);

    // Both should be in the hash table, likely same bucket
    if (!sleepq_has_waiters(chan1)) {
        printf("FAIL: chan1 has no waiters\n");
        return false;
    }
    if (!sleepq_has_waiters(chan2)) {
        printf("FAIL: chan2 has no waiters\n");
        return false;
    }

    // Wake chan1
    thread_t *woken = sleepq_wake_one(chan1);
    if (woken != t1) {
        printf("FAIL: woken != t1\n");
        return false;
    }

    // Chan2 still has waiters
    if (!sleepq_has_waiters(chan2)) {
        printf("FAIL: chan2 lost waiters\n");
        return false;
    }

    woken = sleepq_wake_one(chan2);
    if (woken != t2) {
        printf("FAIL: woken != t2\n");
        return false;
    }

    return true;
}

bool test_sleepq_requeue(void) {
    sleepq_init();
    sched_init();
    int src_chan = 0;
    int dst_chan = 0;

    thread_t *t1 = create_dummy_thread(1);
    thread_t *t2 = create_dummy_thread(2);

    current_thread = t1; t1->state = THREAD_RUNNING; sleepq_add(&src_chan, t1);
    current_thread = t2; t2->state = THREAD_RUNNING; sleepq_add(&src_chan, t2);

    // Move 1 thread to dst, wake 0
    sleepq_requeue(&src_chan, &dst_chan, 0, 1);

    // t1 should now be on dst_chan (FIFO)
    if (t1->wait_chan != &dst_chan) {
        printf("FAIL: t1 not on dst_chan\n");
        return false;
    }
    // t2 still on src_chan
    if (t2->wait_chan != &src_chan) {
        printf("FAIL: t2 not on src_chan\n");
        return false;
    }

    // Wake dst
    thread_t *woken = sleepq_wake_one(&dst_chan);
    if (woken != t1) {
        printf("FAIL: woken != t1 from dst\n");
        return false;
    }

    return true;
}
