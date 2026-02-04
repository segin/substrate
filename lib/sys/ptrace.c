/*
 * lib/sys/ptrace.c
 *
 * ptrace syscall wrapper.
 */

#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>

long syscall(long number, ...);

long ptrace(int request, pid_t pid, void *addr, int data) {
    return syscall(SYS_PTRACE, request, pid, addr, data);
}
