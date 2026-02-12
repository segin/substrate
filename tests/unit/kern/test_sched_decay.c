#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "../../../sys/kern/sched.h"
#include "../../../sys/include/sys/proc.h"

// Mocks - use weak linkage to avoid conflicts if they are defined elsewhere
thread_t threads[MAX_THREADS] __attribute__((weak));
thread_t *current_thread __attribute__((weak)) = NULL;
process_t *current_process __attribute__((weak)) = NULL;

// Mock sched_interactivity_boost to avoid dependency on sched_interactivity.c
// We want to test decay in isolation.
int __attribute__((weak)) sched_interactivity_boost(thread_t *t) {
    (void)t;
    return 0;
}

// Forward declarations of functions to test (from sched_decay.c)
void sched_recalc_priority(thread_t *t);
void sched_decay_tick(uint32_t current_tick);
void sched_recalc_all_priorities(uint32_t current_tick);
void sched_decay_on_voluntary_sleep(thread_t *t);
void sched_decay_on_yield(thread_t *t);

// Helper to reset threads
static void reset_threads(void) {
    memset(threads, 0, sizeof(threads));
    for (int i = 0; i < MAX_THREADS; i++) {
        threads[i].tid = -1; // Mark as free
    }
}

static thread_t* create_test_thread(int tid, sched_class_t cls, int base_prio) {
    if (tid < 0 || tid >= MAX_THREADS) return NULL;
    threads[tid].tid = tid;
    threads[tid].sched_class = cls;
    threads[tid].base_priority = base_prio;
    threads[tid].priority = base_prio;
    threads[tid].run_time = 0;
    threads[tid].sleep_time = 0;
    return &threads[tid];
}

bool test_sched_decay_priority(void) {
    reset_threads();

    // 1. SCHED_REALTIME: Should not change
    thread_t *t_rt = create_test_thread(1, SCHED_REALTIME, 50);
    t_rt->run_time = 1000; // Even with high run time
    sched_recalc_priority(t_rt);
    if (t_rt->priority != 50) return false;

    // 2. SCHED_IDLE: Should be 0
    thread_t *t_idle = create_test_thread(2, SCHED_IDLE, 0);
    sched_recalc_priority(t_idle);
    if (t_idle->priority != 0) return false;

    // 3. SCHED_TIMESHARE: Base case (0 runtime)
    thread_t *t_ts = create_test_thread(3, SCHED_TIMESHARE, 20);
    sched_recalc_priority(t_ts);
    if (t_ts->priority != 20) return false;

    // 4. SCHED_TIMESHARE: Penalty calculation
    // Penalty = run_time / DECAY_PERIOD. DECAY_PERIOD is 100.
    // Set run_time = 200 -> penalty should be 2.
    t_ts->run_time = 200;
    sched_recalc_priority(t_ts);
    // priority = base + penalty = 20 + 2 = 22
    if (t_ts->priority != 22) return false;

    // 5. SCHED_TIMESHARE: Max penalty cap
    // Code says: if (penalty > 10) penalty = 10;
    t_ts->run_time = 2000; // 2000 / 100 = 20, cap at 10
    sched_recalc_priority(t_ts);
    // priority = 20 + 10 = 30
    if (t_ts->priority != 30) return false;

    // 6. SCHED_TIMESHARE: Clamping
    // Max priority is 39.
    t_ts->base_priority = 35;
    t_ts->run_time = 1000; // Penalty 10
    sched_recalc_priority(t_ts);
    // 35 + 10 = 45 -> clamped to 39
    if (t_ts->priority != 39) return false;

    return true;
}

bool test_sched_decay_tick_update(void) {
    reset_threads();

    thread_t *t = create_test_thread(1, SCHED_TIMESHARE, 20);
    t->run_time = 300; // Should result in penalty +3

    // Initial state
    t->priority = 20;

    // sched_decay_tick checks (current_tick - last_decay_tick < DECAY_PERIOD).
    // last_decay_tick is static and init to 0.
    // Call with current_tick = DECAY_PERIOD (100) to trigger decay.

    // We assume this test runs fresh or we need to account for previous runs updating static var.
    // Since we can't reset static var easily, we'll use a large step.
    // But wait, the function does: if (current_tick - last_decay_tick < DECAY_PERIOD) return;
    // So we just need to ensure we pass a tick > last_decay_tick + 100.
    // We'll use a very large number to be safe (assuming it doesn't overflow logic, but it's subtraction).

    sched_decay_tick(10000);

    // Should have decayed
    // Penalty = 300 / 100 = 3.
    // Priority = 20 + 3 = 23.
    if (t->priority != 23) return false;

    // Now verify it doesn't decay again immediately
    t->priority = 20; // Reset
    sched_decay_tick(10010); // +10 ticks, less than 100
    if (t->priority != 20) return false; // Should not have changed

    return true;
}

bool test_sched_decay_starvation(void) {
    reset_threads();

    thread_t *t = create_test_thread(1, SCHED_TIMESHARE, 20);
    t->run_time = 0;
    t->sleep_time = STARVATION_LIMIT + 10; // 510
    t->state = THREAD_READY; // Must be ready to be starved

    // Trigger decay tick
    sched_decay_tick(20000);

    // Should be boosted
    // STARVATION_BOOST is 5.
    // Priority = 20 - 5 = 15.
    if (t->priority != 15) return false;

    return true;
}

bool test_sched_decay_actions(void) {
    thread_t t;
    t.run_time = 20;

    sched_decay_on_voluntary_sleep(&t);
    // Should reduce by 10
    if (t.run_time != 10) return false;

    sched_decay_on_voluntary_sleep(&t);
    // Should reduce to 0 (10 - 10)
    if (t.run_time != 0) return false;

    t.run_time = 5;
    sched_decay_on_voluntary_sleep(&t);
    // Should clamp to 0
    if (t.run_time != 0) return false;

    t.run_time = 20;
    sched_decay_on_yield(&t);
    // Should reduce by 5
    if (t.run_time != 15) return false;

    return true;
}

bool test_sched_decay_suite(void) {
    if (!test_sched_decay_priority()) return false;
    if (!test_sched_decay_tick_update()) return false;
    if (!test_sched_decay_starvation()) return false;
    if (!test_sched_decay_actions()) return false;
    return true;
}
