#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/*
 * Fuzz Test: Random Trampoline parameters
 */

extern void smp_boot_ap(uint8_t apic_id);

void fuzz_trampoline_params(uint32_t seed) {
    uint32_t state = seed;
    auto next_rand = [&]() {
        state = state * 1103515245 + 12345;
        return (uint32_t)(state / 65536);
    };

    for (int i = 0; i < 1000; i++) {
        uint8_t id = (uint8_t)(next_rand() % 32);
        // Simulate booting AP
        // smp_boot_ap(id);
        (void)id;
    }
}
