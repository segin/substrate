#include <stdbool.h>
#include <stdint.h>
#include "../../../sys/kern/sched.h"

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

void run_sched_properties(void) {
    prop_time_is_monotonic(1000);
}
