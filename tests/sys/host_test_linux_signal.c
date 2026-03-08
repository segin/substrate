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

int native_to_linux_signal(int sig) {
    return sig;
}

int linux_to_native_signal(int sig) {
    return sig;
}

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

int main(void) {
    setup_user_stack();
    test_linux_rt_frame_uses_trap_siginfo();
    puts("host_test_linux_signal: PASS");
    return 0;
}
