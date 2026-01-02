#include <stdbool.h>
#include <stddef.h>
#include "../../../sys/vm/vm_kmem.h"

/*
 * Fuzz Test: Random Kmem operations
 */

void fuzz_vm_kmem_ops(uint32_t seed) {
    uint32_t state = seed;
    auto next_rand = [&]() {
        state = state * 1103515245 + 12345;
        return (state / 65536) % 32768;
    };

    void *ptrs[100];
    size_t sizes[100];
    for (int i = 0; i < 100; i++) ptrs[i] = NULL;

    for (int i = 0; i < 1000; i++) {
        int idx = next_rand() % 100;
        if (ptrs[idx] == NULL) {
            size_t size = (next_rand() % 2000) + 1;
            ptrs[idx] = kmalloc(size);
            sizes[idx] = size;
        } else {
            kfree(ptrs[idx], sizes[idx]);
            ptrs[idx] = NULL;
        }
    }
}
