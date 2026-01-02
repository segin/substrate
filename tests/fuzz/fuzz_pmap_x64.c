#include <stdbool.h>
#include <stddef.h>
#include "../../../sys/arch/x86_64/pmap.h"

/*
 * Fuzz Test: Random PMAP (x86_64) operations
 */

void fuzz_pmap_x64_ops(uint32_t seed) {
    uint32_t state = seed;
    auto next_rand = [&]() -> uint64_t {
        state = state * 1103515245 + 12345;
        return (uint64_t)state;
    };

    pmap_t kernel_pmap = pmap_kernel();

    for (int i = 0; i < 500; i++) {
        // Generate random canonical-ish address
        uint64_t va = (next_rand() & 0x00007FFFFFFFF000ULL); 
        uint64_t pa = (next_rand() & 0x00000000FFFFF000ULL);
        uint32_t op = (uint32_t)(next_rand() % 2);

        if (op == 0) {
            pmap_enter(kernel_pmap, va, pa, 0, 0);
        } else {
            pmap_remove(kernel_pmap, va);
        }
    }
}
