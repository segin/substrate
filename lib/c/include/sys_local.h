#ifndef _LIBC_SYS_LOCAL_H
#define _LIBC_SYS_LOCAL_H

#include <stdint.h>

/* Internal syscall wrappers (assembly implementation) */
int64_t _syscall0(int sys_num);
int64_t _syscall1(int sys_num, uintptr_t a1);
int64_t _syscall2(int sys_num, uintptr_t a1, uintptr_t a2);
int64_t _syscall3(int sys_num, uintptr_t a1, uintptr_t a2, uintptr_t a3);
int64_t _syscall4(int sys_num, uintptr_t a1, uintptr_t a2, uintptr_t a3, uintptr_t a4);
/*
 * For syscalls the kernel really does return 64 bits from (currently only
 * SYS_LSEEK).  _syscall4 sign-extends EAX over EDX, which is right for the
 * 32-bit majority but destroys a genuine high word; this one keeps EDX.
 */
int64_t _syscall4_ll(int sys_num, uintptr_t a1, uintptr_t a2, uintptr_t a3, uintptr_t a4);
int64_t _syscall5(int sys_num, uintptr_t a1, uintptr_t a2, uintptr_t a3, uintptr_t a4, uintptr_t a5);
int64_t _syscall6(int sys_num, uintptr_t a1, uintptr_t a2, uintptr_t a3, uintptr_t a4, uintptr_t a5, uintptr_t a6);

#endif
