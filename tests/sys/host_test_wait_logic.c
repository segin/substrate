#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <pm/pm.h>
#include <kern/sched.h>
#include <sys/wait.h>
#include <sys/session.h>

process_t processes[MAX_PROCS];
process_t *current_process;
thread_t *current_thread;
thread_t threads[MAX_THREADS];
mutex_t proctree_lock;

static process_t mock_parent;
static process_t mock_child1;
static process_t mock_child2;
static process_t mock_child3;
static struct pgrp mock_pg1;
static struct pgrp mock_pg2;
static thread_t mock_thread;

static int sched_sleep_calls;
static int sched_sleep_mode;

void mutex_init(mutex_t *m, const char *name) { (void)m; (void)name; }
void mutex_lock(mutex_t *m) { (void)m; }
void mutex_unlock(mutex_t *m) { (void)m; }
int copyout(const void *src, void *dst, size_t len) { memcpy(dst, src, len); return 0; }

void proc_remove_child(process_t *parent, process_t *child) {
    if (!parent || !child) return;
    if (parent->p_children == child) {
        parent->p_children = child->p_sibling;
    } else {
        process_t *prev = parent->p_children;
        while (prev && prev->p_sibling != child) prev = prev->p_sibling;
        if (prev) prev->p_sibling = child->p_sibling;
    }
    child->p_sibling = NULL;
}

void pgrp_remove_proc(struct process *proc) {
    (void)proc;
}

void sched_reap_process_threads(process_t *proc) {
    (void)proc;
}

void sched_sleep(void *chan) {
    (void)chan;
    sched_sleep_calls++;
    if (sched_sleep_mode == 0) {
        if (mock_child2.pid == 102 && mock_child2.state == SRUN) {
            mock_child2.state = SZOMB;
        }
    } else if (sched_sleep_mode == 1) {
        current_thread->sig_pending = sigmask(SIGUSR1);
    }
}

int kern_wait4(pid_t pid, int *status, int options, struct rusage *rusage);

static void setup_mocks(void) {
    memset(processes, 0, sizeof(processes));
    memset(&mock_parent, 0, sizeof(mock_parent));
    memset(&mock_child1, 0, sizeof(mock_child1));
    memset(&mock_child2, 0, sizeof(mock_child2));
    memset(&mock_child3, 0, sizeof(mock_child3));
    memset(&mock_pg1, 0, sizeof(mock_pg1));
    memset(&mock_pg2, 0, sizeof(mock_pg2));
    memset(&mock_thread, 0, sizeof(mock_thread));

    for (int i = 0; i < MAX_PROCS; i++) processes[i].pid = -1;
    for (int i = 0; i < MAX_THREADS; i++) threads[i].tid = -1;

    mock_pg1.pg_id = 100;
    mock_pg2.pg_id = 200;

    mock_parent.pid = 100;
    mock_parent.p_pgrp = &mock_pg1;

    mock_child1.pid = 101;
    mock_child1.p_pgrp = &mock_pg1;
    mock_child1.state = SZOMB;
    mock_child1.p_sibling = &mock_child2;
    mock_child1.p_parent = &mock_parent;
    mock_child1.exit_code = 10;

    mock_child2.pid = 102;
    mock_child2.p_pgrp = &mock_pg1;
    mock_child2.state = SRUN;
    mock_child2.p_sibling = &mock_child3;
    mock_child2.p_parent = &mock_parent;

    mock_child3.pid = 103;
    mock_child3.p_pgrp = &mock_pg2;
    mock_child3.state = SZOMB;
    mock_child3.p_sibling = NULL;
    mock_child3.p_parent = &mock_parent;
    mock_child3.exit_code = 20;

    mock_parent.p_children = &mock_child1;
    current_process = &mock_parent;
    current_thread = &mock_thread;
    current_thread->sig_pending = 0;
    current_thread->sig_mask = 0;

    sched_sleep_calls = 0;
    sched_sleep_mode = 0;
}

static void test_wait_search_and_reap(void) {
    int status = 0;

    setup_mocks();
    assert(kern_wait4(101, &status, WNOHANG, NULL) == 101);
    assert(WEXITSTATUS(status) == 10);
    assert(mock_parent.p_children == &mock_child2);

    setup_mocks();
    assert(kern_wait4(-1, &status, WNOHANG, NULL) == 101);

    setup_mocks();
    assert(kern_wait4(0, &status, WNOHANG, NULL) == 101);

    setup_mocks();
    assert(kern_wait4(-200, &status, WNOHANG, NULL) == 103);
    assert(WEXITSTATUS(status) == 20);

    setup_mocks();
    assert(kern_wait4(999, &status, WNOHANG, NULL) == -ECHILD);
}

static void test_wait_wnohang_and_blocking(void) {
    int status = 0;

    setup_mocks();
    assert(kern_wait4(102, &status, WNOHANG, NULL) == 0);

    setup_mocks();
    mock_child1.state = SRUN;
    mock_child3.state = SRUN;
    assert(kern_wait4(-1, &status, WNOHANG, NULL) == 0);

    setup_mocks();
    mock_parent.p_children = NULL;
    assert(kern_wait4(-1, &status, WNOHANG, NULL) == -ECHILD);

    setup_mocks();
    mock_child2.exit_code = 30;
    assert(kern_wait4(102, &status, 0, NULL) == 102);
    assert(WEXITSTATUS(status) == 30);
    assert(sched_sleep_calls == 1);
    assert(mock_child1.p_sibling == &mock_child3);
    assert(mock_child2.pid == -1);
    assert(mock_child2.state == 0);

    setup_mocks();
    sched_sleep_mode = 1;
    assert(kern_wait4(102, &status, 0, NULL) == -EINTR);
    assert(sched_sleep_calls == 1);
}

static void test_wait_job_control_states(void) {
    int status = 0;

    setup_mocks();
    mock_child2.state = SSTOP;
    mock_child2.p_flag = 0;
    assert(kern_wait4(102, &status, WUNTRACED, NULL) == 102);
    assert(WIFSTOPPED(status));
    assert(mock_child2.p_flag & P_WAITED);

    setup_mocks();
    mock_child2.p_flag = P_CONTINUED;
    assert(kern_wait4(102, &status, WCONTINUED, NULL) == 102);
    assert(WIFCONTINUED(status));
    assert((mock_child2.p_flag & P_CONTINUED) == 0);
}

#include "../../sys/pm/wait.c"

int main(void) {
    test_wait_search_and_reap();
    test_wait_wnohang_and_blocking();
    test_wait_job_control_states();
    puts("host_test_wait_logic: PASS");
    return 0;
}
