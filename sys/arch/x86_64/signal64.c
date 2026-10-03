/*
 * signal64.c - signal delivery and return for native 64-bit processes
 *
 * The amd64 signal ABI of docs/specs/abi-amd64.md, section 7: a struct
 * sigframe below the interrupted stack (clear of the 128-byte red zone),
 * the thread resumed in the 64-bit trampoline, and sigreturn(&sf_uc)
 * putting the saved context back.  32-bit processes keep the i386 frames
 * of arch/i386/signal.c, which hands 64-bit frames to sendsig_amd64().
 */

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <machine/fpu.h>
#include <machine/gdt.h>
#include <machine/idt.h>
#include <machine/signal_arch.h>
#include <machine/vmparam.h>
#include <kern/sched.h>
#include <sys/amd64_abi.h>
#include <sys/copy.h>
#include <sys/errno.h>
#include <sys/proc.h>
#include <sys/signal.h>

#define AMD64_REDZONE   128

/* RFLAGS bits a context may set: the arithmetic flags, TF, DF, and AC. */
#define AMD64_RFLAGS_USER   0x00040DD5UL

void sendsig_amd64(void *handler, int sig, uint32_t mask, uint32_t flags,
                   void *regs_ptr) {
    registers_t *regs = regs_ptr;
    struct amd64_sigframe sf;
    struct amd64_mcontext *mc = &sf.sf_uc.uc_mcontext;
    uint64_t sp;
    int onstack = 0;

    /* The alternate stack, under the same conditions as for i386. */
    if ((flags & SA_ONSTACK) &&
        current_thread->sig_alt_stack.ss_sp != NULL &&
        current_thread->sig_alt_stack.ss_size > 0 &&
        (current_thread->sig_alt_stack.ss_flags & SS_DISABLE) == 0 &&
        !current_thread->sig_on_stack) {
        sp = (uint64_t)(uintptr_t)current_thread->sig_alt_stack.ss_sp +
             current_thread->sig_alt_stack.ss_size;
        onstack = 1;
    } else {
        sp = regs->rsp - AMD64_REDZONE;
    }
    sp = (sp - sizeof(sf)) & ~15UL;

    if (validate_user_addr((void *)(uintptr_t)sp, sizeof(sf)) != 0) {
        sigexit(current_process, SIGSEGV);
        return;
    }

    memset(&sf, 0, sizeof(sf));
    sf.sf_ahu = (uint64_t)(uintptr_t)handler;

    sf.sf_uc.uc_sigmask.bits[0] = mask;
    stack_to_amd64(&current_thread->sig_alt_stack, &sf.sf_uc.uc_stack);

    mc->mc_onstack = current_thread->sig_on_stack ? 1 : 0;
    mc->mc_rdi = regs->rdi;
    mc->mc_rsi = regs->rsi;
    mc->mc_rdx = regs->rdx;
    mc->mc_rcx = regs->rcx;
    mc->mc_r8  = regs->r8;
    mc->mc_r9  = regs->r9;
    mc->mc_rax = regs->rax;
    mc->mc_rbx = regs->rbx;
    mc->mc_rbp = regs->rbp;
    mc->mc_r10 = regs->r10;
    mc->mc_r11 = regs->r11;
    mc->mc_r12 = regs->r12;
    mc->mc_r13 = regs->r13;
    mc->mc_r14 = regs->r14;
    mc->mc_r15 = regs->r15;
    mc->mc_trapno = (uint32_t)regs->int_no;
    mc->mc_fs = (uint16_t)regs->fs;
    mc->mc_gs = (uint16_t)regs->gs;
    mc->mc_es = (uint16_t)regs->es;
    mc->mc_ds = (uint16_t)regs->ds;
    mc->mc_err = regs->err_code;
    mc->mc_rip = regs->rip;
    mc->mc_cs = regs->cs;
    mc->mc_rflags = regs->rflags;
    mc->mc_rsp = regs->rsp;
    mc->mc_ss = regs->ss;
    mc->mc_len = sizeof(*mc);
    if (current_thread->trap_signo == sig)
        mc->mc_addr = current_thread->trap_addr;
    if (fpu_signal_save(mc->mc_fpstate)) {
        mc->mc_fpformat = AMD64_MC_FPFMT_XMM;
        mc->mc_ownedfp = AMD64_MC_FPOWNED_FPU;
    } else {
        mc->mc_fpformat = AMD64_MC_FPFMT_NODEV;
        mc->mc_ownedfp = AMD64_MC_FPOWNED_NONE;
    }

    siginfo_t ksi;
    int code = (current_thread->trap_signo == sig) ? current_thread->trap_code
                                                    : SI_USER;
    populate_siginfo(&ksi, sig, code);
    siginfo_to_amd64(&ksi, &sf.sf_si);

    if (copyout(&sf, (void *)(uintptr_t)sp, sizeof(sf)) != 0) {
        sigexit(current_process, SIGSEGV);
        return;
    }

    if (onstack) {
        current_thread->sig_on_stack = 1;
        current_thread->sig_alt_stack.ss_flags |= SS_ONSTACK;
    }

    regs->rsp = sp;
    regs->rip = AMD64_SIG_TRAMPOLINE_ADDR;
    regs->rdi = (uint64_t)sig;
    if (flags & SA_SIGINFO) {
        regs->rsi = sp + offsetof(struct amd64_sigframe, sf_si);
    } else {
        regs->rsi = (uint64_t)(int64_t)code;
        regs->rcx = mc->mc_addr;
    }
    regs->rdx = sp + offsetof(struct amd64_sigframe, sf_uc);
    regs->rflags &= ~(0x400UL | 0x100UL);       /* DF, TF */
    regs->cs = SEL_UCODE_RPL3;
    regs->ss = SEL_UDATA_RPL3;
}

