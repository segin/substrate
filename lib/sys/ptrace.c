/*
 * lib/sys/ptrace.c
 *
 * ptrace syscall wrapper.
 */

#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>

long ptrace(int request, pid_t pid, void *addr, int data) {
    /* For PEEK requests -1 can be a legitimate data value, so the POSIX
     * idiom is to clear errno before the call and inspect it after.  A
     * negative-errno raw return maps to -1 + errno. */
    errno = 0;
    long r = syscall(SYS_PTRACE, request, pid, addr, data);
    if (r < 0 && r >= -4095) {
        errno = (int)-r;
        return -1;
    }
    return r;
}
