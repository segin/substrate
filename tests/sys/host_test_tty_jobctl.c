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
static int tty_driver_open_count;
static int tty_driver_close_count;
static int tty_driver_open_errno;
static unsigned char tty_driver_out[256];
static int tty_driver_out_len;

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
void kprint(const char *str) { (void)str; }
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
    tty_driver_open_count = 0;
    tty_driver_close_count = 0;
    tty_driver_open_errno = 0;
    tty_driver_out_len = 0;
    memset(tty_driver_out, 0, sizeof(tty_driver_out));
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

static int mock_tty_open(struct tty *tty) {
    (void)tty;
    tty_driver_open_count++;
    return tty_driver_open_errno;
}

static void mock_tty_close(struct tty *tty) {
    (void)tty;
    tty_driver_close_count++;
}

static int mock_tty_write(struct tty *tty, const unsigned char *buf, int count) {
    (void)tty;
    int room = (int)sizeof(tty_driver_out) - tty_driver_out_len;
    if (room <= 0) {
        return 0;
    }
    if (count > room) {
        count = room;
    }
    memcpy(tty_driver_out + tty_driver_out_len, buf, (size_t)count);
    tty_driver_out_len += count;
    return count;
}

static int mock_tty_write_room(struct tty *tty) {
    (void)tty;
    return (int)sizeof(tty_driver_out) - tty_driver_out_len;
}


static void test_tty_open_close_refcounts_driver_transitions(void) {
    struct tty_driver driver = {
        .open = mock_tty_open,
        .close = mock_tty_close,
    };
    struct tty *tty;

    reset_env();

    tty = tty_alloc(&driver, 0);
    assert(tty != NULL);

    assert(tty_open(tty) == 0);
    assert(tty->count == 1);
    assert(tty->driver_active == 1);
    assert(tty_driver_open_count == 1);
    assert(tty_driver_close_count == 0);

    assert(tty_open(tty) == 0);
    assert(tty->count == 2);
    assert(tty_driver_open_count == 1);

    tty_close(tty);
    assert(tty->count == 1);
    assert(tty->driver_active == 1);
    assert(tty_driver_close_count == 0);

    tty_close(tty);
    assert(tty->count == 0);
    assert(tty->driver_active == 0);
    assert(tty_driver_close_count == 1);

    tty_free(tty);
}

static void test_tty_open_failure_restores_state(void) {
    struct tty_driver driver = {
        .open = mock_tty_open,
        .close = mock_tty_close,
    };
    struct tty *tty;

    reset_env();
    tty_driver_open_errno = -5;

    tty = tty_alloc(&driver, 1);
    assert(tty != NULL);

    assert(tty_open(tty) == -5);
    assert(tty->count == 0);
    assert(tty->driver_active == 0);
    assert(tty->lifecycle_busy == 0);
    assert(tty_driver_open_count == 1);
    assert(tty_driver_close_count == 0);

    tty_free(tty);
}

static void test_tiocsctty_assigns_owner(void) {
    reset_env();

    process_t *proc = init_proc(0, 42);
    struct pgrp pgrp;
    struct session sess;
    struct tty tty;

    memset(&tty, 0, sizeof(tty));
    tty.magic = TTY_MAGIC;
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
    tty.magic = TTY_MAGIC;
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
    tty.magic = TTY_MAGIC;
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
    tty.magic = TTY_MAGIC;
    init_session_leader(proc, &pgrp, &sess, 70);
    current_process = proc;

    assert(tty_ioctl_kern(&tty, TIOCSPGRP, (uintptr_t)&value) == 0);
    assert(tty.pgrp == 99);
    assert(tty_ioctl_kern(&tty, TIOCGPGRP, (uintptr_t)&out) == 0);
    assert(out == 99);
}

static void test_tty_erase_echo_sequence(void) {
    struct tty_driver driver = {
        .write = mock_tty_write,
        .write_room = mock_tty_write_room,
    };
    struct tty *tty;

    reset_env();

    tty = tty_alloc(&driver, 2);
    assert(tty != NULL);

    tty_flip_buffer_push(tty, 'a');
    tty_flip_buffer_push(tty, tty->termios.c_cc[VERASE]);

    assert(tty_driver_out_len == 4);
    assert(tty_driver_out[0] == 'a');
    assert(tty_driver_out[1] == '');
    assert(tty_driver_out[2] == ' ');
    assert(tty_driver_out[3] == '');

    tty_free(tty);
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
    tty.magic = TTY_MAGIC;
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
    test_tty_open_close_refcounts_driver_transitions();
    test_tty_open_failure_restores_state();
    test_tiocsctty_assigns_owner();
    test_tiocsctty_rejects_foreign_owner_without_steal();
    test_tiocnotty_hangsup_foreground_group();
    test_tiocpgrp_roundtrip();
    test_tiocspgrp_checks_sigttou_for_background_group();
    test_tty_erase_echo_sequence();
    puts("host_test_tty_jobctl: PASS");
    return 0;
}
