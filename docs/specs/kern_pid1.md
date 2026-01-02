# PID 1 (Init) Handling Specification

## Overview
The process with PID 1 (init) is the ancestor of all other processes in TestUnix. it has special protections to ensure system stability.

## Protections
- **Immunity to Signals:** `sys_kill()` prevents sending termination signals (`SIGKILL`, `SIGTERM`) or stop signals (`SIGSTOP`) to PID 1.
- **Exit Prevention:** `sys_exit()` will trigger a kernel panic if called by the init process, as the system cannot function without it.
- **Reaping:** Init is responsible for reaping orphan processes (to be fully implemented in the reaper logic).

## API
- `sys_exit(code)`: Panics if `current_process->pid == 1`.
- `sys_kill(1, sig)`: Returns `-1` for termination/stop signals.

## Constraints
- Future work: Implement an automatic respawn mechanism for init instead of a hard panic if possible.
- Orphan processes should be automatically reparented to PID 1.
