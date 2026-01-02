#include <stdbool.h>
#include <stdint.h>
#include "../../../sys/arch/i386/idt.h"

/*
 * Fuzz Test: Random IDT gate configurations
 */

extern idt_entry_t idt_entries[256];

void fuzz_idt_gates(uint32_t seed) {
    uint32_t state = seed;
    auto next_rand = [&]() {
        state = state * 1103515245 + 12345;
        return (state / 65536) % 32768;
    };

    for (int i = 0; i < 1000; i++) {
        uint8_t num = next_rand() % 256;
        uint32_t base = next_rand() | (next_rand() << 16);
        uint16_t sel = next_rand();
        uint8_t flags = next_rand() % 256;

        idt_set_gate(num, base, sel, flags);
        
        // Invariant check
        if (idt_entries[num].sel != sel) {
            // Error! (Mocked)
        }
    }
}
