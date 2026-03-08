#include <assert.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <pm/pm.h>
#include <sys/proc.h>
#include <kern/sched.h>

#define _ARCH_I386_INTR_H
static uint32_t intr_flags_seen;
uint32_t intr_disable(void) { intr_flags_seen++; return 0x1234; }
void intr_restore(uint32_t flags) { intr_flags_seen ^= flags; }
void intr_enable(void) {}
void wait_for_interrupt(void) { longjmp(*(jmp_buf *)current_thread->wait_chan, 1); }

process_t processes[MAX_PROCS];
thread_t threads[MAX_THREADS];
thread_t *current_thread;
process_t *current_process;

static thread_t *idle_balance_target;
static int vm_pageout_calls;
static jmp_buf swapper_loop_jmp;

void vm_pageout(void) {
    vm_pageout_calls++;
    longjmp(swapper_loop_jmp, 1);
}

thread_t *sched_idle_balance(void) {
    return idle_balance_target;
}

void sched_switch(thread_t *next) {
    current_thread = next;
    current_process = next ? next->proc : NULL;
    longjmp(swapper_loop_jmp, 1);
}

#include "../../sys/kern/swapper.c"

static void reset_env(void) {
    memset(processes, 0, sizeof(processes));
    memset(threads, 0, sizeof(threads));
    current_thread = NULL;
    current_process = NULL;
    idle_balance_target = NULL;
    vm_pageout_calls = 0;
    intr_flags_seen = 0;
    for (int i = 0; i < MAX_PROCS; i++) {
        processes[i].pid = -1;
    }
    for (int i = 0; i < MAX_THREADS; i++) {
        threads[i].tid = -1;
    }
}

static void test_swapper_init_seeds_fallback_context(void) {
    reset_env();
    swapper_init();
    assert(current_thread != NULL);
    assert(current_process != NULL);
    assert(current_process->pid == 0);
    assert(sched_is_idle() == 1);
}

static void test_swapper_prefers_live_pid0_process(void) {
    reset_env();
    processes[0].pid = 0;
    strcpy(processes[0].comm, "swapper");
    assert(swapper_get_proc() == &processes[0]);
}

static void test_swapper_request_work_runs_pageout(void) {
    reset_env();
    swapper_init();
    current_thread->wait_chan = &swapper_loop_jmp;
    swapper_request_work();

    if (setjmp(swapper_loop_jmp) == 0) {
        swapper_idle_loop();
        assert(!"swapper_idle_loop should have broken out");
    }

    assert(vm_pageout_calls == 1);
}

static void test_swapper_idle_switches_directly_to_runnable_thread(void) {
    thread_t next;
    process_t proc;

    reset_env();
    swapper_init();
    memset(&next, 0, sizeof(next));
    memset(&proc, 0, sizeof(proc));
    proc.pid = 2;
    next.tid = 2;
    next.proc = &proc;
    next.state = THREAD_READY;
    next.sched_class = SCHED_TIMESHARE;
    idle_balance_target = &next;

    if (setjmp(swapper_loop_jmp) == 0) {
        swapper_idle_loop();
        assert(!"swapper_idle_loop should have switched");
    }

    assert(current_thread == &next);
    assert(current_process == &proc);
}

int main(void) {
    test_swapper_init_seeds_fallback_context();
    test_swapper_prefers_live_pid0_process();
    test_swapper_request_work_runs_pageout();
    test_swapper_idle_switches_directly_to_runnable_thread();
    puts("host_test_swapper: PASS");
    return 0;
}
