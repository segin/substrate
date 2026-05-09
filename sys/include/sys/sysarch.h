#ifndef _SYS_SYSARCH_H
#define _SYS_SYSARCH_H

#include <stdint.h>

#define I386_VM86       6  /* FreeBSD/NetBSD i386 VM86 */
#define I386_GET_FSBASE 7
#define I386_SET_FSBASE 8
#define I386_GET_GSBASE 9
#define I386_SET_GSBASE 10

// Sub-functions for I386_VM86
#define VM86_INIT       1
#define VM86_GET_VME    2
#define VM86_INTCALL    3

struct i386_vm86_args {
    int     sub_op;       /* sub-operation to perform */
    void    *sub_args;    /* argument to sub-operation */
};

/* Direct (non-syscall) entry point for installing a TLS base on the
 * current thread.  Used by foreign personalities whose own TLS
 * primitive (e.g. NetBSD _lwp_setprivate, Linux set_thread_area)
 * receives the base by value rather than via a userspace pointer,
 * so they can't go through sys_sysarch which copyin()s the arg. */
int i386_set_gsbase(uint32_t base);

#endif
