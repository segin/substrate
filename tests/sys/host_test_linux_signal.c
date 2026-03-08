#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include <sys/proc.h>
#include <arch/i386/idt.h>
#include <exec/perso/linux/linux_user.h>
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
static int last_kern_sigaction_sig;
static struct sigaction last_kern_sigaction_act;
static int last_kern_sigprocmask_how;
static uint32_t last_kern_sigprocmask_set;
static int last_sys_kill_pid;
static int last_sys_kill_sig;

static void setup_user_stack(void) {
    user_stack_size = 0x4000;
    user_stack_base = mmap((void *)0x21000000, user_stack_size,
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
    last_kern_sigaction_sig = 0;
    memset(&last_kern_sigaction_act, 0, sizeof(last_kern_sigaction_act));
    last_kern_sigprocmask_how = 0;
    last_kern_sigprocmask_set = 0;
    last_sys_kill_pid = 0;
    last_sys_kill_sig = 0;

    current_process = &proc;
    current_thread = &thread;
    thread.syscall_regs = &regs;
    proc.pid = 42;
    proc.uid = 1000;
    regs.useresp = (uint32_t)(uintptr_t)(user_stack_base + user_stack_size - 4);
    regs.eip = 0x08042222;
    regs.cs = 0x1B;
    regs.ss = 0x23;
    regs.ds = 0x23;
    regs.es = 0x23;
    regs.fs = 0x23;
    regs.gs = 0x23;
    regs.eflags = 0x00000202;
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
    memcpy(dst, src, size);
    return 0;
}

int copyin(const void *src, void *dst, size_t size) {
    memcpy(dst, src, size);
    return 0;
}

void sigexit(process_t *p, int sig) {
    (void)p;
    sigexit_called = 1;
    sigexit_sig = sig;
}

int kern_sigaction(int sig, const struct sigaction *act, struct sigaction *oact) {
    last_kern_sigaction_sig = sig;
    if (oact) {
        *oact = current_process->sig_actions[sig - 1];
    }
    if (act) {
        last_kern_sigaction_act = *act;
        current_process->sig_actions[sig - 1] = *act;
    }
    return 0;
}

int kern_sigprocmask(int how, const uint32_t *set, uint32_t *oset) {
    last_kern_sigprocmask_how = how;
    if (oset) {
        *oset = current_thread->sig_mask;
    }
    if (set) {
        last_kern_sigprocmask_set = *set;
        current_thread->sig_mask = *set;
    }
    return 0;
}

int sys_kill(int pid, int sig) {
    last_sys_kill_pid = pid;
    last_sys_kill_sig = sig;
    return 0;
}

#define HOST_TEST 1
#include "../../sys/exec/perso/perso_linux.c"
#include "../../sys/exec/perso/linux/linux_sig.c"

static void test_linux_rt_frame_uses_trap_siginfo(void) {
    reset_env();

    thread.trap_signo = SIGSEGV;
    thread.trap_code = SEGV_ACCERR;
    thread.trap_addr = 0xBAD00BADu;

    linux_sendsig((void *)0x08043333, SIGSEGV, 0xA5A5A5A5u, SA_SIGINFO, &regs);

    assert(sigexit_called == 0);
    assert(regs.eip == 0x08043333);

    struct linux_rt_sigframe *frame =
        (struct linux_rt_sigframe *)(uintptr_t)regs.useresp;
    assert(frame->sig == SIGSEGV);
    assert(frame->info.si_signo == SIGSEGV);
    assert(frame->info.si_code == SEGV_ACCERR);
    assert((uintptr_t)frame->info._sifields._sigfault._addr == 0xBAD00BADu);
    assert(frame->uc.uc_sigmask.sig[0] == 0xA5A5A5A5u);
}

static void test_linux_rt_sigaction_translates_mask_and_restorer(void) {
    struct linux_sigaction act;
    struct linux_sigaction old;

    reset_env();

    current_process->sig_actions[SIGSEGV - 1].sa_handler = (sig_t)0x08041111u;
    current_process->sig_actions[SIGSEGV - 1].sa_flags = SA_RESTART;
    current_process->sig_actions[SIGSEGV - 1].sa_mask = sigmask(SIGUSR1);
    current_process->linux_sig_restorer[SIGSEGV - 1] = (void *)0x0804AAAAu;

    memset(&act, 0, sizeof(act));
    act.sa_handler = 0x08042222u;
    act.sa_flags = SA_SIGINFO | SA_RESTART | LINUX_SA_RESTORER;
    act.sa_restorer = 0x0804BBBBu;
    act.sa_mask.sig[0] = (1U << (SIGUSR2 - 1));

    assert(linux_sys_rt_sigaction(SIGSEGV, &act, &old, sizeof(linux_sigset_t)) == 0);
    assert(last_kern_sigaction_sig == SIGSEGV);
    assert((uintptr_t)last_kern_sigaction_act.sa_handler == 0x08042222u);
    assert((last_kern_sigaction_act.sa_flags & (SA_SIGINFO | SA_RESTART)) ==
           (SA_SIGINFO | SA_RESTART));
    assert(last_kern_sigaction_act.sa_mask == sigmask(SIGUSR2));
    assert((uintptr_t)current_process->linux_sig_restorer[SIGSEGV - 1] == 0x0804BBBBu);

    assert(old.sa_handler == 0x08041111u);
    assert(old.sa_flags == (SA_RESTART | LINUX_SA_RESTORER));
    assert(old.sa_restorer == 0x0804AAAAu);
    assert(old.sa_mask.sig[0] == sigmask(SIGUSR1));
}

static void test_linux_rt_sigprocmask_translates_sets(void) {
    linux_sigset_t set;
    linux_sigset_t old;

    reset_env();
    current_thread->sig_mask = sigmask(SIGUSR1) | sigmask(SIGSEGV);

    memset(&set, 0, sizeof(set));
    set.sig[0] = sigmask(SIGUSR2) | sigmask(SIGCHLD);

    assert(linux_sys_rt_sigprocmask(3, &set, &old, sizeof(linux_sigset_t)) == 0);
    assert(last_kern_sigprocmask_how == 3);
    assert(last_kern_sigprocmask_set == (sigmask(SIGUSR2) | sigmask(SIGCHLD)));
    assert(old.sig[0] == (sigmask(SIGUSR1) | sigmask(SIGSEGV)));
}

static void test_linux_kill_translates_signal_numbers(void) {
    reset_env();
    assert(linux_sys_kill(123, SIGSEGV) == 0);
    assert(last_sys_kill_pid == 123);
    assert(last_sys_kill_sig == SIGSEGV);

    assert(linux_sys_kill(123, 0) == 0);
    assert(last_sys_kill_sig == 0);
}

static void test_linux_sendsig_prefers_user_restorer(void) {
    reset_env();
    current_process->linux_sig_restorer[SIGUSR1 - 1] = (void *)0x0804CCCCu;

    linux_sendsig((void *)0x08043333, SIGUSR1, 0, 0, &regs);

    assert(sigexit_called == 0);
    assert(regs.eip == 0x08043333);

    struct linux_sigframe *frame =
        (struct linux_sigframe *)(uintptr_t)regs.useresp;
    assert(frame->pretcode == 0x0804CCCCu);
}

int main(void) {
    setup_user_stack();
    test_linux_rt_frame_uses_trap_siginfo();
    test_linux_rt_sigaction_translates_mask_and_restorer();
    test_linux_rt_sigprocmask_translates_sets();
    test_linux_kill_translates_signal_numbers();
    test_linux_sendsig_prefers_user_restorer();
    puts("host_test_linux_signal: PASS");
    return 0;
}
