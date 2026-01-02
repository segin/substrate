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
