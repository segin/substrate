#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include "../../../sys/kern/sched.h"

extern void timer_tick(void);

void fuzz_timer_interrupt(const uint8_t *data, size_t size) {
    if (size < sizeof(int)) return;
    int num_ticks = *(int*)data;
    
    // Limit to reasonable number of ticks to avoid infinite loops in test
    num_ticks %= 10000;
    if (num_ticks < 0) num_ticks = -num_ticks;

    for (int i = 0; i < num_ticks; i++) {
        timer_tick();
    }
}

void fuzz_sched_priority(const uint8_t *data, size_t size) {
    if (size < 4) return;
    
    // Simulate setting priority for a random tid
    int tid = (data[0] % 5) + 1; // Mapped to small TIDs
    sched_class_t cls = (sched_class_t)(data[1] % 3);
    int prio = data[2];
    
    sched_set_priority(tid, cls, prio);
    sched_yield();
}
