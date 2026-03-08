# Process Group, Session, and CTTY Lifecycle

## Scope

This document describes the current BSD-style process-group and session model implemented in `sys/pm/pgrp.c`, together with the controlling-terminal interactions currently handled by the tty layer.

## Core Ownership Model

Each live process may reference one process group through `p_pgrp`.
Each process group belongs to exactly one session through `pg_session`.

Current structural invariants:

- `struct pgrp` owns a member list via each member's `p_pgrp_link`
- `struct pgrp` participates in two independent lists:
  - global hash chain via `pg_hash_next`
  - owning session list via `pg_sess_next`
- `struct session` owns its group list through `s_pgrps`

The split hash/session linkage is required so hash-table membership does not corrupt per-session enumeration.

## Session Creation

`sys_setsid()`:

1. rejects callers that are already process-group leaders
2. allocates a new `session`
3. allocates a new `pgrp` owned by that session
4. inserts the calling process as both session leader and process-group leader
5. detaches the caller from any prior controlling terminal association

The new session ID is the caller PID.

## Group Management

`sys_setpgid(pid, pgid)` supports:

- moving the caller or one of its children
- joining an existing group in the same session
- creating a new group when `pgid == target->pid`

The implementation rejects cross-session joins.

`sys_getpgid()` and `sys_getsid()` are simple lookups over the live process table.

## Orphaned Process Groups

A process group is treated as orphaned when no member has a parent in a different process group within the same session.

The current kernel checks this with `pgrp_is_orphaned()`.
When a group becomes orphaned and has at least one stopped member, `pgrp_check_orphan()` sends:

1. `SIGHUP`
2. `SIGCONT`

to the entire group.

This matches the expected BSD/POSIX job-control recovery path for stopped orphaned groups.

## Child Reparenting

`proc_reparent_children()` moves all children of an exiting process to:

- init (`PID 1`) in the normal case
- swapper (`PID 0`) if init is already dying or a zombie

The operation is serialized by `proctree_lock`.
Children are relinked onto the new parent's `p_children` list and the new parent is woken on its `p_children` wait channel.

## Controlling Terminal Model

Controlling-terminal state is currently split between:

- `process->tty`: live controlling tty pointer used by the process
- `tty->session`: owning session ID
- `tty->pgrp`: foreground process group ID
- `session->s_ttyvp`: reserved session-side vnode field, not yet the authoritative ownership source

Current implemented tty-side operations:

- `TIOCSCTTY`: assigns the tty to the calling session leader, rejects foreign ownership unless explicit steal is requested, and sets foreground pgrp to the caller's process group
- `TIOCNOTTY`: when invoked by the owning session, performs tty hangup semantics (`SIGHUP` + `SIGCONT` to the foreground group) and clears tty ownership
- `TIOCSPGRP` / `TIOCGPGRP`: set and query the foreground process group on the tty
- `tty_hangup()`: sends `SIGHUP` and `SIGCONT` to the foreground group, then clears tty session ownership

## Exit-Path Boundary

Process exit and final reap are intentionally distinct in the current kernel:

- `proc_exit()` handles resource teardown, child reparenting, and parent notification
- `wait4()` performs final process-group removal and slot reclamation

This split keeps zombie group identity available to the wait subsystem today.
If process-group removal moves earlier into `proc_exit()`, the kernel must preserve enough exit-time group identity for `waitpid(0, ...)` and `waitpid(-pgid, ...)`.
