#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <pm/pm.h>
#include <sys/tty.h>
#include <sys/session.h>
#include <sys/signal.h>
#include <sys/copy.h>

#define _ARCH_I386_INTR_H
process_t processes[MAX_PROCS];
process_t *current_process;
thread_t *current_thread;
mutex_t proctree_lock;

static int signal_count;
static int signal_pgrp[8];
static int signal_sig[8];
static process_t *last_psignal_proc;
static int last_psignal_sig;

uint32_t intr_disable(void) { return 0; }
void intr_restore(uint32_t flags) { (void)flags; }
void spinlock_init(spinlock_t *lock, const char *name) { (void)lock; (void)name; }
void spinlock_acquire(spinlock_t *lock) { (void)lock; }
void spinlock_release(spinlock_t *lock) { (void)lock; }
void *kmalloc(size_t size) { return calloc(1, size); }
void kfree(void *ptr, size_t size) { (void)size; free(ptr); }
int copyin(const void *src, void *dst, size_t size) { memcpy(dst, src, size); return 0; }
int copyout(const void *src, void *dst, size_t size) { memcpy(dst, src, size); return 0; }
int copyinstr(const void *src, void *dst, size_t maxlen, size_t *len) {
    size_t n = strnlen((const char *)src, maxlen);
    if (n == maxlen) return -1;
    memcpy(dst, src, n + 1);
    if (len) *len = n + 1;
    return 0;
}
void devfs_register_device(fs_node_t *node) { (void)node; }
void kprint(const char *fmt, ...) { (void)fmt; }
void sched_wakeup(void *chan) { (void)chan; }
void sched_yield(void) {}
int signal_send_group(int pgrp, int sig) {
    if (signal_count < 8) {
        signal_pgrp[signal_count] = pgrp;
        signal_sig[signal_count] = sig;
    }
    signal_count++;
    return 0;
}
void psignal(process_t *proc, int sig) {
    last_psignal_proc = proc;
    last_psignal_sig = sig;
}

#include "../../sys/drivers/console/tty.c"

static void reset_env(void) {
    memset(processes, 0, sizeof(processes));
    for (int i = 0; i < MAX_PROCS; i++) {
        processes[i].pid = -1;
    }
    current_process = NULL;
    current_thread = NULL;
    signal_count = 0;
    memset(signal_pgrp, 0, sizeof(signal_pgrp));
    memset(signal_sig, 0, sizeof(signal_sig));
    last_psignal_proc = NULL;
    last_psignal_sig = 0;
}

static process_t *init_proc(int slot, int pid) {
    process_t *p = &processes[slot];
    memset(p, 0, sizeof(*p));
    p->pid = pid;
    return p;
}

static void init_session_leader(process_t *proc, struct pgrp *pgrp, struct session *sess, int pgid) {
    memset(pgrp, 0, sizeof(*pgrp));
    memset(sess, 0, sizeof(*sess));
    sess->s_sid = proc->pid;
    sess->s_leader = proc;
    pgrp->pg_id = pgid;
    pgrp->pg_session = sess;
    pgrp->pg_members = proc;
    proc->p_pgrp = pgrp;
}

static void test_tiocsctty_assigns_owner(void) {
    reset_env();

    process_t *proc = init_proc(0, 42);
    struct pgrp pgrp;
    struct session sess;
    struct tty tty;

    memset(&tty, 0, sizeof(tty));
    init_session_leader(proc, &pgrp, &sess, 42);
    current_process = proc;

    assert(tty_ioctl_kern(&tty, TIOCSCTTY, 0) == 0);
    assert(tty.session == 42);
    assert(tty.pgrp == 42);
    assert(proc->tty == &tty);
}

static void test_tiocsctty_rejects_foreign_owner_without_steal(void) {
    reset_env();

    process_t *proc = init_proc(0, 50);
    struct pgrp pgrp;
    struct session sess;
    struct tty tty;

    memset(&tty, 0, sizeof(tty));
    tty.session = 77;
    tty.pgrp = 77;
    init_session_leader(proc, &pgrp, &sess, 50);
    current_process = proc;

    assert(tty_ioctl_kern(&tty, TIOCSCTTY, 0) == -1);
    assert(tty.session == 77);
    assert(proc->tty == NULL);

    assert(tty_ioctl_kern(&tty, TIOCSCTTY, 1) == 0);
    assert(tty.session == 50);
    assert(tty.pgrp == 50);
    assert(proc->tty == &tty);
}

static void test_tiocnotty_hangsup_foreground_group(void) {
    reset_env();

    process_t *proc = init_proc(0, 60);
    struct pgrp pgrp;
    struct session sess;
    struct tty tty;

    memset(&tty, 0, sizeof(tty));
    init_session_leader(proc, &pgrp, &sess, 61);
    current_process = proc;
    proc->tty = &tty;
    tty.session = 60;
    tty.pgrp = 61;

    assert(tty_ioctl_kern(&tty, TIOCNOTTY, 0) == 0);
    assert(proc->tty == NULL);
    assert(tty.session == 0);
    assert(tty.pgrp == 0);
    assert(signal_count == 2);
    assert(signal_pgrp[0] == 61);
    assert(signal_sig[0] == SIGHUP);
    assert(signal_pgrp[1] == 61);
    assert(signal_sig[1] == SIGCONT);
}

static void test_tiocpgrp_roundtrip(void) {
    reset_env();

    process_t *proc = init_proc(0, 70);
    struct pgrp pgrp;
    struct session sess;
    struct tty tty;
    int value = 99;
    int out = 0;

    memset(&tty, 0, sizeof(tty));
    init_session_leader(proc, &pgrp, &sess, 70);
    current_process = proc;

    assert(tty_ioctl_kern(&tty, TIOCSPGRP, (uintptr_t)&value) == 0);
    assert(tty.pgrp == 99);
    assert(tty_ioctl_kern(&tty, TIOCGPGRP, (uintptr_t)&out) == 0);
    assert(out == 99);
}

static void test_tiocspgrp_checks_sigttou_for_background_group(void) {
    reset_env();

    process_t *proc = init_proc(0, 80);
    thread_t thread;
    struct pgrp pgrp;
    struct session sess;
    struct tty tty;
    int value = 81;

    memset(&tty, 0, sizeof(tty));
    memset(&thread, 0, sizeof(thread));
    init_session_leader(proc, &pgrp, &sess, 80);
    current_process = proc;
    current_thread = &thread;
    tty.session = 80;
    tty.pgrp = 70;

    assert(tty_ioctl_kern(&tty, TIOCSPGRP, (uintptr_t)&value) == -1);
    assert(tty.pgrp == 70);
    assert(last_psignal_proc == proc);
    assert(last_psignal_sig == SIGTTOU);

    proc->sig_actions[SIGTTOU - 1].sa_handler = SIG_IGN;
    assert(tty_ioctl_kern(&tty, TIOCSPGRP, (uintptr_t)&value) == 0);
    assert(tty.pgrp == 81);
}

int main(void) {
    test_tiocsctty_assigns_owner();
    test_tiocsctty_rejects_foreign_owner_without_steal();
    test_tiocnotty_hangsup_foreground_group();
    test_tiocpgrp_roundtrip();
    test_tiocspgrp_checks_sigttou_for_background_group();
    puts("host_test_tty_jobctl: PASS");
    return 0;
}
