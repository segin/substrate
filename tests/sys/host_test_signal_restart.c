#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <sys/signal.h>
#include <sys/proc.h>
#include <sys/session.h>
#include <arch/i386/idt.h>
#include <pm/pm.h>
#include <kern/sched.h>
#include <exec/perso/personality.h>

thread_t threads[MAX_THREADS];
process_t processes[MAX_PROCS];
process_t *current_process;
thread_t *current_thread;
mutex_t proctree_lock;

static struct personality test_personality;
static int sendsig_calls;
static void *last_handler;
static int last_sig;
static uint32_t last_mask;
static uint32_t last_flags;
static registers_t *last_regs;

void mutex_init(mutex_t *m, const char *name) { (void)m; (void)name; }
void mutex_lock(mutex_t *m) { (void)m; }
void mutex_unlock(mutex_t *m) { (void)m; }
void sched_yield(void) {}
void sched_sleep(void *chan) { (void)chan; }
int sleepq_remove_thread(thread_t *t) { (void)t; return 0; }
void kprint(const char *msg) { (void)msg; }
void panic(const char *msg) { (void)msg; assert(!"panic"); }
uint64_t get_ticks(void) { return 0; }
uint32_t get_hz(void) { return 128; }
int copyin(const void *src, void *dst, size_t size) { memcpy(dst, src, size); return 0; }
int copyout(const void *src, void *dst, size_t size) { memcpy(dst, src, size); return 0; }
const uint8_t sigprop[NSIG] = {0};
void proc_exit(int status) { (void)status; assert(!"proc_exit should not be called"); }
void sched_wakeup(void *chan) { (void)chan; }
struct pgrp *pgrp_find(int pgid) { (void)pgid; return NULL; }
void pgrp_signal(struct pgrp *pgrp, int sig) { (void)pgrp; (void)sig; }
int pgrp_is_orphaned(struct pgrp *pgrp) { (void)pgrp; return 0; }
int coredump(process_t *p) { (void)p; return 0; }

struct personality *perso_lookup(int id) {
    return (id == (int)test_personality.id) ? &test_personality : NULL;
}

const char *perso_name(int id) {
    return (id == (int)test_personality.id) ? test_personality.name : "unknown";
}

void sendsig(sig_t handler, int sig, uint32_t mask, uint32_t flags, registers_t *regs) {
    sendsig_calls++;
    last_handler = (void *)handler;
    last_sig = sig;
    last_mask = mask;
    last_flags = flags;
    last_regs = regs;
}

#include "../../sys/kern/signal.c"

static void reset_env(void) {
    memset(threads, 0, sizeof(threads));
    memset(processes, 0, sizeof(processes));
    memset(&test_personality, 0, sizeof(test_personality));
    sendsig_calls = 0;
    last_handler = NULL;
    last_sig = 0;
    last_mask = 0;
    last_flags = 0;
    last_regs = NULL;

    current_process = &processes[0];
    current_thread = &threads[0];
    current_thread->proc = current_process;
    current_thread->state = THREAD_RUNNING;
    current_process->pid = 42;
    current_process->state = SRUN;
    current_process->perso_id = PERS_NATIVE;

    test_personality.name = "substrate-test";
    test_personality.id = PERS_NATIVE;
    test_personality.sendsig = (void (*)(void *, int, uint32_t, uint32_t, void *))sendsig;
}

