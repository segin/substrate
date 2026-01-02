#include <stdbool.h>
#include <stddef.h>
#include "../../../sys/arch/i386/pmap.h"

/*
 * Fuzz Test: Random PMAP (i386) operations
 */

void fuzz_pmap_i386_ops(uint32_t seed) {
    uint32_t state = seed;
    auto next_rand = [&]() {
        state = state * 1103515245 + 12345;
        return (state / 65536) % 32768;
    };

    pmap_t kernel_pmap = pmap_kernel();

    for (int i = 0; i < 500; i++) {
        uintptr_t va = (next_rand() % 1024) * 4096 + 0x2000000;
        uintptr_t pa = (next_rand() % 1024) * 4096 + 0x1000000;
        uint32_t op = next_rand() % 2;

        if (op == 0) {
            pmap_enter(kernel_pmap, va, pa, VM_PROT_READ|VM_PROT_WRITE, 0);
        } else {
            pmap_remove(kernel_pmap, va);
        }
        
        // Occasional sanity check
        if (i % 50 == 0) {
            pmap_extract(kernel_pmap, va);
        }
    }
}
