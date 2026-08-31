/*
 * Raw syscall stubs for the host builds of libc.
 *
 * libc_stdio_prefixed.o bundles src/stdlib.c, which reaches the kernel
 * directly for getrandom(2) -- arc4random_buf() and the mkstemp suffix
 * filler both call _syscall3(SYS_GETRANDOM, ...).  There is no kernel under
 * a host test, and the real stubs are i386 assembly (lib/c/arch/i386/
 * syscall.S) that these 32-bit-but-hosted links cannot use.
 *
 * Every call fails, which is the interesting case anyway: it drives libc's
 * userspace entropy fallback rather than the kernel path.
 *
 * Signatures per lib/c/include/sys_local.h -- keep them matched.
 */

#include <stdint.h>

int64_t _syscall0(int sys_num)
{
    (void)sys_num;
    return -1;
}

int64_t _syscall1(int sys_num, uintptr_t a1)
{
    (void)sys_num; (void)a1;
    return -1;
}

int64_t _syscall2(int sys_num, uintptr_t a1, uintptr_t a2)
{
    (void)sys_num; (void)a1; (void)a2;
    return -1;
}

int64_t _syscall3(int sys_num, uintptr_t a1, uintptr_t a2, uintptr_t a3)
{
    (void)sys_num; (void)a1; (void)a2; (void)a3;
    return -1;
}

int64_t _syscall4(int sys_num, uintptr_t a1, uintptr_t a2, uintptr_t a3,
                  uintptr_t a4)
{
    (void)sys_num; (void)a1; (void)a2; (void)a3; (void)a4;
    return -1;
}

int64_t _syscall4_ll(int sys_num, uintptr_t a1, uintptr_t a2, uintptr_t a3,
                     uintptr_t a4)
{
    (void)sys_num; (void)a1; (void)a2; (void)a3; (void)a4;
    return -1;
}

int64_t _syscall5(int sys_num, uintptr_t a1, uintptr_t a2, uintptr_t a3,
                  uintptr_t a4, uintptr_t a5)
{
    (void)sys_num; (void)a1; (void)a2; (void)a3; (void)a4; (void)a5;
    return -1;
}

int64_t _syscall6(int sys_num, uintptr_t a1, uintptr_t a2, uintptr_t a3,
                  uintptr_t a4, uintptr_t a5, uintptr_t a6)
{
    (void)sys_num; (void)a1; (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    return -1;
}
