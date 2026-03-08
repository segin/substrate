#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <kern/sched.h>
#include <kern/sleepq.h>

thread_t threads[MAX_THREADS];
thread_t *current_thread;
process_t *current_process;

static process_t proc1;
static process_t proc2;

void sched_init(void) {}
void sched_smp_init(int cpu_count) { (void)cpu_count; }
thread_t *sched_alloc_thread(process_t *proc) { (void)proc; return NULL; }
thread_t *sched_create_thread(process_t *proc, void (*entry_point)(void*), void *stack, void *arg) {
    (void)proc; (void)entry_point; (void)stack; (void)arg; return NULL;
}
int sched_fork_process(process_t *parent, void *stack) { (void)parent; (void)stack; return -1; }
int sched_spawn_kernel_process(void (*entry)(void*), void *arg) { (void)entry; (void)arg; return -1; }
void sched_yield(void) {}
void sched_switch(thread_t *next) { (void)next; }
int sched_get_current_tid(void) { return current_thread ? current_thread->tid : -1; }
void sched_set_priority(int tid, sched_class_t cls, int prio) { (void)tid; (void)cls; (void)prio; }
void sched_tick(void) {}
void sched_sleep(void *chan) { if (current_thread) { current_thread->wait_chan = chan; current_thread->state = THREAD_BLOCKED; } }
int sched_sleep_until(void *chan, uint64_t deadline_tick) { (void)deadline_tick; sched_sleep(chan); return 0; }
void sched_wakeup(void *chan) { (void)chan; }
void sched_wakeup_n(void *chan, int n) { (void)chan; (void)n; }
process_t *sched_create_process(struct personality *pers) { (void)pers; return NULL; }
thread_t *sched_get_thread(int tid) { return (tid >= 0 && tid < MAX_THREADS) ? &threads[tid] : NULL; }
void sched_iterate_threads(void (*callback)(thread_t *t, void *arg), void *arg) { (void)callback; (void)arg; }
void sched_reap_process_threads(process_t *proc) { (void)proc; }
void sched_check_timeouts(void) {}
int sched_can_run_on_cpu(thread_t *t, int cpu_id) { (void)t; (void)cpu_id; return 1; }
int sched_bind_thread(thread_t *t, int cpu_id) { (void)t; (void)cpu_id; return 0; }
void sched_unbind_thread(thread_t *t) { (void)t; }
void sched_update_loadavg(void) {}
void sched_get_loadavg(unsigned long *loads) { if (loads) loads[0] = loads[1] = loads[2] = 0; }
uint32_t sched_count_runnable(void) { return 0; }
uint32_t sched_count_threads(void) { return 0; }

#include "../../sys/kern/sleepq.c"

static void reset_env(void) {
    memset(threads, 0, sizeof(threads));
    memset(&proc1, 0, sizeof(proc1));
    memset(&proc2, 0, sizeof(proc2));
    proc1.pid = 100;
    proc2.pid = 200;
    current_process = NULL;
    current_thread = NULL;
    for (int i = 0; i < MAX_THREADS; i++) {
        threads[i].tid = -1;
    }
    sleepq_init();
}

static thread_t *init_thread(int slot, int tid) {
    thread_t *t = &threads[slot];
    memset(t, 0, sizeof(*t));
    t->tid = tid;
    t->state = THREAD_RUNNING;
    return t;
}

static void test_private_helpers_tolerate_null_process(void) {
    int chan = 0;

    reset_env();
    assert(sleepq_wake_one_private(&chan) == NULL);
    assert(sleepq_wake_all_private(&chan) == 0);
    assert(sleepq_wake_n_private(&chan, 1) == 0);
    assert(sleepq_has_waiters_private(&chan) == 0);
    assert(sleepq_requeue_private(&chan, &chan, 1, 1) == 0);
}

static void test_private_queue_stays_pid_scoped(void) {
    int chan = 0;
    thread_t *t1;
    thread_t *t2;

    reset_env();
    t1 = init_thread(0, 1);
    t2 = init_thread(1, 2);

    current_process = &proc1;
    current_thread = t1;
    sleepq_add_private(&chan, t1);

    current_process = &proc2;
    current_thread = t2;
    sleepq_add_private(&chan, t2);

    current_process = &proc1;
    assert(sleepq_has_waiters_private(&chan) == 1);
    assert(sleepq_wake_one_private(&chan) == t1);
    assert(t2->state == THREAD_BLOCKED);

    current_process = &proc2;
    assert(sleepq_wake_all_private(&chan) == 1);
    assert(t2->state == THREAD_READY);
}

int main(void) {
    test_private_helpers_tolerate_null_process();
    test_private_queue_stays_pid_scoped();
    puts("host_test_sleepq_private: PASS");
    return 0;
}
