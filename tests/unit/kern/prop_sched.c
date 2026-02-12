#include <stdbool.h>
#include <stdint.h>
#include <kern/sched.h"

extern void timer_tick(void);
extern uint32_t get_time(void);

bool prop_time_is_monotonic(int iterations) {
    uint32_t last_time = get_time();
    for (int i = 0; i < iterations; i++) {
        timer_tick();
        uint32_t current_time = get_time();
        if (current_time < last_time) return false;
        last_time = current_time;
    }
    return true;
}

bool prop_realtime_preempts_timeshare(void) {
    sched_init();
    char s1[4096], s2[4096];
    int t_ts = sched_create_thread(current_process, (void*)0x1, s1+4096, NULL);
    int t_rt = sched_create_thread(current_process, (void*)0x2, s2+4096, NULL);
    
    // RT with lower priority than TS
    sched_set_priority(t_ts, SCHED_TIMESHARE, 100);
    sched_set_priority(t_rt, SCHED_REALTIME, 10);
    
    sched_yield();
    return (current_thread->tid == t_rt);
}

bool prop_sleep_wakeup_consistency(void) {
    sched_init();
    char s1[4096], s2[4096];
    int t1 = sched_create_thread(current_process, (void*)0x1, s1+4096, NULL);
    int t2 = sched_create_thread(current_process, (void*)0x2, s2+4096, NULL);
    
    void *chan1 = (void*)0x111;
    void *chan2 = (void*)0x222;
    
    // Switch to t1 and sleep
    sched_yield(); // should pick t1
    if (current_thread->tid != t1) return false;
    sched_sleep(chan1);
    
    // Switch to t2 and sleep
    sched_yield(); // should pick t2
    if (current_thread->tid != t2) return false;
    sched_sleep(chan2);
    
    // Wakeup chan1
    sched_wakeup(chan1);
    if (sched_get_thread(t1)->state != THREAD_READY) return false;
    if (sched_get_thread(t2)->state != THREAD_BLOCKED) return false;
    
    return true;
}

void run_sched_properties(void) {
    prop_time_is_monotonic(1000);
}
