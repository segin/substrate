#include <stdbool.h>
#include <stddef.h>
#include "../../../sys/vm/vm_page.h"

/*
 * Fuzz Test: Random Swap operations
 */

extern int swap_out(vm_page_t *m);
extern int swap_in(vm_page_t *m);

void fuzz_swap_ops(uint32_t seed) {
    uint32_t state = seed;
    auto next_rand = [&]() {
        state = state * 1103515245 + 12345;
        return (state / 65536) % 32768;
    };

    vm_page_t pool[100];
    for (int i = 0; i < 100; i++) {
        pool[i].flags = PG_VALID;
        pool[i].phys_addr = i * 4096;
    }

    for (int i = 0; i < 1000; i++) {
        int idx = next_rand() % 100;
        if (!(pool[idx].flags & PG_SWAPPED)) {
            swap_out(&pool[idx]);
        } else {
            swap_in(&pool[idx]);
        }
    }
}
