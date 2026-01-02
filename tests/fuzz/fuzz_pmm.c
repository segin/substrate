#include <stdbool.h>
#include <stddef.h>
#include "../../../sys/arch/i386/pmm.h"

/*
 * Fuzz Test: Random PMM operations
 */

void fuzz_pmm_ops(uint32_t seed) {
    uint32_t state = seed;
    auto next_rand = [&]() {
        state = state * 1103515245 + 12345;
        return (state / 65536) % 32768;
    };

    void *ptrs[100];
    int counts[100];
    for (int i = 0; i < 100; i++) ptrs[i] = NULL;

    for (int i = 0; i < 1000; i++) {
        int idx = next_rand() % 100;
        if (ptrs[idx] == NULL) {
            // Alloc
            uint32_t op = next_rand() % 2;
            if (op == 0) {
                ptrs[idx] = pmm_alloc_block();
                counts[idx] = 1;
            } else {
                int count = (next_rand() % 8) + 1;
                ptrs[idx] = pmm_alloc_contiguous(count);
                counts[idx] = count;
            }
        } else {
            // Free
            if (counts[idx] == 1) {
                pmm_free_block(ptrs[idx]);
            } else {
                pmm_free_contiguous(ptrs[idx], counts[idx]);
            }
            ptrs[idx] = NULL;
        }
    }
}
