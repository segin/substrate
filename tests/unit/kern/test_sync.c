#include <stdbool.h>
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

bool test_mutex_contention(void) {
    sched_init();
    mutex_t m;
    mutex_init(&m, "contend");
    
    char s1[4096];
    thread_t *t_tid1 = sched_create_thread(current_process, (void*)0x1, s1 + 4096, NULL); int tid1 = t_tid1->tid;
    thread_t *t1 = sched_get_thread(tid1);
    
    // Simulate tid1 holding the lock
    current_thread = t1;
    current_thread->state = THREAD_RUNNING;
    mutex_lock(&m);
    
    char s2[4096];
    thread_t *t_tid2 = sched_create_thread(current_process, (void*)0x2, s2 + 4096, NULL); int tid2 = t_tid2->tid;
    thread_t *t2 = sched_get_thread(tid2);
    
    // Simulate tid2 trying to lock and failing (blocking)
    // We call sched_sleep manually because calling mutex_lock would loop forever 
    // since sched_yield() doesn't actually stop the execution of the caller in host mocks.
    current_thread = t2;
    current_thread->state = THREAD_RUNNING;
    sched_sleep(&m);
    
    if (t2->state != THREAD_BLOCKED) return false;
    if (t2->wait_chan != &m) return false;
    
    // Now switch back to tid1 and unlock
    current_thread = t1;
    current_thread->state = THREAD_RUNNING;
    mutex_unlock(&m);
    
    // tid2 should now be ready
    if (t2->state != THREAD_READY) return false;
    if (t2->wait_chan != NULL) return false;
    
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
    thread_t *t_tid = sched_create_thread(current_process, (void*)0x1, stack + 4096, NULL); int tid = t_tid->tid;
    thread_t *t = sched_get_thread(tid);
    
    // Simulate t trying to wait and blocking
    current_thread = t;
    current_thread->state = THREAD_RUNNING;
    
    // Manual wait logic because sema_wait would loop on sched_sleep
    sched_sleep(&s);
    
    if (t->state != THREAD_BLOCKED) return false;
    
    // Now post from another thread (kernel thread)
    current_thread = sched_get_thread(1);
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
    thread_t *t_tid = sched_create_thread(current_process, (void*)0x1, stack + 4096, NULL); int tid = t_tid->tid;
    thread_t *t = sched_get_thread(tid);
    
    // Simulate t calling FUTEX_WAIT
    current_thread = t;
    current_thread->state = THREAD_RUNNING;
    
    // Manual simulation of sys_futex call that blocks
    sys_futex(&futex_val, FUTEX_WAIT, 10, NULL, NULL, 0);
    
    if (t->state != THREAD_BLOCKED) return false;
    if (t->wait_chan != &futex_val) return false;
    
    // Now wake up from kernel thread
    current_thread = sched_get_thread(1);
    current_thread->state = THREAD_RUNNING;
    sys_futex(&futex_val, FUTEX_WAKE, 1, NULL, NULL, 0);
    
    if (t->state != THREAD_READY) return false;
    
    return true;
}
