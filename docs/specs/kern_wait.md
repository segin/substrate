# `wait4` / `waitpid` Internal Semantics

## Scope

This document describes the current kernel-internal behavior of the wait subsystem in `sys/pm/wait.c`.
It is an implementation-facing contract for `kern_wait4()` and the search/reap rules it applies to a caller's child list.

## Search Model

`kern_wait4(pid, status, options, rusage)` scans only the calling process's `p_children` list.

Supported selector modes:

- `pid > 0`: match one specific child PID
- `pid == -1`: match any child
- `pid == 0`: match a child in the same process group as the caller
- `pid < -1`: match a child whose process group is `abs(pid)`

The kernel performs a full rescan after every wakeup. No cached iterator state is retained across sleeps.

## Waitable Child States

The search logic recognizes three reportable child conditions:

- `SZOMB`: always waitable and eligible for reap
- `SSTOP`: reportable only when `WUNTRACED` is set and the child has not already been reported (`P_WAITED` clear)
- `P_CONTINUED`: reportable only when `WCONTINUED` is set

If no child matches the selector at all, the call returns `-ECHILD`.
If matching children exist but none are reportable and `WNOHANG` is set, the call returns `0`.

## Blocking Behavior

When no matching child is currently reportable and `WNOHANG` is clear, the caller sleeps on its own `&p_children` wait channel.

Before sleeping, the kernel checks for an unmasked pending signal on the calling thread:

- if one is present, the call returns `-EINTR`
- otherwise the caller blocks and rescans on wake

Parents are woken on the same `&p_children` channel when a child becomes waitable or is reparanted into the parent.

## Reap Path

When a zombie child is selected:

1. The child's exit status is copied to the caller's `status` result.
2. The child's `rusage` is copied to the caller if requested.
3. The child's resource usage is accumulated into the parent's `rusage_children`.
4. The child is unlinked from the parent's child list.
5. The child is removed from its process group.
6. All thread slots belonging to the child are retired.
7. The process slot is returned to the free pool by setting `pid = -1` and clearing hierarchy state.

This is the point where the kernel currently performs final process-group removal.

## Job Control Reports

Stopped children:

- reported once while `P_WAITED` is clear
- reported status uses the traditional stopped encoding (`0x7f | signo << 8`)
- `P_WAITED` is set after reporting

Continued children:

- reported when `P_CONTINUED` is set and `WCONTINUED` is requested
- returned status is `0xffff`
- `P_CONTINUED` is cleared after reporting

## Known Design Boundaries

- The wait subsystem currently depends on the child remaining linked from the parent's `p_children` list until final reap.
- Group-based wait selection currently derives the child's process group from `p_pgrp`; any future earlier group-detach during `proc_exit()` must preserve enough group identity for `waitpid(0, ...)` and `waitpid(-pgid, ...)` to remain correct for zombies.
