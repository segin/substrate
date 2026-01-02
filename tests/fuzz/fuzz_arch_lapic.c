#include <stdbool.h>
#include <stdint.h>
#include "../../../sys/arch/i386/lapic.h"

/*
 * Fuzz Test: Random LAPIC register values
 */

void fuzz_lapic_regs(uint32_t seed) {
    uint32_t state = seed;
    auto next_rand = [&]() {
        state = state * 1103515245 + 12345;
        return (uint32_t)(state / 65536);
    };

    for (int i = 0; i < 1000; i++) {
        uint32_t val = next_rand();
        // Simulate setting Spurious vector with random bits
        // In live kernel this would be lapic_write(LAPIC_SVR, val);
        (void)val;
    }
}
