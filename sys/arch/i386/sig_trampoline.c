
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
    /* Offset 0x00: Legacy sigreturn (sys_sigreturn) */
    /* 
     * Stack: [ sig ][ sigcontext... ]
     * We need dummy RET for "substrate" personality calling convention.
     */
    0x8D, 0x44, 0x24, 0x04,       // lea 4(%esp), %eax  ; Load &sigcontext
    0x50,                         // push %eax          ; Push Arg 1 (ptr)
    0x6A, 0x00,                   // push $0            ; Push Dummy Return Address
    0xB8, 0x77, 0x00, 0x00, 0x00, // mov $119, %eax     ; SYS_sigreturn
    0xCD, 0x80,                   // int $0x80          ; Syscall
    0xEB, 0xFE,                   // jmp .              ; Halt if return
    0x90, 0x90,                   // padding (total 16 bytes)

    /* Offset 0x10: RT sigreturn (sys_rt_sigreturn) */
    /*
     * Stack: [ sig ][ info_ptr ][ ucontext_ptr ]
     * ucontext_ptr at ESP+8 contains the address of ucontext. We need to pass that value.
     */
    0x8B, 0x44, 0x24, 0x08,       // mov 8(%esp), %eax  ; Load ucontext_ptr (value)
    0x50,                         // push %eax          ; Push Arg 1 (ptr)
    0x6A, 0x00,                   // push $0            ; Push Dummy Return Address
    0xB8, 0xF7, 0x00, 0x00, 0x00, // mov $247, %eax     ; SYS_rt_sigreturn
    0xCD, 0x80,                   // int $0x80          ; Syscall
    0xEB, 0xFE,                   // jmp .              ; Halt if return
    0x90, 0x90                    // padding (total 16 bytes)
};

unsigned int sig_trampoline_size = 4096;
