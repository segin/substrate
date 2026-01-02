#include <stdbool.h>
#include <stdint.h>
#include "../../../sys/kern/sched.h"

extern void timer_tick(void);
extern uint32_t get_time(void);

bool test_timer_tick_increments(void) {
    uint32_t initial_time = get_time();
    // Simulate 100 ticks (1 second at 100Hz)
    for (int i = 0; i < 100; i++) {
        timer_tick();
    }
    uint32_t new_time = get_time();
    return (new_time == initial_time + 1);
}

bool test_sched_priority(void) {
    // Reset scheduler state
    sched_init();
    
    char stack1[4096];
    char stack2[4096];
    
    // Create two threads
    int tid1 = sched_create_thread(current_process, (void*)0x100, stack1 + 4096, NULL);
    int tid2 = sched_create_thread(current_process, (void*)0x200, stack2 + 4096, NULL);
    
    // Set tid2 to higher priority (Realtime)
    sched_set_priority(tid2, SCHED_REALTIME, 50);
    sched_set_priority(tid1, SCHED_TIMESHARE, 20);
    
    // Yield - should pick tid2
    sched_yield();
    
    if (current_thread->tid != tid2) return false;
    
    // Set tid1 to even higher Realtime priority
    sched_set_priority(tid1, SCHED_REALTIME, 60);
    
    sched_yield();
    if (current_thread->tid != tid1) return false;
    
    return true;
}

bool test_sched_sleep_wakeup(void) {
    sched_init();
    
    char s1[4096];
    int tid1 = sched_create_thread(current_process, (void*)0x100, s1 + 4096, NULL);
    
    void *chan = (void*)0xDEADBEEF;
    
    // Switch to tid1
    sched_yield();
    if (current_thread->tid != tid1) return false;
    
    // tid1 sleeps
    sched_sleep(chan);
    
    // Should have switched back to kernel thread (tid 1)
    if (current_thread->tid != 1) return false;
    if (sched_get_thread(tid1)->state != THREAD_BLOCKED) return false;
    
    // Wakeup
    sched_wakeup(chan);
    if (sched_get_thread(tid1)->state != THREAD_READY) return false;
    
    // Yield should pick tid1 again
    sched_yield();
    if (current_thread->tid != tid1) return false;
    
    return true;
}
