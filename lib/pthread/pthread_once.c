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
 *
 * Cancellation: pthread_once is not itself a cancellation point, but
 * init_routine may be one.  POSIX requires that if init_routine is
 * canceled, once_control be left as if pthread_once had never been called
 * so a later call re-runs init.  A cleanup handler pushed around
 * init_routine resets the state to 0 on cancellation; a thread that was
 * waiting for the (now canceled) initializer re-claims it.
 */

#include <pthread.h>
#include <stddef.h>
#include <sched.h>

/* Reset once_control to "not yet run" if init_routine is canceled. */
static void once_cancel_cleanup(void *arg) {
    pthread_once_t *once_control = (pthread_once_t *)arg;
    __sync_synchronize();
    *once_control = 0;
}

int pthread_once(pthread_once_t *once_control, void (*init_routine)(void)) {
    if (!once_control || !init_routine) return 22;  /* EINVAL */

    for (;;) {
        int prev = __sync_val_compare_and_swap(once_control, 0, 1);
        if (prev == 2) return 0;  /* already done */
        if (prev == 0) {
            /* We claimed the initialization.  Run init under a cleanup handler
             * so a cancellation inside init_routine resets once_control. */
            pthread_cleanup_push(once_cancel_cleanup, once_control);
            init_routine();
            pthread_cleanup_pop(0);
            __sync_synchronize();
            *once_control = 2;
            return 0;
        }
        /* prev == 1: another thread is running it.  Wait for it to complete —
         * or, if that thread is canceled (which resets once_control to 0),
         * loop back and try to claim the initialization ourselves. */
        while (*once_control == 1)
            sched_yield();
        if (*once_control == 2) return 0;
        /* fell back to 0 (initializer canceled): retry the claim */
    }
}