/*
 * sigreturn(const ucontext_t *): put back the context a signal frame
 * saved.  The context comes from user memory, so only what a process may
 * choose for itself is taken from it.
 */
int amd64_sys_sigreturn(void *ucp) {
    struct amd64_ucontext uc;
    struct amd64_mcontext *mc = &uc.uc_mcontext;

    if (!current_thread || !current_thread->syscall_regs)
        return -EINVAL;
    registers_t *regs = current_thread->syscall_regs;

    if (copyin(ucp, &uc, sizeof(uc)) != 0)
        return -EFAULT;
    if (mc->mc_len != sizeof(*mc) ||
        mc->mc_cs != SEL_UCODE_RPL3 || mc->mc_ss != SEL_UDATA_RPL3 ||
        mc->mc_rip >= USER32_VA_END || mc->mc_rsp >= USER32_VA_END)
        return -EINVAL;

    regs->rdi = mc->mc_rdi;
    regs->rsi = mc->mc_rsi;
    regs->rdx = mc->mc_rdx;
    regs->rcx = mc->mc_rcx;
    regs->r8  = mc->mc_r8;
    regs->r9  = mc->mc_r9;
    regs->rax = mc->mc_rax;
    regs->rbx = mc->mc_rbx;
    regs->rbp = mc->mc_rbp;
    regs->r10 = mc->mc_r10;
    regs->r11 = mc->mc_r11;
    regs->r12 = mc->mc_r12;
    regs->r13 = mc->mc_r13;
    regs->r14 = mc->mc_r14;
    regs->r15 = mc->mc_r15;
    regs->rip = mc->mc_rip;
    regs->rsp = mc->mc_rsp;
    regs->cs = SEL_UCODE_RPL3;
    regs->ss = SEL_UDATA_RPL3;
    regs->rflags = (regs->rflags & ~AMD64_RFLAGS_USER) |
                   (mc->mc_rflags & AMD64_RFLAGS_USER);

    if (mc->mc_fpformat == AMD64_MC_FPFMT_XMM &&
        mc->mc_ownedfp == AMD64_MC_FPOWNED_FPU)
        fpu_signal_restore(mc->mc_fpstate);

    current_thread->sig_mask = uc.uc_sigmask.bits[0];
    current_thread->sig_on_stack = 0;
    current_thread->sig_alt_stack.ss_flags &= ~SS_ONSTACK;

    /* The frame is a restored context now, not a system call's return. */
    current_thread->frame_replaced = 1;
    return 0;
}
