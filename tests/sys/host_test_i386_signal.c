#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include <sys/proc.h>
#include <arch/i386/idt.h>
#include <arch/i386/signal_arch.h>
#include <sys/copy.h>

process_t *current_process;
thread_t *current_thread;

static process_t proc;
static thread_t thread;
static registers_t regs;
static char *user_stack_base;
static size_t user_stack_size;
static int sigexit_called;
static int sigexit_sig;

static void setup_user_stack(void) {
    user_stack_size = 0x4000;
    user_stack_base = mmap((void *)0x20000000, user_stack_size,
                           PROT_READ | PROT_WRITE,
                           MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
    assert(user_stack_base != MAP_FAILED);
}

static void reset_env(void) {
    memset(&proc, 0, sizeof(proc));
    memset(&thread, 0, sizeof(thread));
    memset(&regs, 0, sizeof(regs));
    sigexit_called = 0;
    sigexit_sig = 0;

    current_process = &proc;
    current_thread = &thread;
    thread.syscall_regs = &regs;
    regs.useresp = (uint32_t)(uintptr_t)(user_stack_base + user_stack_size);
    regs.eip = 0x08041234;
    regs.cs = 0x1B;
    regs.ss = 0x23;
    regs.eflags = 0x00000202;
    regs.eax = 0x11111111;
    regs.ebx = 0x22222222;
    regs.ecx = 0x33333333;
    regs.edx = 0x44444444;
    regs.esi = 0x55555555;
    regs.edi = 0x66666666;
    regs.ebp = 0x77777777;
    regs.esp = 0x88888888;
    regs.useresp = (uint32_t)(uintptr_t)(user_stack_base + user_stack_size - 4);
    regs.ds = 0x23;
    regs.es = 0x23;
    regs.fs = 0x23;
    regs.gs = 0x23;
    regs.int_no = 14;
    regs.err_code = 0x5A;
}

int validate_user_addr(const void *addr, size_t size) {
    uintptr_t start = (uintptr_t)addr;
    uintptr_t end = start + size;
    uintptr_t base = (uintptr_t)user_stack_base;
    uintptr_t limit = base + user_stack_size;

    if (start < base || end > limit || end < start) {
        return -1;
    }
    return 0;
}

int copyout(const void *src, void *dst, size_t size) {
    if (validate_user_addr(dst, size) != 0) {
        return -1;
    }
    memcpy(dst, src, size);
    return 0;
}

int copyin(const void *src, void *dst, size_t size) {
    if (validate_user_addr(src, size) != 0) {
        return -1;
    }
    memcpy(dst, src, size);
    return 0;
}

void sigexit(process_t *p, int sig) {
    (void)p;
    sigexit_called = 1;
    sigexit_sig = sig;
}

void kprint(const char *msg) {
    (void)msg;
}

#include "../../sys/arch/i386/signal.c"

static void test_legacy_sendsig_and_sigreturn(void) {
    reset_env();

    registers_t saved = regs;
    sendsig((sig_t)0x08049999, SIGUSR1, 0xA5A5A5A5u, 0, &regs);

    assert(sigexit_called == 0);
    assert(regs.eip == 0x08049999);

    struct sigframe *frame = (struct sigframe *)(uintptr_t)regs.useresp;
    assert(frame->retaddr == SIG_TRAMPOLINE_ADDR);
    assert(frame->sig == SIGUSR1);
    assert(frame->sc.eip == saved.eip);
    assert(frame->sc.user_esp == saved.useresp);
    assert(frame->sc.oldmask == 0xA5A5A5A5u);
    assert((regs.eflags & (1u << 10)) == 0);

    regs.eax = 0;
    regs.ebx = 0;
    regs.ecx = 0;
    regs.edx = 0;
    regs.esi = 0;
    regs.edi = 0;
    regs.ebp = 0;
    regs.eip = 0;
    regs.useresp = 0;
    regs.cs = 0x1B;
    regs.ss = 0x23;
    regs.eflags = 0x00033200;

    thread.sig_mask = 0;
    assert(sys_sigreturn(&frame->sc) == (int)saved.eax);
    assert(regs.eip == saved.eip);
    assert(regs.ebx == saved.ebx);
    assert(regs.ecx == saved.ecx);
    assert(regs.edx == saved.edx);
    assert(regs.esi == saved.esi);
    assert(regs.edi == saved.edi);
    assert(regs.ebp == saved.ebp);
    assert(regs.useresp == saved.useresp);
    assert(thread.sig_mask == 0xA5A5A5A5u);
}

static void test_siginfo_sendsig_and_rt_sigreturn(void) {
    reset_env();

    registers_t saved = regs;
    sendsig((sig_t)0x0804AAAA, SIGUSR2, 0x55AA55AAu, SA_SIGINFO, &regs);

    assert(sigexit_called == 0);
    assert(regs.eip == 0x0804AAAA);

    struct siginfo_frame *frame = (struct siginfo_frame *)(uintptr_t)regs.useresp;
    assert(frame->retaddr == RT_SIG_TRAMPOLINE_ADDR);
    assert(frame->sig == SIGUSR2);
    assert(frame->info_ptr == regs.useresp + offsetof(struct siginfo_frame, info));
    assert(frame->ucontext_ptr == regs.useresp + offsetof(struct siginfo_frame, uc));
    assert(frame->info.si_signo == SIGUSR2);
    assert(frame->uc.uc_sigmask == 0x55AA55AAu);
    assert(frame->uc.uc_mcontext.mc_eip == saved.eip);
    assert(frame->uc.uc_mcontext.mc_esp == saved.useresp);

    regs.eax = 0;
    regs.ebx = 0;
    regs.ecx = 0;
    regs.edx = 0;
    regs.esi = 0;
    regs.edi = 0;
    regs.ebp = 0;
    regs.eip = 0;
    regs.useresp = 0;
    regs.cs = 0x1B;
    regs.ss = 0x23;
    regs.eflags = 0x00033200;

    thread.sig_mask = 0;
    assert(sys_rt_sigreturn(&frame->uc) == (int)saved.eax);
    assert(regs.eip == saved.eip);
    assert(regs.ebx == saved.ebx);
    assert(regs.ecx == saved.ecx);
    assert(regs.edx == saved.edx);
    assert(regs.esi == saved.esi);
    assert(regs.edi == saved.edi);
    assert(regs.ebp == saved.ebp);
    assert(regs.useresp == saved.useresp);
    assert(thread.sig_mask == 0x55AA55AAu);
}

static void test_siginfo_uses_trap_metadata(void) {
    reset_env();

    thread.trap_signo = SIGSEGV;
    thread.trap_code = SEGV_ACCERR;
    thread.trap_addr = 0xDEADBEEFu;

    sendsig((sig_t)0x0804BBBB, SIGSEGV, 0x01020304u, SA_SIGINFO, &regs);

    assert(sigexit_called == 0);
    assert(regs.eip == 0x0804BBBB);

    struct siginfo_frame *frame = (struct siginfo_frame *)(uintptr_t)regs.useresp;
    assert(frame->info.si_signo == SIGSEGV);
    assert(frame->info.si_code == SEGV_ACCERR);
    assert((uintptr_t)frame->info.si_addr == 0xDEADBEEFu);
}

static void test_i386_trap_to_signal_mapping(void) {
    int sig;
    int code;
    uintptr_t addr;

    reset_env();

    regs.int_no = 0;
    assert(i386_trap_to_signal(&regs, 0, &sig, &code, &addr) == 1);
    assert(sig == SIGFPE);
    assert(code == FPE_INTDIV);
    assert(addr == 0);

    regs.int_no = 6;
    regs.eip = 0x08041234;
    assert(i386_trap_to_signal(&regs, 0, &sig, &code, &addr) == 1);
    assert(sig == SIGILL);
    assert(code == ILL_ILLOPC);
    assert(addr == 0x08041234u);

    regs.int_no = 14;
    regs.err_code = 0;
    assert(i386_trap_to_signal(&regs, 0xCAFEBABEu, &sig, &code, &addr) == 1);
    assert(sig == SIGSEGV);
    assert(code == SEGV_MAPERR);
    assert(addr == 0xCAFEBABEu);

    regs.err_code = 1;
    assert(i386_trap_to_signal(&regs, 0xFEEDFACEu, &sig, &code, &addr) == 1);
    assert(sig == SIGSEGV);
    assert(code == SEGV_ACCERR);
    assert(addr == 0xFEEDFACEu);
}

int main(void) {
    setup_user_stack();
    test_legacy_sendsig_and_sigreturn();
    test_siginfo_sendsig_and_rt_sigreturn();
    test_siginfo_uses_trap_metadata();
    test_i386_trap_to_signal_mapping();
    puts("host_test_i386_signal: PASS");
    return 0;
}
