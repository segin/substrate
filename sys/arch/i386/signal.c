/*
 * sys/arch/i386/signal.c - Architecture-specific signal handling
 */

#include <sys/signal.h>
#include <sys/proc.h>
#include <kern/console.h>
#include "idt.h" // for registers_t

/*
 * sendsig - Deliver a signal to a user process
 * 
 * This function prepares the user stack frame for the signal handler.
 * It pushes the context, signal number, and return address.
 */
void sendsig(sig_t handler, int sig, uint32_t mask, registers_t *regs) {
    if (!regs) return;

    // TODO: Implement full frame pushing logic
    // For now, just log that we reached here, replacing the old panic
    kprint("sendsig: Delivering signal %d to handler %p (EIP=%x, ESP=%x)\n", 
           sig, handler, regs->eip, regs->useresp);
           
    // This is where we will:
    // 1. Calculate new stack pointer
    // 2. Setup struct sigframe
    // 3. Copy frame to user stack (copyout)
    // 4. Update regs->esp and regs->eip
}
