/*
 * sys/arch/i386/signal.c - Architecture-specific signal handling
 */

#include <sys/signal.h>
#include <sys/proc.h>
#include "../../kern/console.h"
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
 * This function prepares the user stack frame for the signal handler.
 * It pushes the context, signal number, and return address.
 */
/*
 * This function prepares the user stack frame for the signal handler.
 * It pushes the context, signal number, and return address.
 */
void sendsig(sig_t handler, int sig, uint32_t mask, uint32_t flags, registers_t *regs) {
    if (!regs) return;

    // TODO: Handle SA_SIGINFO (extended frame)
    if (flags & SA_SIGINFO) {
        kprint("sendsig: SA_SIGINFO requested but not fully implemented (using legacy frame)\n");
    }

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
    scp = &sf.sc;
    
    scp->gs = regs->gs;
    // scp->fs = regs->fs; // registers_t lacks fs
    // scp->es = regs->es; // registers_t lacks es
    scp->fs = 0; // Default/Safe value
    scp->es = 0; // Default/Safe value (OS uses DS mostly?)
    scp->ds = regs->ds;
    scp->edi = regs->edi;
    scp->esi = regs->esi;
    scp->ebp = regs->ebp;
    scp->esp = regs->esp; 
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
    scp->oldmask = mask; // Save signal mask
    
    // Populate sigframe arguments
    sf.sig = sig;
    
    // Set return address to trampoline
    #define SIG_TRAMPOLINE_ADDR 0xFFFF1000
    sf.retaddr = SIG_TRAMPOLINE_ADDR;
    
    // Copy frame to user stack
    if (copyout(&sf, (void*)esp, sizeof(sf)) != 0) {
        kprint("sendsig: Failed to write stack frame\n");
        return;
    }
    
    // Update user registers to return to handler
    regs->useresp = esp;
    regs->eip = (uint32_t)handler;
}

/*
 * sys_sigreturn - Restore context from signal frame
 */
int sys_sigreturn(struct sigcontext *scp) {
    if (!scp) return -1; // EINVAL
    
    extern registers_t *syscall_regs;
    if (!syscall_regs) return -1;
    
    struct sigcontext sc;
    memcpy(&sc, scp, sizeof(sc));
    
    // Verification (Security)
    if ((sc.cs & 3) != 3) return -1; // Must be user mode
    if ((sc.user_ss & 3) != 3) return -1;
    
    // Restore User Registers
    syscall_regs->gs = sc.gs;
    // syscall_regs->fs = sc.fs; // registers_t lacks fs
    // syscall_regs->es = sc.es; // registers_t lacks es
    syscall_regs->ds = sc.ds;
    syscall_regs->edi = sc.edi;
    syscall_regs->esi = sc.esi;
    syscall_regs->ebp = sc.ebp;
    // esp ignored
    syscall_regs->ebx = sc.ebx;
    syscall_regs->edx = sc.edx;
    syscall_regs->ecx = sc.ecx;
    syscall_regs->eax = sc.eax;
    syscall_regs->eip = sc.eip;
    syscall_regs->cs = sc.cs;
    syscall_regs->eflags = sc.eflags;
    syscall_regs->useresp = sc.user_esp;
    syscall_regs->ss = sc.user_ss;
    
    // Restore signal mask
    if (current_thread) {
        current_thread->sig_mask = sc.oldmask;
    }
    
    // Return EAX from the context, not the syscall return value
    return sc.eax; 
}
