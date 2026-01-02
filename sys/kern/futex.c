#include "../sys/futex.h"
#include "../sys/proc.h"
#include "sched.h"
#include <stddef.h>
#include <stdint.h>

int sys_futex(int *uaddr, int op, int val, void *timeout, int *uaddr2, int val3) {
    (void)timeout; (void)uaddr2; (void)val3;

    if (!uaddr) return -1;

    switch (op) {
        case FUTEX_WAIT:
            // Atomic check: if (*uaddr == val) then sleep
            // In a real kernel we need copy_from_user and pinning
            if (*uaddr != val) return -1; // EAGAIN
            sched_sleep(uaddr);
            return 0;

        case FUTEX_WAKE:
            // Wake up 'val' threads
            sched_wakeup_n(uaddr, val);
            return 0;

        default:
            return -1; // ENOSYS
    }
}
