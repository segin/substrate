/* pthread_mutex_*  — simple test-and-set spinlock.  Other pthread
 * functions live in pthread_create.c, pthread_cond.c, pthread_sig.c. */

#include "pthread.h"

int pthread_mutex_init(pthread_mutex_t *mutex, const void *attr) {
    (void)attr;
    *mutex = 0;
    return 0;
}

int pthread_mutex_lock(pthread_mutex_t *mutex) {
    /* Spinlock for now (unsafe if single core without preemption) */
    while (__sync_lock_test_and_set(mutex, 1)) {
        /* yield(); */
    }
    return 0;
}

int pthread_mutex_unlock(pthread_mutex_t *mutex) {
    __sync_lock_release(mutex);
    return 0;
}

int pthread_mutex_destroy(pthread_mutex_t *mutex) {
    (void)mutex;
    return 0;
}
