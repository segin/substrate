#include <stdbool.h>
#include <stddef.h>
#include "../../../sys/vm/vm_zone.h"

/*
 * Fuzz Test: Random Zone Allocator operations
 */

void fuzz_vm_zone_ops(uint32_t seed) {
    uint32_t state = seed;
    auto next_rand = [&]() {
        state = state * 1103515245 + 12345;
        return (state / 65536) % 32768;
    };

    vm_zone_t *zones[4];
    zones[0] = vm_zone_create("fuzz-16", 16, 16);
    zones[1] = vm_zone_create("fuzz-32", 32, 32);
    zones[2] = vm_zone_create("fuzz-64", 64, 64);
    zones[3] = vm_zone_create("fuzz-128", 128, 128);

    void *ptrs[100];
    int zone_map[100];
    for (int i = 0; i < 100; i++) ptrs[i] = NULL;

    for (int i = 0; i < 1000; i++) {
        int idx = next_rand() % 100;
        if (ptrs[idx] == NULL) {
            int z_idx = next_rand() % 4;
            ptrs[idx] = vm_zone_alloc(zones[z_idx]);
            zone_map[idx] = z_idx;
        } else {
            vm_zone_free(zones[zone_map[idx]], ptrs[idx]);
            ptrs[idx] = NULL;
        }
    }
}
