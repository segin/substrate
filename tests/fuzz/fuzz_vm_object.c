#include <stdbool.h>
#include <stddef.h>
#include "../../../sys/vm/vm_object.h"

/*
 * Fuzz Test: Random VM Object operations
 */

void fuzz_vm_object_ops(uint32_t seed) {
    uint32_t state = seed;
    auto next_rand = [&]() {
        state = state * 1103515245 + 12345;
        return (state / 65536) % 32768;
    };

    vm_object_t *objects[10];
    for (int i = 0; i < 10; i++) objects[i] = NULL;

    vm_page_t pages[50];
    for (int i = 0; i < 50; i++) {
        pages[i].object = NULL;
        pages[i].pindex = i;
    }

    for (int i = 0; i < 1000; i++) {
        int obj_idx = next_rand() % 10;
        uint32_t op = next_rand() % 3;

        if (objects[obj_idx] == NULL) {
            objects[obj_idx] = vm_object_allocate(VM_OBJ_TYPE_DEFAULT, 4096 * 10);
        } else {
            if (op == 0) {
                // Add random page
                int pg_idx = next_rand() % 50;
                if (pages[pg_idx].object == NULL) {
                    vm_object_add_page(objects[obj_idx], &pages[pg_idx]);
                }
            } else if (op == 1) {
                // Remove random page
                int pg_idx = next_rand() % 50;
                if (pages[pg_idx].object == objects[obj_idx]) {
                    vm_object_remove_page(objects[obj_idx], &pages[pg_idx]);
                }
            } else {
                // Deallocate
                vm_object_deallocate(objects[obj_idx]);
                objects[obj_idx] = NULL;
            }
        }
    }
}
