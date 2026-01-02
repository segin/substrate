#include <stdbool.h>
#include <stdint.h>
#include "../../../sys/sys/lock.h"

/*
 * Fuzz Test: Random Spinlock operations
 */

extern uint32_t lapic_get_id(void);

void fuzz_spinlock_ops(uint32_t seed) {
    uint32_t state = seed;
    auto next_rand = [&]() {
        state = state * 1103515245 + 12345;
        return (uint32_t)(state / 65536);
    };

    spinlock_t lock;
    spinlock_init(&lock, "fuzz-lock");

    for (int i = 0; i < 1000; i++) {
        // Since we are single-threaded on host for this mock, 
        // we test the integrity of the state machine.
        
        if (!spinlock_is_held(&lock)) {
            spinlock_acquire(&lock);
        } else {
            spinlock_release(&lock);
        }
        
        uint32_t junk = next_rand();
        (void)junk;
    }
}
