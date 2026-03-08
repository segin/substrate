# Kernel Futex Specification

## Overview
Futexes (Fast Userspace Mutexes) provide a foundation for high-performance synchronization in userspace. A futex is a 32-bit integer in userspace whose address is used as a wait channel in the kernel.

## Mechanism
- **Keying model:**
    - Shared futexes use the physical address returned by `pmap_extract()` as the wait key.
    - Private futexes use the user virtual address plus current process identity.
    - Wait queues are provided by the hashed sleep-queue subsystem.
- **FUTEX_WAIT:**
    - The kernel validates the user address and compares the current userspace value against the caller-supplied expected value.
    - If the value matches, the thread sleeps on the futex key.
    - If the value does not match, the call returns immediately with `-EAGAIN`.
    - Relative timeout support is available through a userspace `timespec`.
- **FUTEX_WAKE:**
    - The kernel wakes up to `N` waiters sleeping on the futex key.
- **FUTEX_REQUEUE / FUTEX_CMP_REQUEUE:**
    - Waiters can be moved from one futex key to another without waking all of them.
    - The compare variant validates the source futex value before requeue.
- **Robust futexes:**
    - Threads may register a Linux-compatible robust list with `sys_set_robust_list()`.
    - On thread exit, `futex_exit_cleanup()` walks the list, marks owned futexes with `FUTEX_OWNER_DIED`, and wakes a waiter.
- **PI futexes:**
    - `FUTEX_LOCK_PI`, `FUTEX_UNLOCK_PI`, and `FUTEX_TRYLOCK_PI` are implemented.
    - Kernel-side PI state tracks owner, waiter priorities, and temporary priority boosts.

## API
### `int sys_futex(int *uaddr, int op, int val, const struct timespec *timeout, int *uaddr2, int val3)`
Primary system call for futex operations.

### `int sys_set_robust_list(struct robust_list_head *head, size_t len)`
Registers the current thread's robust futex list.

### `int sys_get_robust_list(int pid, struct robust_list_head **head_ptr, size_t *len_ptr)`
Returns the robust futex list for the requested thread, subject to permission checks.

## Constraints
- User memory access is validated by address-range checks plus `pmap_extract()` presence checks before direct dereference.
- The current implementation uses sleep queues for hashing and bucket locking instead of a dedicated futex wait-table.
- Robust-list cleanup wakes one waiter after owner death, matching mutex handoff expectations.
- PI boosting is local to the tracked futex owner/waiter set and does not yet provide full chain proofs beyond the implemented waiter ordering.
