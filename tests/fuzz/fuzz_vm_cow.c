#include <stdbool.h>
#include <stddef.h>
#include "../../../sys/vm/vm_fault.h"
#include "../../../sys/vm/vm_object.h"

/*
 * Fuzz Test: Random CoW operations
 */

void fuzz_vm_cow_ops(uint32_t seed) {
    uint32_t state = seed;
    auto next_rand = [&]() {
        state = state * 1103515245 + 12345;
        return (state / 65536) % 32768;
    };

    vm_map_t maps[5];
    vm_object_t *shared_obj = vm_object_allocate(VM_OBJ_TYPE_DEFAULT, 0x10000);
    
    for (int i = 0; i < 5; i++) {
        vm_map_init(&maps[i], pmap_kernel(), 0x1000, 0x11000);
        vm_map_insert(&maps[i], shared_obj, 0, 0x1000, 0x11000, 0x3, 0x3, 1);
        vm_object_reference(shared_obj);
    }

    for (int i = 0; i < 1000; i++) {
        int map_idx = next_rand() % 5;
        uint32_t addr = (next_rand() % 16) * 4096 + 0x1000;
        uint32_t prot = (next_rand() % 2) + 1; // Read or Write

        vm_fault(&maps[map_idx], addr, prot);
    }
}
