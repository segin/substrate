
#include <arch/i386/syscall.h>

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
    0xEB, 0xFE,                   // jmp .              ; Halt if return (16 bytes, ends at 0x10)

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
    0xEB, 0xFE,                   // jmp .              ; Halt if return (16 bytes, ends at 0x20)

    /* Offset 0x20: NetBSD sigreturn trampoline.
     *
     * When a NetBSD signal handler returns, ESP points at the handler
     * argument block [ sig ][ code ][ scp ] (the return address has just
     * been popped).  netbsd_sys_sigreturn() reads the sigcontext pointer
     * from EBX, so load it from the scp slot (ESP+8) before issuing the
     * sigreturn syscall (119, routed to the personality .sigreturn hook).
     */
    0x8B, 0x5C, 0x24, 0x08,       // mov 0x8(%esp), %ebx ; ebx = scp
    0xB8, 0x77, 0x00, 0x00, 0x00, // mov $119, %eax      ; SYS_sigreturn
    0xCD, 0x80,                   // int $0x80           ; Syscall
    0xEB, 0xFE,                   // jmp .               ; Halt if return
    0x90, 0x90, 0x90,             // padding (total 16 bytes)

    /* Offset 0x30: FreeBSD sigreturn trampoline.
     *
     * Identical shape to the NetBSD slot: a FreeBSD handler is entered
     * directly and returns here with ESP at [ sig ][ code ][ scp ] (the
     * return address just popped).  freebsd_sys_sigreturn() reads the
     * sigcontext pointer from EBX, so load it from the scp slot (ESP+8)
     * and issue sigreturn (syscall 119, routed to the personality
     * .sigreturn hook). */
    0x8B, 0x5C, 0x24, 0x08,       // mov 0x8(%esp), %ebx ; ebx = scp
    0xB8, 0x77, 0x00, 0x00, 0x00, // mov $119, %eax      ; SYS_sigreturn
    0xCD, 0x80,                   // int $0x80           ; Syscall
    0xEB, 0xFE,                   // jmp .               ; Halt if return
    0x90, 0x90, 0x90,             // padding (total 16 bytes)

    /* Offset 0x40: Linux legacy sigreturn trampoline (__kernel_sigreturn).
     *
     * Linux passes NOTHING to sigreturn on the stack -- the kernel recovers
     * the frame from ESP alone (linux_sys_sigreturn: frame = ESP - 8).  A
     * Linux handler returns here with ESP at [ sig ][ sigcontext... ]; the
     * pop discards `sig` so ESP lands on the sigcontext, i.e. frame + 8.
     *
     * Do NOT push an argument the way the native slot at 0x00 does: that
     * lowers ESP by 8 and the kernel then reads the sigcontext 12 bytes
     * below the real frame, restoring GS from the pushed dummy return
     * address and FS from the pushed pointer -- a garbage selector that
     * faults on the IRET path, in the kernel, with the trap frame already
     * half-restored (unrecoverable GP, not a signal). */
    0x58,                         // popl %eax           ; discard sig
    0xB8, 0x77, 0x00, 0x00, 0x00, // mov $119, %eax      ; SYS_sigreturn
    0xCD, 0x80,                   // int $0x80           ; Syscall
    0xEB, 0xFE,                   // jmp .               ; Halt if return
    0x90, 0x90, 0x90, 0x90, 0x90, 0x90, // padding (total 16 bytes)

    /* Offset 0x50: Linux rt_sigreturn trampoline (__kernel_rt_sigreturn).
     *
     * Same rule, one word less: an SA_SIGINFO handler returns with ESP at
     * [ sig ][ pinfo ][ puc ][ ... ] and Linux's rt frame starts one word
     * back (linux_sys_rt_sigreturn: frame = ESP - 4), so there is nothing
     * to pop and nothing to push. */
    0xB8, 0xF7, 0x00, 0x00, 0x00, // mov $247, %eax      ; SYS_rt_sigreturn
    0xCD, 0x80,                   // int $0x80           ; Syscall
    0xEB, 0xFE,                   // jmp .               ; Halt if return
    0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90 // padding (total 16 bytes)
};

unsigned int sig_trampoline_size = 4096;
