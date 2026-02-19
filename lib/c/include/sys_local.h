#ifndef _LIBC_SYS_LOCAL_H
#define _LIBC_SYS_LOCAL_H

#include <stdint.h>

/* Internal syscall wrappers (assembly implementation) */
extern int64_t _syscall0(int sys_num);
extern int64_t _syscall1(int sys_num, int a1);
extern int64_t _syscall2(int sys_num, int a1, int a2);
extern int64_t _syscall3(int sys_num, int a1, int a2, int a3);
extern int64_t _syscall4(int sys_num, int a1, int a2, int a3, int a4);
extern int64_t _syscall5(int sys_num, int a1, int a2, int a3, int a4, int a5);
extern int64_t _syscall6(int sys_num, int a1, int a2, int a3, int a4, int a5, int a6);

#endif
