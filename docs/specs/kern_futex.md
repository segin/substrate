# Kernel Futex Specification

## Overview
Futexes (Fast Userspace Mutexes) provide a foundation for high-performance synchronization in userspace. A futex is a 32-bit integer in userspace whose address is used as a wait channel in the kernel.

## Mechanism
- **FUTEX_WAIT:** 
    - The kernel checks if the value at the userspace address matches the expected value.
    - If it matches, the thread is suspended (put to sleep) on that address.
    - If it does not match, the call returns immediately with an error (EAGAIN).
- **FUTEX_WAKE:**
    - The kernel wakes up a specified number of threads waiting on the userspace address.

## API
### `int sys_futex(int *uaddr, int op, int val, const struct timespec *timeout, int *uaddr2, int val3)`
Primary system call for futex operations.

## Constraints
- Initial implementation only supports `FUTEX_WAIT` and `FUTEX_WAKE`.
- Timeout support is not yet implemented.
- Requeueing is not yet implemented.
