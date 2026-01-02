#include <stdbool.h>
#include <stddef.h>
#include "../../../sys/sys/lock.h"
#include "../../../sys/kern/sched.h"

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
    int tid1 = sched_create_thread(current_process, (void*)0x1, s1 + 4096, NULL);
    thread_t *t1 = sched_get_thread(tid1);
    
    // Simulate tid1 holding the lock
    current_thread = t1;
    current_thread->state = THREAD_RUNNING;
    mutex_lock(&m);
    
    char s2[4096];
    int tid2 = sched_create_thread(current_process, (void*)0x2, s2 + 4096, NULL);
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
    int tid = sched_create_thread(current_process, (void*)0x1, stack + 4096, NULL);
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
