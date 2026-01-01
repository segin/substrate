#include <stdint.h>

// Stub for a time source (seconds since boot or epoch)
static uint32_t boot_time = 1700000000; // Mock epoch
static uint32_t ticks = 0;

uint32_t get_time(void) {
    return boot_time + (ticks / 100); // Assuming 100Hz
}

// Syscall stub
int sys_time(uint32_t *tloc) {
    uint32_t t = get_time();
    if (tloc) *tloc = t;
    return t;
}

// Tick handler (called from timer interrupt - to be connected)
void timer_tick(void) {
    ticks++;
}
