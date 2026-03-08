# Process Exit Teardown

## Scope

This document describes the current internal `proc_exit()` teardown path implemented in `sys/pm/process.c`.
It is an implementation contract for the existing kernel behavior, not a claim that every long-term Unix exit feature is complete.

## Entry Conditions

`sys_exit(status)`, `sys__exit(status)`, and signal-driven `sigexit()` all converge on `proc_exit(code)`.

Special case:

- if the current process is init (`PID 1`), the kernel prints a warning and halts in place rather than attempting to recover

## Current Phase Order

### 1. Enter Dying State

The process immediately transitions to:

- `state = SDYING`
- `exit_code = code`

This blocks further ordinary signal delivery because `psignal()` ignores processes already in `SDYING` or `SZOMB`.

### 2. Thread-Level Exit Cleanup

For every thread belonging to the process:

- robust futex cleanup is invoked through `futex_thread_exit()`
- pending signals are cleared
- non-current threads are marked `THREAD_ZOMBIE`

Later in the teardown, all process threads are forced to `THREAD_ZOMBIE` and woken through the thread object as a sleepq channel.

### 3. Accounting and Resource Release

The current implementation performs:

- `acct_process(code)`
- `fd_close_all(process)`
- `close_fs(cwd)`
- `close_fs(root)` when root differs from the global root
- switch to `pmap_kernel()` immediately so the exiting thread no longer runs on
  its dying user pmap
- defer `vm_map_destroy()` / `pmap_release()` to the final reap path in
  `wait4()` or async autoreap once the zombie is no longer visible to the parent

Notes:

- timer, System V IPC, POSIX IPC, and lock-release phases remain explicit placeholders

### 4. Child Reparenting

`proc_reparent_children(process)` moves all children to:

- init (`PID 1`) normally
- swapper (`PID 0`) if init is already dying or zombified

The new parent is woken on its `p_children` wait channel after the move.

### 5. Controlling Terminal Cleanup

If the exiting process is the leader of its session and has a controlling tty:

- `tty_hangup()` is called
- the process tty pointer is cleared

`tty_hangup()` sends `SIGHUP` and `SIGCONT` to the tty foreground group and then clears tty session ownership.

### 6. Resource Usage Finalization

`rusage_finalize(process)` runs before the process becomes waitable as a zombie.

### 7. Zombie Transition and Parent Notification

The process then transitions to:

- `state = SZOMB`

If a parent exists:

- the parent is sent `SIGCHLD`
- the parent is woken on `&parent->p_children`

Current boundary:

- `SA_NOCLDWAIT` children are detached from the parent's waitable child list and
  reclaimed asynchronously after all thread contexts retire
- final process-group removal still happens during `wait4()` reap, not in `proc_exit()`

### 8. Final Thread Retirement

Every thread in the exiting thread group is forced to:

- `state = THREAD_ZOMBIE`

and sleepers are woken via the thread object channel.

At final reap time (`wait4()` or async autoreap), the scheduler retires each
thread slot and releases any scheduler-owned kernel stack backing that thread:

- `fork()` child stacks allocated from PMM contiguous pages are freed
- scheduler-spawned kernel-process stacks allocated from PMM single pages are freed
- kmalloc-backed kthread stacks are freed
- borrowed or CPU-local idle stacks are not freed by the generic reap path

### 9. Final Context Switch

The current thread calls `sched_yield()` and is not expected to resume execution.

## Intentional Boundary with `wait4()`

The current kernel splits process death into two stages:

- `proc_exit()`: teardown and transition to waitable zombie
- `wait4()`: final reap, group removal, thread-stack release, thread-slot retirement, and process-slot return to the free pool

This split preserves enough process identity for the present wait implementation, especially for child-list traversal and group-based wait selection.

During the zombie window, the kernel guarantees only wait-visible state and
final-reap bookkeeping remain meaningful:

- `pid`, `ppid`, `exit_code`, and `rusage`
- parent linkage needed for `wait4()` traversal when the process is still
  waitable
- process-group/session linkage that must survive until final reap for
  group-based wait and orphan checks

Active runtime resources such as file descriptors, cwd/root references, tty
ownership, pending signals, and user-execution mappings are already torn down
before the process becomes waitable as `SZOMB`.
