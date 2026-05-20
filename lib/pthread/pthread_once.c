/*
 * pthread_once.c — one-shot initializer for libpthread.
 *
 * State encoding (matches the pthread_once_t typedef in pthread.h):
 *   0 = not yet run
 *   1 = currently running init_routine
 *   2 = complete
 *
 * Contention path: spin on sched_yield() until the running thread
 * marks complete.  Substrate's sched_yield is the SYS_SCHED_YIELD
 * syscall via libsys, so the spin doesn't hot-loop the CPU.
 */

#include <pthread.h>
#include <stddef.h>
#include <sched.h>

int pthread_once(pthread_once_t *once_control, void (*init_routine)(void)) {
    if (!once_control || !init_routine) return 22;  /* EINVAL */

    int prev = __sync_val_compare_and_swap(once_control, 0, 1);
    if (prev == 2) return 0;  /* already done */
    if (prev == 0) {
        init_routine();
        __sync_synchronize();
        *once_control = 2;
        return 0;
    }
    /* prev == 1: another thread is running it.  Wait. */
    while (*once_control != 2) sched_yield();
    return 0;
}
