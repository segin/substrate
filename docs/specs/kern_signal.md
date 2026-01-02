# Kernel Signal Specification

## Overview
TestUnix implements a POSIX-like signal mechanism for asynchronous event notification. Signals can be sent to processes or threads and are delivered when the target thread returns to user-mode from an interrupt or system call.

## Signal Delivery
- **Check:** Before returning to user-mode, the kernel checks `current_thread->sig_pending` against `current_thread->sig_mask`.
- **Handling:**
    - `SIG_IGN`: Signal is discarded.
    - `SIG_DFL`: Default action is performed (Terminate, Core, Ignore, Stop, Continue).
    - User-mode handler: A stack frame is set up on the user stack, saving the current register state, and the program counter is set to the handler address. (Note: Full delivery is stubbed in the initial prototype).

## Default Actions
- **Terminate:** `SIGHUP`, `SIGINT`, `SIGKILL`, `SIGPIPE`, `SIGALRM`, `SIGTERM`, `SIGUSR1`, `SIGUSR2`.
- **Core:** `SIGQUIT`, `SIGILL`, `SIGTRAP`, `SIGABRT`, `SIGBUS`, `SIGFPE`, `SIGSEGV`.
- **Ignore:** `SIGCHLD`, `SIGWINCH`.
- **Stop:** `SIGSTOP`, `SIGTSTP`, `SIGTTIN`, `SIGTTOU`.
- **Continue:** `SIGCONT`.

## API
### `int sys_sigaction(int sig, const struct sigaction *act, struct sigaction *oact)`
Sets or retrieves the signal handling action for a specific signal.

### `int sys_sigprocmask(int how, const uint32_t *set, uint32_t *oset)`
Modifies the signal mask of the current thread.

### `int sys_kill(int pid, int sig)`
Sends a signal to a process identified by PID.

### `int sys_sigpending(uint32_t *set)`
Returns the set of signals that are pending for delivery to the calling thread.

### `int sys_sigsuspend(const uint32_t *mask)`
Temporarily replaces the signal mask and suspends the thread until a signal is delivered.

## Constraints
- `SIGKILL` and `SIGSTOP` cannot be ignored or caught.
- Signal delivery is only performed when returning to user-mode.
