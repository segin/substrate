#include <stdbool.h>
#include <stddef.h>
#include "../../../sys/vm/vm_map.h"

/*
 * Fuzz Test: Random VM Map operations
 */

void fuzz_vm_map_ops(uint32_t seed) {
    uint32_t state = seed;
    auto next_rand = [&]() {
        state = state * 1103515245 + 12345;
        return (state / 65536) % 32768;
    };

    vm_map_t map;
    vm_map_init(&map, NULL, 0x1000, 0x1000000);

    for (int i = 0; i < 1000; i++) {
        uint32_t op = next_rand() % 3;
        uint32_t addr = (next_rand() % 1000) * 4096 + 0x1000;
        uint32_t len = ((next_rand() % 50) + 1) * 4096;

        if (op == 0) {
            // Insert
            vm_map_insert(&map, NULL, 0, addr, addr + len, 0x3, 0x3, 1);
        } else if (op == 1) {
            // Remove
            vm_map_remove(&map, addr, addr + len);
        } else {
            // Find space
            uintptr_t found_addr;
            vm_map_find_space(&map, &found_addr, len);
        }
    }
}
