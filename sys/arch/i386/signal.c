/*
 * sys/arch/i386/signal.c - Architecture-specific signal handling
 */

#include <sys/signal.h>
#include <sys/proc.h>
#include <kern/console.h>
#include "idt.h" 
#include "include/signal_arch.h"
#include <string.h> // for memcpy

// Helper to safely write to user stack (prototype: wrappers memcpy)
static int copyout(const void *src, void *dst, size_t size) {
    // TODO: proper user-space verification and fault handling
    memcpy(dst, src, size);
    return 0;
}

/*
 * sendsig - Deliver a signal to a user process
 * 
 * This function prepares the user stack frame for the signal handler.
 * It pushes the context, signal number, and return address.
 */
void sendsig(sig_t handler, int sig, uint32_t mask, registers_t *regs) {
    if (!regs) return;

    struct sigframe sf;
    struct sigcontext *scp;
    
    // Calculate stack pointer
    uint32_t esp = regs->useresp;
    
    // Align stack to 16 bytes (System V ABI)
    // Reserve space for sigframe
    esp -= sizeof(struct sigframe);
    esp &= ~0xF; // Align down to 16-byte boundary
    
    // Populate sigcontext
    // We point scp to the struct sigcontext INSIDE the sigframe on the stack
    // (Wait, struct sigframe contains struct sigcontext sc)
    scp = &sf.sc;
    
    scp->gs = regs->gs;
    scp->fs = regs->fs;
    scp->es = regs->es;
    scp->ds = regs->ds;
    scp->edi = regs->edi;
    scp->esi = regs->esi;
    scp->ebp = regs->ebp;
    scp->esp = regs->esp; // Kernel ESP? No, this is dummy in popad
    scp->ebx = regs->ebx;
    scp->edx = regs->edx;
    scp->ecx = regs->ecx;
    scp->eax = regs->eax;
    scp->trapno = regs->int_no;
    scp->err = regs->err_code;
    scp->eip = regs->eip;
    scp->cs = regs->cs;
    scp->eflags = regs->eflags;
    scp->user_esp = regs->useresp;
    scp->user_ss = regs->ss;
    
    // Populate sigframe arguments
    sf.sig = sig;
    
    // Set return address to trampoline
    // TODO: Define a fixed trampoline address or user page
    // For now, let's assume a fixed address provided by the C library crt0
    // or a page mapped by the kernel.
    // Let's rely on a symbol that we might need to export or define.
    // Ideally 0xDEADBEEF for now to catch return if not set up.
    // OR we put the trampoline ON THE STACK (executable stack)
    // The prompt asked to "Set EIP to trampoline".
    // I'll pick a dummy address 0xBAAAAAAD for now, or better:
    // If we support sigreturn, the trampoline should call sigreturn.
    
    sf.retaddr = 0xDEADBEEF; // Placeholder
    
    // Copy frame to user stack
    if (copyout(&sf, (void*)esp, sizeof(sf)) != 0) {
        // Failed to write to stack - kill process?
        kprint("sendsig: Failed to write stack frame\n");
        // force_sig(SIGSEGV)?
        return;
    }
    
    // Update user registers to return to handler
    regs->useresp = esp;
    regs->eip = (uint32_t)handler;
    
    // Reset segment registers if needed (e.g. ds/es/fs/gs to user selectors)
    // Assuming they are already correct from the interrupt
}
