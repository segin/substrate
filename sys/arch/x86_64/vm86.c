/*
 * vm86.c - virtual-8086 mode on the x86_64 kernel: there is none
 *
 * Long mode drops virtual-8086 mode, so the vm86 system calls fail with
 * ENOSYS and BIOS calls through it are unavailable.  The callers already
 * cope: the vm86 syscalls are optional for their personalities, and video
 * mode setting has native drivers (BGA, VBE via the boot loader).
 */
#include <errno.h>

#include <sys/kern_syscalls.h>
#include <sys/vm86.h>

int sys_vm86(void *v) {
    (void)v;
    return -ENOSYS;
}

int vm86_init_bsd(void *args) {
    (void)args;
    return -ENOSYS;
}

int vm86_bios_call(int int_no, struct vm86_regs *regs) {
    (void)int_no;
    (void)regs;
    return -ENOSYS;
}
