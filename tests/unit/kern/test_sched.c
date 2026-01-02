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
