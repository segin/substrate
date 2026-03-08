#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <pm/pm.h>
#include <sys/session.h>
#include <sys/signal.h>

process_t processes[MAX_PROCS];
process_t *current_process;
thread_t *current_thread;
mutex_t proctree_lock;

static int last_signal_mask;

void mutex_init(mutex_t *m, const char *name) { (void)m; (void)name; }
void mutex_lock(mutex_t *m) { (void)m; }
void mutex_unlock(mutex_t *m) { (void)m; }

void *kmalloc(size_t size) {
    return calloc(1, size);
}

void kfree(void *ptr, size_t size) {
    (void)size;
    free(ptr);
}

void psignal(process_t *proc, int sig) {
    if (!proc || sig <= 0 || sig > NSIG) return;
    proc->sig_catch |= sigmask(sig);
    last_signal_mask |= sigmask(sig);
}

#include "../../sys/pm/pgrp.c"

static void reset_env(void) {
    memset(processes, 0, sizeof(processes));
    for (int i = 0; i < MAX_PROCS; i++) {
        processes[i].pid = -1;
    }
    current_process = NULL;
    current_thread = NULL;
    last_signal_mask = 0;
}

static process_t *alloc_proc(int slot, int pid, int ppid) {
    process_t *proc = &processes[slot];
    memset(proc, 0, sizeof(*proc));
    proc->pid = pid;
    proc->ppid = ppid;
    return proc;
}

static void assert_group_session_chain(process_t *proc, int sid) {
    assert(proc->p_pgrp != NULL);
    assert(proc->p_pgrp->pg_session != NULL);
    assert(proc->p_pgrp->pg_session->s_sid == sid);
}

static void test_setsid_and_ids(void) {
    reset_env();

    process_t *leader = alloc_proc(0, 10, 0);
    leader->tty = (struct tty *)0x1;
    current_process = leader;

    assert(sys_setsid() == 10);
    assert(leader->tty == NULL);
    assert_group_session_chain(leader, 10);
    assert(leader->p_pgrp->pg_id == 10);
    assert(sys_getsid(0) == 10);
    assert(sys_getpgid(0) == 10);
}

static void test_setpgid_paths(void) {
    reset_env();

    process_t *parent = alloc_proc(0, 20, 0);
    current_process = parent;
    assert(sys_setsid() == 20);

    process_t *child = alloc_proc(1, 21, 20);
    proc_join_pgrp(child, parent->p_pgrp);
    assert_group_session_chain(parent, 20);
    assert_group_session_chain(child, 20);

    assert(sys_setpgid(21, 0) == 0);
    assert_group_session_chain(child, 20);
    assert(child->p_pgrp->pg_id == 21);
    assert(sys_getpgid(21) == 21);
    assert(sys_getsid(21) == 20);

    process_t *peer = alloc_proc(2, 22, 20);
    proc_join_pgrp(peer, parent->p_pgrp);
    assert_group_session_chain(peer, 20);
    assert(sys_setpgid(22, 21) == 0);
    assert(peer->p_pgrp == child->p_pgrp);
    assert_group_session_chain(peer, 20);
}

static void test_orphaned_group_signals(void) {
    reset_env();

    process_t *session_leader = alloc_proc(0, 30, 0);
    current_process = session_leader;
    assert(sys_setsid() == 30);

    process_t *child = alloc_proc(1, 31, 30);
    proc_join_pgrp(child, session_leader->p_pgrp);
    assert(sys_setpgid(31, 0) == 0);

    process_t *stopped = alloc_proc(2, 32, 30);
    stopped->state = SSTOP;
    stopped->p_parent = child;
    proc_join_pgrp(stopped, child->p_pgrp);

    stopped->sig_catch = 0;
    last_signal_mask = 0;

    session_leader->p_pgrp = NULL;
    child->p_parent = session_leader;

    assert(pgrp_is_orphaned(child->p_pgrp) == 1);
    pgrp_check_orphan(child->p_pgrp);
    assert(last_signal_mask & sigmask(SIGHUP));
    assert(last_signal_mask & sigmask(SIGCONT));
    assert(stopped->sig_catch & sigmask(SIGHUP));
    assert(stopped->sig_catch & sigmask(SIGCONT));
}

int main(void) {
    test_setsid_and_ids();
    test_setpgid_paths();
    test_orphaned_group_signals();
    puts("host_test_pgrp: PASS");
    return 0;
}
