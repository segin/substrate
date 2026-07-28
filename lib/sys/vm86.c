/*
 * vm86.c - vm86() syscall wrapper
 *
 * Provides a typed wrapper for the VM86 system call.
 */
#include <errno.h>
#include <unistd.h>

#include <sys/syscall.h>
#include <sys/vm86.h>
#include <sysret.h>

int vm86(struct vm86_struct *info) {
    return (int)__sysret(syscall(SYS_VM86, info));
}
