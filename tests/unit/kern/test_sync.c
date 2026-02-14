#include <stdbool.h>
#include <stdio.h>
#include <stddef.h>
#include <sys/lock.h>
#include <kern/sched.h>

bool test_mutex_basic(void) {
    mutex_t m;
    mutex_init(&m, "test");
    
    mutex_lock(&m);
    if (!mutex_is_held(&m)) return false;
    
    mutex_unlock(&m);
    if (m.locked != 0) return false;
    
    return true;
}

bool test_mutex_trylock(void) {
    mutex_t m;
    mutex_init(&m, "trylock");

    if (!mutex_trylock(&m)) return false;
    if (m.locked != 1) return false;
    if (!mutex_is_held(&m)) return false;

    // Second try should fail
    if (mutex_trylock(&m)) return false;

    mutex_unlock(&m);
    if (m.locked != 0) return false;

    // Should succeed now
    if (!mutex_trylock(&m)) return false;
    mutex_unlock(&m);

    return true;
}

bool test_mutex_ownership(void) {
    sched_init();
    mutex_t m;
    mutex_init(&m, "owner");

    char s1[4096], s2[4096];
    thread_t *t1 = sched_create_thread(current_process, (void*)0x1, s1+4096, NULL);
    thread_t *t2 = sched_create_thread(current_process, (void*)0x2, s2+4096, NULL);

    current_thread = t1;
    mutex_lock(&m);
    if (!mutex_is_held(&m)) return false;

    current_thread = t2;
    if (mutex_is_held(&m)) return false; // Held by t1, not t2

    current_thread = t1;
    mutex_unlock(&m);
    if (mutex_is_held(&m)) return false;

    return true;
}

bool test_mutex_multiple_waiters(void) {
    sched_init();
    mutex_t m;
    mutex_init(&m, "multi");

    char s1[4096], s2[4096], s3[4096];
    thread_t *t1 = sched_create_thread(current_process, (void*)0x1, s1+4096, NULL);
    thread_t *t2 = sched_create_thread(current_process, (void*)0x2, s2+4096, NULL);
    thread_t *t3 = sched_create_thread(current_process, (void*)0x3, s3+4096, NULL);

    // t1 holds lock
    current_thread = t1;
    mutex_lock(&m);

    // t2 and t3 block
    extern void sleepq_add(void *chan, thread_t *t);
    current_thread = t2; sleepq_add(&m, t2);
    current_thread = t3; sleepq_add(&m, t3);

    if (t2->state != THREAD_BLOCKED || t3->state != THREAD_BLOCKED) return false;

    // t1 unlocks, should wake t2 (FIFO)
    current_thread = t1;
    mutex_unlock(&m);

    if (t2->state != THREAD_READY) return false;
    if (t3->state != THREAD_BLOCKED) return false;

    // Simulate t2 taking the lock
    current_thread = t2;
    mutex_lock(&m);

    // t2 unlocks, should wake t3
    mutex_unlock(&m);
    if (t3->state != THREAD_READY) return false;

    return true;
}

bool test_mutex_contention(void) {
    sched_init();
    mutex_t m;
    mutex_init(&m, "contend");
    
    char s1[4096];
    thread_t *t1 = sched_create_thread(current_process, (void*)0x1, s1 + 4096, NULL);
    
    // Simulate tid1 holding the lock
    current_thread = t1;
    current_thread->state = THREAD_RUNNING;
    mutex_lock(&m);
    
    char s2[4096];
    thread_t *t2 = sched_create_thread(current_process, (void*)0x2, s2 + 4096, NULL);
    
    // Simulate tid2 trying to lock and failing (blocking)
    // We call sleepq_add manually because calling mutex_lock would loop forever
    // since sched_yield() doesn't actually stop the execution of the caller in host mocks.
    current_thread = t2;
    current_thread->state = THREAD_RUNNING;
    extern void sleepq_add(void *chan, thread_t *t);
    sleepq_add(&m, t2);
    
    if (t2->state != THREAD_BLOCKED) { printf("t2 state %d != BLOCKED\n", t2->state); return false; }
    if (t2->wait_chan != &m) { printf("t2 wait_chan %p != %p\n", t2->wait_chan, &m); return false; }
    
    // Now switch back to tid1 and unlock
    current_thread = t1;
    current_thread->state = THREAD_RUNNING;
    mutex_unlock(&m);
    
    // tid2 should now be ready
    if (t2->state != THREAD_READY) { printf("t2 state %d != READY\n", t2->state); return false; }
    if (t2->wait_chan != NULL) { printf("t2 wait_chan %p != NULL\n", t2->wait_chan); return false; }
    
    return true;
}