static void test_sa_restart_rewinds_interrupted_syscall(void) {
    registers_t regs;

    reset_env();
    memset(&regs, 0, sizeof(regs));
    regs.eax = (uint32_t)-EINTR;
    regs.eip = 0x08049000;

    current_thread->in_syscall = 1;
    current_thread->syscall_orig_eax = 123;
    current_thread->sig_mask = sigmask(SIGTERM);
    current_thread->sig_pending = sigmask(SIGUSR1);
    current_process->sig_actions[SIGUSR1 - 1].sa_handler = (sig_t)0xDEADBEEF;
    current_process->sig_actions[SIGUSR1 - 1].sa_flags = SA_RESTART;
    current_process->sig_actions[SIGUSR1 - 1].sa_mask = sigmask(SIGINT);

    signal_handle_pending(&regs);

    assert(sendsig_calls == 1);
    assert(last_handler == (void *)0xDEADBEEF);
    assert(last_sig == SIGUSR1);
    assert(last_mask == sigmask(SIGTERM));
    assert(last_flags == SA_RESTART);
    assert(last_regs == &regs);
    assert(regs.eax == 123);
    assert(regs.eip == 0x08048FFE);
    assert(current_thread->sig_mask == (sigmask(SIGTERM) | sigmask(SIGINT) | sigmask(SIGUSR1)));
    assert((current_thread->sig_pending & sigmask(SIGUSR1)) == 0);
}

static void test_no_restart_leaves_eintr_result_intact(void) {
    registers_t regs;

    reset_env();
    memset(&regs, 0, sizeof(regs));
    regs.eax = (uint32_t)-EINTR;
    regs.eip = 0x0804A000;

    current_thread->in_syscall = 1;
    current_thread->syscall_orig_eax = 321;
    current_thread->sig_mask = 0;
    current_thread->sig_pending = sigmask(SIGUSR2);
    current_process->sig_actions[SIGUSR2 - 1].sa_handler = (sig_t)0xCAFEBABE;
    current_process->sig_actions[SIGUSR2 - 1].sa_flags = 0;
    current_process->sig_actions[SIGUSR2 - 1].sa_mask = sigmask(SIGCHLD);

    signal_handle_pending(&regs);

    assert(sendsig_calls == 1);
    assert(last_handler == (void *)0xCAFEBABE);
    assert(last_sig == SIGUSR2);
    assert(last_mask == 0);
    assert(regs.eax == (uint32_t)-EINTR);
    assert(regs.eip == 0x0804A000);
    assert(current_thread->sig_mask == (sigmask(SIGUSR2) | sigmask(SIGCHLD)));
}

static void test_reseethand_resets_handler_after_delivery(void) {
    registers_t regs;

    reset_env();
    memset(&regs, 0, sizeof(regs));
    regs.eax = 0;
    regs.eip = 0x0804B000;

    current_thread->sig_pending = sigmask(SIGUSR1);
    current_process->sig_actions[SIGUSR1 - 1].sa_handler = (sig_t)0xDEADC0DE;
    current_process->sig_actions[SIGUSR1 - 1].sa_flags = SA_RESETHAND;
    current_process->sig_catch = sigmask(SIGUSR1);

    signal_handle_pending(&regs);

    assert(sendsig_calls == 1);
    assert(current_process->sig_actions[SIGUSR1 - 1].sa_handler == SIG_DFL);
    assert((current_process->sig_catch & sigmask(SIGUSR1)) == 0);
}

static void test_nodefer_does_not_self_block_signal(void) {
    registers_t regs;

    reset_env();
    memset(&regs, 0, sizeof(regs));
    regs.eax = 0;
    regs.eip = 0x0804C000;

    current_thread->sig_mask = sigmask(SIGTERM);
    current_thread->sig_pending = sigmask(SIGUSR2);
    current_process->sig_actions[SIGUSR2 - 1].sa_handler = (sig_t)0xCAFED00D;
    current_process->sig_actions[SIGUSR2 - 1].sa_flags = SA_NODEFER;
    current_process->sig_actions[SIGUSR2 - 1].sa_mask = sigmask(SIGCHLD);

    signal_handle_pending(&regs);

    assert(sendsig_calls == 1);
    assert(current_thread->sig_mask == (sigmask(SIGTERM) | sigmask(SIGCHLD)));
    assert((current_thread->sig_mask & sigmask(SIGUSR2)) == 0);
}

int main(void) {
    test_sa_restart_rewinds_interrupted_syscall();
    test_no_restart_leaves_eintr_result_intact();
    test_reseethand_resets_handler_after_delivery();
    test_nodefer_does_not_self_block_signal();
    puts("host_test_signal_restart: PASS");
    return 0;
}
