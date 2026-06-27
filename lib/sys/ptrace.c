/*
 * lib/sys/ptrace.c
 *
 * ptrace(2) syscall wrapper.
 *
 * The kernel returns 0/-errno for every request and, for PTRACE_PEEK*, stores
 * the read word through the `data` pointer.  This wrapper re-exposes the
 * classic POSIX/Linux convention where PTRACE_PEEK* returns the word itself —
 * callers clear errno before the call and inspect it after to disambiguate a
 * genuine word of -1.
 */

#include <sys/syscall.h>
#include <sys/ptrace.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>

long ptrace(int request, pid_t pid, void *addr, void *data) {
    errno = 0;

    if (request == PTRACE_PEEKTEXT ||
        request == PTRACE_PEEKDATA ||
        request == PTRACE_PEEKUSER) {
        unsigned long word = 0;
        long r = syscall(SYS_PTRACE, request, pid, addr, &word);
        if (r < 0 && r >= -4095) {
            errno = (int)-r;
            return -1;
        }
        return (long)word;
    }

    long r = syscall(SYS_PTRACE, request, pid, addr, data);
    if (r < 0 && r >= -4095) {
        errno = (int)-r;
        return -1;
    }
    return r;
}
