#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <pm/pm.h>
#include <sys/time.h>
#include <sys/signal.h>

process_t processes[MAX_PROCS];
process_t *current_process;
thread_t *current_thread;

static int sched_tick_calls;
static int sched_update_loadavg_calls;
static int last_signal;
static int signal_count;
static int current_cpu;

void sched_tick(void) { sched_tick_calls++; }
void sched_update_loadavg(void) { sched_update_loadavg_calls++; }

void psignal(process_t *p, int sig) {
    assert(p == current_process);
    last_signal = sig;
    signal_count++;
}

int copyin(const void *src, void *dst, size_t size) {
    memcpy(dst, src, size);
    return 0;
}

int copyout(const void *src, void *dst, size_t size) {
    memcpy(dst, src, size);
    return 0;
}

void spinlock_init(spinlock_t *lock, const char *name) { (void)lock; (void)name; }
void spinlock_acquire(spinlock_t *lock) { (void)lock; }
void spinlock_release(spinlock_t *lock) { (void)lock; }

int percpu_get_cpu_id(void) { return current_cpu; }
void hw_text_tick(void) {}
void hw_text_tick_1hz(void) {}

#include "../../sys/kern/time.c"

static void reset_env(void) {
    memset(processes, 0, sizeof(processes));
    current_process = &processes[0];
    current_thread = NULL;
    current_process->pid = 10;
    current_process->state = SRUN;
    current_process->is_kernel_task = 0;
    proc_timers_init(current_process);
    for (int i = 1; i < MAX_PROCS; i++) {
        processes[i].pid = -1;
    }
    sched_tick_calls = 0;
    sched_update_loadavg_calls = 0;
    last_signal = 0;
    signal_count = 0;
    current_cpu = 0;
}

static void test_alarm_round_trip_and_cancel(void) {
    struct itimerval curr;

    reset_env();
    assert(kern_alarm(2) == 0);
    assert(kern_getitimer(ITIMER_REAL, &curr) == 0);
    assert(curr.it_value.tv_sec == 2);
    assert(curr.it_value.tv_usec == 0);

    assert(kern_alarm(0) == 2);
    assert(kern_getitimer(ITIMER_REAL, &curr) == 0);
    assert(curr.it_value.tv_sec == 0);
    assert(curr.it_value.tv_usec == 0);
}

static void test_virtual_timer_only_ticks_in_usermode(void) {
    struct itimerval setv;
    struct itimerval curr;

    reset_env();
    memset(&setv, 0, sizeof(setv));
    setv.it_value.tv_usec = 1;
    assert(kern_setitimer(ITIMER_VIRTUAL, &setv, NULL) == 0);

    timer_tick_context(0);
    assert(signal_count == 0);
    assert(kern_getitimer(ITIMER_VIRTUAL, &curr) == 0);
    assert(curr.it_value.tv_sec == 0);
    assert(curr.it_value.tv_usec > 0);

    timer_tick_context(1);
    assert(signal_count == 1);
    assert(last_signal == SIGVTALRM);
    assert(kern_getitimer(ITIMER_VIRTUAL, &curr) == 0);
    assert(curr.it_value.tv_sec == 0);
    assert(curr.it_value.tv_usec == 0);
}

static void test_prof_timer_ticks_and_reloads_interval(void) {
    struct itimerval setv;
    struct itimerval curr;

    reset_env();
    memset(&setv, 0, sizeof(setv));
    setv.it_interval.tv_usec = 1;
    setv.it_value.tv_usec = 1;
    assert(kern_setitimer(ITIMER_PROF, &setv, NULL) == 0);

    timer_tick_context(0);
    assert(signal_count == 1);
    assert(last_signal == SIGPROF);
    assert(kern_getitimer(ITIMER_PROF, &curr) == 0);
    assert(curr.it_value.tv_sec == 0);
    assert(curr.it_value.tv_usec > 0);
}

static void test_real_timer_ticks_only_on_bsp_scan(void) {
    struct itimerval setv;
    struct itimerval curr;

    reset_env();
    memset(&setv, 0, sizeof(setv));
    setv.it_value.tv_usec = 1;
    assert(kern_setitimer(ITIMER_REAL, &setv, NULL) == 0);

    current_cpu = 1;
    timer_tick_context(0);
    assert(signal_count == 0);
    assert(kern_getitimer(ITIMER_REAL, &curr) == 0);
    assert(curr.it_value.tv_usec > 0);

    current_cpu = 0;
    timer_tick_context(0);
    assert(signal_count == 1);
    assert(last_signal == SIGALRM);
    assert(kern_getitimer(ITIMER_REAL, &curr) == 0);
    assert(curr.it_value.tv_sec == 0);
    assert(curr.it_value.tv_usec == 0);
}

static void test_proc_timers_cancel_zeros_all_timers(void) {
    struct itimerval setv;
    struct itimerval curr;

    reset_env();
    memset(&setv, 0, sizeof(setv));
    setv.it_interval.tv_sec = 1;
    setv.it_value.tv_sec = 3;
    assert(kern_setitimer(ITIMER_REAL, &setv, NULL) == 0);
    assert(kern_setitimer(ITIMER_VIRTUAL, &setv, NULL) == 0);
    assert(kern_setitimer(ITIMER_PROF, &setv, NULL) == 0);

    proc_timers_cancel(current_process);

    assert(kern_getitimer(ITIMER_REAL, &curr) == 0);
    assert(curr.it_value.tv_sec == 0);
    assert(curr.it_interval.tv_sec == 0);
    assert(kern_getitimer(ITIMER_VIRTUAL, &curr) == 0);
    assert(curr.it_value.tv_sec == 0);
    assert(curr.it_interval.tv_sec == 0);
    assert(kern_getitimer(ITIMER_PROF, &curr) == 0);
    assert(curr.it_value.tv_sec == 0);
    assert(curr.it_interval.tv_sec == 0);
}

int main(void) {
    test_alarm_round_trip_and_cancel();
    test_virtual_timer_only_ticks_in_usermode();
    test_prof_timer_ticks_and_reloads_interval();
    test_real_timer_ticks_only_on_bsp_scan();
    test_proc_timers_cancel_zeros_all_timers();
    puts("host_test_itimer: PASS");
    return 0;
}
