#include <sys/signal.h>
#include "../../../include/sys/proc.h"
#include "linux_user.h"
#include "../../../arch/i386/idt.h"
#include <string.h>
#include <stddef.h>
#include <stdint.h>

/* These should be exported from sys/arch/i386/signal.c or shared in a header */
extern int copyout(const void *src, void *dst, size_t size);
extern int copyin(const void *src, void *dst, size_t size);
extern int validate_user_addr(const void *addr, size_t size);

static void populate_linux_siginfo(linux_siginfo_t *info, int lsig) {
    memset(info, 0, sizeof(*info));
    info->si_signo = lsig;
    info->si_errno = 0;
    info->si_code = 0; /* SI_USER */

    if (current_process) {
        info->_sifields._kill._pid = current_process->pid;
        info->_sifields._kill._uid = current_process->uid;
    }
}

void linux_sendsig(void *handler, int sig, uint32_t mask, uint32_t flags, void *regs_ptr) {
    registers_t *regs = (registers_t *)regs_ptr;
    uint32_t esp = regs->useresp;
    int lsig = native_to_linux_signal(sig);

    if (flags & SA_SIGINFO) {
        /* rt_sigframe */
        struct linux_rt_sigframe frame;
        memset(&frame, 0, sizeof(frame));

        esp -= sizeof(struct linux_rt_sigframe);
        esp &= ~0xFUL;

        if (validate_user_addr((void*)(uintptr_t)esp, sizeof(frame)) != 0) {
            sigexit(current_process, SIGSEGV);
            return;
        }

        frame.sig = lsig;
        frame.pinfo = esp + offsetof(struct linux_rt_sigframe, info);
        frame.puc = esp + offsetof(struct linux_rt_sigframe, uc);
        
        /* info and uc populated here */
        populate_linux_siginfo(&frame.info, lsig);

        frame.uc.uc_flags = 0;
        frame.uc.uc_link = 0;
        if (current_thread) {
            frame.uc.uc_stack.ss_sp = (uint32_t)current_thread->sig_alt_stack.ss_sp;
            frame.uc.uc_stack.ss_size = current_thread->sig_alt_stack.ss_size;
            frame.uc.uc_stack.ss_flags = current_thread->sig_alt_stack.ss_flags;
        }

        frame.uc.uc_sigmask.sig[0] = mask;
        frame.uc.uc_mcontext.eip = regs->eip;
        frame.uc.uc_mcontext.eax = regs->eax;
        frame.uc.uc_mcontext.ebx = regs->ebx;
        frame.uc.uc_mcontext.ecx = regs->ecx;
        frame.uc.uc_mcontext.edx = regs->edx;
        frame.uc.uc_mcontext.edi = regs->edi;
        frame.uc.uc_mcontext.esi = regs->esi;
        frame.uc.uc_mcontext.ebp = regs->ebp;
        frame.uc.uc_mcontext.esp = regs->useresp;
        frame.uc.uc_mcontext.eflags = regs->eflags;
        frame.uc.uc_mcontext.cs = regs->cs;
        frame.uc.uc_mcontext.ss = regs->ss;
        frame.uc.uc_mcontext.ds = regs->ds;
        frame.uc.uc_mcontext.es = regs->es;
        frame.uc.uc_mcontext.fs = regs->fs;
        frame.uc.uc_mcontext.gs = regs->gs;
        frame.uc.uc_mcontext.oldmask = mask;

        /* Linux expects a return trampoline */
        /* For now we use the kernel-mapped trampoline if it matches Linux ABI */
        frame.pretcode = 0xFFFF1010; // RT_SIG_TRAMPOLINE_ADDR

        if (copyout(&frame, (void*)(uintptr_t)esp, sizeof(frame)) != 0) {
            sigexit(current_process, SIGSEGV);
            return;
        }

        regs->useresp = esp;
        regs->eip = (uint32_t)handler;
    } else {
        /* traditional sigframe */
        struct linux_sigframe frame;
        memset(&frame, 0, sizeof(frame));

        esp -= sizeof(struct linux_sigframe);
        esp &= ~0xFUL;

        if (validate_user_addr((void*)(uintptr_t)esp, sizeof(frame)) != 0) {
            sigexit(current_process, SIGSEGV);
            return;
        }

        frame.sig = lsig;
        frame.sc.eip = regs->eip;
        frame.sc.eax = regs->eax;
        frame.sc.ebx = regs->ebx;
        frame.sc.ecx = regs->ecx;
        frame.sc.edx = regs->edx;
        frame.sc.edi = regs->edi;
        frame.sc.esi = regs->esi;
        frame.sc.ebp = regs->ebp;
        frame.sc.esp = regs->useresp;
        frame.sc.eflags = regs->eflags;
        frame.sc.cs = regs->cs;
        frame.sc.ss = regs->ss;
        frame.sc.ds = regs->ds;
        frame.sc.es = regs->es;
        frame.sc.fs = regs->fs;
        frame.sc.gs = regs->gs;
        frame.sc.oldmask = mask;

        frame.pretcode = 0xFFFF1000; // SIG_TRAMPOLINE_ADDR

        if (copyout(&frame, (void*)(uintptr_t)esp, sizeof(frame)) != 0) {
            sigexit(current_process, SIGSEGV);
            return;
        }

        regs->useresp = esp;
        regs->eip = (uint32_t)handler;
    }
}

