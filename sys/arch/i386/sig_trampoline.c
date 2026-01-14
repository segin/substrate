
#include "syscall.h"

/*
 * Signal Trampoline Code (VDSO-like)
 * 
 * This code is copied to a page at SIG_TRAMPOLINE_ADDR (0xFFFF1000).
 * It is used as the return address for signal handlers.
 * It calls sys_sigreturn to restore the user context.
 */

/*
 * Trampoline assembly:
 *  mov $SYS_SIGRETURN, %eax
 *  int $0x80
 *  nop
 */
 
// Hex encoding: b8 77 00 00 00 cd 80 90
// SYS_SIGRETURN = 119 = 0x77
unsigned char sig_trampoline_code[] = {
    0xB8, 0x77, 0x00, 0x00, 0x00, // mov $119, %eax
    0xCD, 0x80,                   // int $0x80
    0x90                          // nop
};

unsigned int sig_trampoline_size = sizeof(sig_trampoline_code);