bool test_sema_basic(void) {
    semaphore_t s;
    sema_init(&s, 1, "test-sema");
    
    sema_wait(&s);
    if (sema_getvalue(&s) != 0) return false;
    
    sema_post(&s);
    if (sema_getvalue(&s) != 1) return false;
    
    return true;
}

bool test_sema_blocking(void) {
    sched_init();
    semaphore_t s;
    sema_init(&s, 0, "block-sema"); // Start at 0
    
    char stack[4096];
    thread_t *t = sched_create_thread(current_process, (void*)0x1, stack + 4096, NULL);
    
    // Simulate t trying to wait and blocking
    current_thread = t;
    current_thread->state = THREAD_RUNNING;
    
    // Manual wait logic because sema_wait would loop on sched_sleep
    extern void sleepq_add(void *chan, thread_t *t);
    sleepq_add(&s, t);
    
    if (t->state != THREAD_BLOCKED) return false;
    
    // Now post from another thread (kernel thread)
    current_thread = sched_get_thread(0);
    current_thread->state = THREAD_RUNNING;
    sema_post(&s);
    
    if (t->state != THREAD_READY) return false;
    if (sema_getvalue(&s) != 1) return false;
    
    return true;
}

#include <sys/futex.h>

extern int sys_futex(int *uaddr, int op, int val, void *timeout, int *uaddr2, int val3);

bool test_futex_basic(void) {
    int futex_val = 1;
    
    // Should not block if val doesn't match
    if (sys_futex(&futex_val, FUTEX_WAIT, 0, NULL, NULL, 0) == 0) return false;
    
    // Success case (WAKE)
    if (sys_futex(&futex_val, FUTEX_WAKE, 1, NULL, NULL, 0) != 0) return false;
    
    return true;
}

bool test_futex_blocking(void) {
    sched_init();
    int futex_val = 10;
    
    char stack[4096];
    thread_t *t = sched_create_thread(current_process, (void*)0x1, stack + 4096, NULL);
    
    // Simulate t calling FUTEX_WAIT
    current_thread = t;
    current_thread->state = THREAD_RUNNING;
    
    // Manual simulation of sys_futex call that blocks
    int ret = sys_futex(&futex_val, FUTEX_WAIT, 10, NULL, NULL, 0);
    if (ret != 0) { printf("sys_futex WAIT failed: %d\n", ret); return false; }
    
    if (t->state != THREAD_BLOCKED) { printf("t->state %d != BLOCKED\n", t->state); return false; }
    if (t->wait_chan != &futex_val) { printf("t->wait_chan %p != %p\n", t->wait_chan, &futex_val); return false; }
    
    // Now wake up from kernel thread
    current_thread = sched_get_thread(0);
    current_thread->state = THREAD_RUNNING;
    ret = sys_futex(&futex_val, FUTEX_WAKE, 1, NULL, NULL, 0);
    if (ret < 0) { printf("sys_futex WAKE failed: %d\n", ret); return false; }
    if (ret == 0) { printf("sys_futex WAKE woke 0 threads\n"); return false; }
    
    if (t->state != THREAD_READY) { printf("t->state %d != READY\n", t->state); return false; }
    
    return true;
}
