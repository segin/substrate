#include <sys/futex.h>
#include <sys/proc.h>
#include <errno.h>
#include <kern/sched.h>
#include <arch/i386/pmap.h>
#include <stddef.h>
#include <stdint.h>

// Forward declarations for sleepq backend (since header is missing)
extern int sleepq_wake_n(void *chan, int n);

// Validate user address (User space < 0xC0000000)
static inline int validate_uaddr(uintptr_t addr) {
    return addr < 0xC0000000 && (addr % 4 == 0);
}

// Get physical address key for a user virtual address
// Returns 0 on failure (page not mapped)
static void *futex_get_key(uintptr_t uaddr) {
    if (!current_process || !current_process->pmap) return NULL;
    
    // Use pmap_extract to get physical address
    // This ensures threads/processes mapping the same page get the same key
    uintptr_t pa = pmap_extract(current_process->pmap, uaddr);
    
    // If not mapped, we can't key off it
    if (pa == 0) return NULL;
    
    return (void *)pa;
}

int sys_futex(int *uaddr, int op, int val, void *timeout, int *uaddr2, int val3) {
    (void)timeout; (void)uaddr2; (void)val3; // TODO: Implement timeout and requeue

    if (!uaddr || !validate_uaddr((uintptr_t)uaddr)) {
        return -EFAULT;
    }

    // Extract operation and flags
    int cmd = op & FUTEX_CMD_MASK;
    // int private_flag = op & FUTEX_PRIVATE_FLAG; // Optimization TODO

    void *key = futex_get_key((uintptr_t)uaddr);
    if (!key) {
        return -EFAULT; // Page must be present
    }

    switch (cmd) {
        case FUTEX_WAIT: {
            // Atomic check: if (*uaddr == val) then sleep
            // Note: Direct access checks are done by validate_uaddr, but
            // we really should use atomic_load/copyin. For now, direct load.
            int current_val = *uaddr;
            
            if (current_val != val) {
                return -EAGAIN;
            }
            
            // Sleep on the physical address key
            // sched_sleep handles the atomic sleepq_add + switch
            sched_sleep(key);
            
            // Return 0 on wake (or EINTR if we had signal handling)
            return 0;
        }

        case FUTEX_WAKE: {
            // Wake up 'val' threads waiting on this key
            // Use sleepq_wake_n directly to get the count of woken threads
            int ret = sleepq_wake_n(key, val);
            return ret;
        }

        default:
            return -ENOSYS;
    }
}
