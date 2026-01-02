#include <stdbool.h>
#include <stddef.h>
#include "../../../sys/vm/vm_fault.h"
#include "../../../sys/vm/vm_object.h"

/*
 * Fuzz Test: Random VM Fault operations
 */

void fuzz_vm_fault_ops(uint32_t seed) {
    uint32_t state = seed;
    auto next_rand = [&]() {
        state = state * 1103515245 + 12345;
        return (state / 65536) % 32768;
    };

    vm_map_t map;
    vm_map_init(&map, pmap_kernel(), 0x1000, 0x100000);
    
    vm_object_t *obj = vm_object_allocate(VM_OBJ_TYPE_DEFAULT, 0x100000);
    vm_map_insert(&map, obj, 0, 0x1000, 0x50000); // Only map half

    for (int i = 0; i < 1000; i++) {
        uint32_t addr = (next_rand() % 256) * 4096 + 0x1000;
        uint32_t prot = (next_rand() % 3) + 1;

        // Trigger fault
        vm_fault(&map, addr, prot);
    }
}
