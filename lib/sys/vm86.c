/*
 * vm86.c - vm86() syscall wrapper
 *
 * Provides a typed wrapper for the VM86 system call.
 */
#include <sys/syscall.h>
#include <sys/vm86.h>
#include <unistd.h>

int vm86(struct vm86_struct *info) {
    return (int)syscall(SYS_VM86, info);
}