int linux_sys_sigreturn(void *regs_ptr) {
    registers_t *regs = (registers_t *)regs_ptr;
    struct linux_sigframe *frame = (struct linux_sigframe *)(regs->useresp - 8);
    struct linux_sigcontext sc;

    if (copyin(&frame->sc, &sc, sizeof(sc)) != 0) return -1;

    regs->eip = sc.eip;
    regs->eax = sc.eax;
    regs->ebx = sc.ebx;
    regs->ecx = sc.ecx;
    regs->edx = sc.edx;
    regs->edi = sc.edi;
    regs->esi = sc.esi;
    regs->ebp = sc.ebp;
    regs->useresp = sc.esp;
    regs->eflags = sc.eflags;
    regs->cs = sc.cs | 3;
    regs->ss = sc.ss | 3;
    regs->ds = sc.ds | 3;
    regs->es = sc.es | 3;
    regs->fs = sc.fs | 3;
    regs->gs = sc.gs | 3;

    current_thread->sig_mask = sc.oldmask;
    return regs->eax;
}

int linux_sys_rt_sigreturn(void *regs_ptr) {
    registers_t *regs = (registers_t *)regs_ptr;
    struct linux_rt_sigframe *frame = (struct linux_rt_sigframe *)(regs->useresp - 4);
    struct linux_ucontext uc;

    if (copyin(&frame->uc, &uc, sizeof(uc)) != 0) return -1;

    regs->eip = uc.uc_mcontext.eip;
    regs->eax = uc.uc_mcontext.eax;
    regs->ebx = uc.uc_mcontext.ebx;
    regs->ecx = uc.uc_mcontext.ecx;
    regs->edx = uc.uc_mcontext.edx;
    regs->edi = uc.uc_mcontext.edi;
    regs->esi = uc.uc_mcontext.esi;
    regs->ebp = uc.uc_mcontext.ebp;
    regs->useresp = uc.uc_mcontext.esp;
    regs->eflags = uc.uc_mcontext.eflags;
    regs->cs = uc.uc_mcontext.cs | 3;
    regs->ss = uc.uc_mcontext.ss | 3;
    regs->ds = uc.uc_mcontext.ds | 3;
    regs->es = uc.uc_mcontext.es | 3;
    regs->fs = uc.uc_mcontext.fs | 3;
    regs->gs = uc.uc_mcontext.gs | 3;

    current_thread->sig_mask = uc.uc_sigmask.sig[0];
    return regs->eax;
}
