# Kernel Thread (kthread) Specification

## Overview
Kernel threads (`kthreads`) are 1:1 execution units that run entirely in kernel mode. They share the kernel's address space and are managed by the BSD-style scheduler.

## Design
- **Structure:** Managed via the `thread_t` structure.
- **Process Association:** All kthreads belong to the initial kernel process (PID 1).
- **Stack:** Each kthread is allocated its own kernel stack (typically 4KB or 8KB).
- **Context:**
    - Instruction Pointer (EIP/RIP) set to the thread entry point.
    - Stack Pointer (ESP/RSP) set to the top of the allocated kernel stack.
    - Privilege level remains at Ring 0.
- **Lifecycle:**
    - **Creation:** `kthread_create()` allocates resources and marks the thread as `THREAD_READY`.
    - **Exit:** `kthread_exit()` reaps resources and transitions state to `THREAD_ZOMBIE`.

## API
### `int kthread_create(void (*func)(void *), void *arg, thread_t **tdp, const char *name)`
Creates a new kernel thread.
- `func`: Entry point function.
- `arg`: Argument passed to the entry point.
- `tdp`: Pointer to store the new thread structure.
- `name`: Thread name for debugging.

### `void kthread_exit(void)`
Terminates the current kernel thread.

## Constraints
- Not intended for user-mode execution.
- Shared memory model (kernel space).
