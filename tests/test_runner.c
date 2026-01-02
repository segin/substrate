#include <stdio.h>
#include <stdbool.h>
#include "../sys/vm/vm_zone.h"
#include "../sys/vm/vm_kmem.h"
#include "../sys/vm/vm_object.h"
#include "../sys/vm/vm_page.h"

// VM Tests
extern bool test_kmem_basic_alloc(void);
extern bool test_kmem_multiple_alloc(void);
extern bool test_kmem_too_large(void);
extern bool test_zone_create_and_alloc(void);
extern bool test_zone_exhaustion(void);
extern bool test_vm_map_init(void);
extern bool test_vm_map_insert_and_find(void);
extern bool test_vm_map_remove(void);
extern bool test_vm_object_lifecycle(void);
extern bool test_vm_object_page_mgmt(void);
extern bool test_vm_page_queue_ops(void);
extern bool test_vm_page_flags(void);
extern bool test_vm_fault_anonymous(void);
extern bool test_vm_fault_protection_violation(void);
extern bool test_mmap_logic(void);
extern bool test_munmap_logic(void);
extern bool test_swap_lifecycle(void);
extern bool test_swap_full(void);
extern bool test_vm_fault_cow_trigger(void);

// Arch Tests (Mocked)
extern bool test_trampoline_preparation(void);

typedef struct {
    const char *name;
    bool (*func)(void);
} test_case_t;

test_case_t tests[] = {
    {"kmem_basic", test_kmem_basic_alloc},
    {"kmem_multi", test_kmem_multiple_alloc},
    {"kmem_large", test_kmem_too_large},
    {"zone_basic", test_zone_create_and_alloc},
    {"zone_exhaust", test_zone_exhaustion},
    {"map_init", test_vm_map_init},
    {"map_insert", test_vm_map_insert_and_find},
    {"map_remove", test_vm_map_remove},
    {"object_life", test_vm_object_lifecycle},
    {"object_page", test_vm_object_page_mgmt},
    {"page_queues", test_vm_page_queue_ops},
    {"page_flags", test_vm_page_flags},
    {"fault_anon", test_vm_fault_anonymous},
    {"fault_prot", test_vm_fault_protection_violation},
    {"mmap_logic", test_mmap_logic},
    {"munmap_logic", test_munmap_logic},
    {"swap_life", test_swap_lifecycle},
    {"swap_full", test_swap_full},
    {"cow_trigger", test_vm_fault_cow_trigger},
    {"smp_tramp", test_trampoline_preparation},
    {NULL, NULL}
};

int main() {
    int passed = 0;
    int total = 0;

    // Initialize subsystems
    vm_zone_init();
    kmem_init();
    vm_object_init();
    vm_page_init();

    printf("Starting TestUnix Unit Tests...\n");
    printf("--------------------------------\n");

    for (int i = 0; tests[i].name != NULL; i++) {
        total++;
        printf("[%02d] Testing %-20s ... ", total, tests[i].name);
        if (tests[i].func()) {
            printf("PASS\n");
            passed++;
        } else {
            printf("FAIL\n");
        }
    }

    printf("--------------------------------\n");
    printf("Result: %d/%d passed.\n", passed, total);
    return (passed == total) ? 0 : 1;
}
