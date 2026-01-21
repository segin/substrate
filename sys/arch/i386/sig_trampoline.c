
#include "syscall.h"

/*
 * Signal Trampoline Code (VDSO-like)
 * 
 * This code is copied to a page at SIG_TRAMPOLINE_ADDR (0xFFFF1000).
 * It is used as the return address for signal handlers.
 * It calls sys_sigreturn to restore the user context.
 */

/*
 * Trampoline assembly (Legacy):
 *  lea 4(%esp), %eax   # Get pointer to sigcontext (right after sig arg)
 *  push %eax           # Push as first argument to sys_sigreturn
 *  mov $119, %eax      # SYS_sigreturn
 *  int $0x80           # Invoke kernel
 * 
 * Trampoline assembly (RT):
 *  lea 8(%esp), %eax   # Get pointer to ucontext (after sig and info_ptr args)
 *  push %eax           # Push as first argument to sys_rt_sigreturn
 *  mov $247, %eax      # SYS_rt_sigreturn
 *  int $0x80           # Invoke kernel
 */

unsigned char sig_trampoline_code[4096] __attribute__((aligned(4096))) = {
    /* Offset 0x00: Legacy sigreturn */
    0x8D, 0x44, 0x24, 0x04,       // lea 4(%esp), %eax
    0x50,                         // push %eax
    0xB8, 0x77, 0x00, 0x00, 0x00, // mov $119, %eax
    0xCD, 0x80,                   // int $0x80
    0xEB, 0x03,                   // jmp to end/halt (should not reach)
    0x90, 0x90, 0x90,

    /* Offset 0x10: RT sigreturn (SA_SIGINFO) */
    0x8D, 0x44, 0x24, 0x08,       // lea 8(%esp), %eax
    0x50,                         // push %eax
    0xB8, 0xF7, 0x00, 0x00, 0x00, // mov $247, %eax (0xF7 = 247)
    0xCD, 0x80,                   // int $0x80
    0x90, 0x90, 0x90, 0x90, 0x00
};

unsigned int sig_trampoline_size = 4096;
