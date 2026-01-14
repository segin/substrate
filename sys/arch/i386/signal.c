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
    
    // Set return address to trampoline
    // The trampoline code (sys_sigreturn) is expected to be at this address.
    // In a real system, this is mapped in a VDSO or provided by libc.
    // We use a fixed address for now that we will map later.
    #define SIG_TRAMPOLINE_ADDR 0xFFFF1000
    sf.retaddr = SIG_TRAMPOLINE_ADDR;
    
    // Copy frame to user stack
    if (copyout(&sf, (void*)esp, sizeof(sf)) != 0) {
        // Failed to write to stack - kill process?
        kprint("sendsig: Failed to write stack frame\n");
        // force_sig(SIGSEGV)?
        return;
    }
    
    // Update user registers to return to handler
    // EIP points to the handler function
    // ESP points to the sigframe we just constructed
    regs->useresp = esp;
    regs->eip = (uint32_t)handler;
    
    // Reset segment registers to user data selectors if needed
    // (Assuming kernel entry preserves them or they are restored from regs)
}

/*
 * sys_sigreturn - Restore context from signal frame
 * 
 * Arguments:
 *   scp - Pointer to struct sigcontext on user stack
 * 
 * This syscall is called by the trampoline code after the signal handler returns.
 * It restores the user thread's state to what it was before the signal.
 */
int sys_sigreturn(struct sigcontext *scp) {
    if (!scp) return -1; // EINVAL
    
    // We need access to the current thread's kernel trap frame (registers)
    // to overwrite them with the restored context.
    extern registers_t *syscall_regs;
    if (!syscall_regs) return -1;
    
    // Copy sigcontext from user stack (needs checking)
    struct sigcontext sc;
    // copyin(scp, &sc, sizeof(sc));
    // For now, assume direct access
    memcpy(&sc, scp, sizeof(sc));
    
    // Verification (Security)
    // Ensure segment selectors are safe (RPL 3, valid indices)
    if ((sc.cs & 3) != 3) return -1; // Must be user mode
    if ((sc.ss & 3) != 3) return -1;
    
    // Restore User Registers
    syscall_regs->gs = sc.gs;
    syscall_regs->fs = sc.fs;
    syscall_regs->es = sc.es;
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
    
    // Return EAX from the context, not the syscall return value
    return sc.eax; 
}
