/*
 * wait.c - waitpid(2) wrapper
 *
 * Backed by SYS_WAITPID (7) under native dispatch.  The wrapper
 * preserves the raw kernel return value (PID on success, 0 if
 * WNOHANG and no child, -errno on failure); higher-level libc
 * is responsible for setting errno.
 */

#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/wait.h>

long syscall(long number, ...);

pid_t waitpid(pid_t pid, int *status, int options) {
    return (pid_t)syscall(SYS_WAITPID, (long)pid, (long)status, options);
}
