#include <stdbool.h>
#include <stdint.h>
#include "../../../sys/arch/i386/gdt.h"

/*
 * Fuzz Test: Random TSS updates
 */

void fuzz_tss_updates(uint32_t seed) {
    uint32_t state = seed;
    auto next_rand = [&]() {
        state = state * 1103515245 + 12345;
        return (state / 65536) % 32768;
    };

    for (int i = 0; i < 1000; i++) {
        uint32_t stack = next_rand() | (next_rand() << 16);
        set_kernel_stack(stack);
    }
}
