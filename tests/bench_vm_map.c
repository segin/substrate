#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <vm/vm_map.h>

#define N_ENTRIES 2000
#define N_LOOKUPS 100000
#define ENTRY_SIZE 4096

// Mock pmap (void*)
#define PMAP_KERNEL ((pmap_t)1)

int main() {
    vm_map_t *map = vm_map_create(PMAP_KERNEL, 0, 0xFFFFFFFF);
    if (!map) {
        fprintf(stderr, "Failed to create map\n");
        return 1;
    }

    printf("Inserting %d entries...\n", N_ENTRIES);
    for (int i = 0; i < N_ENTRIES; i++) {
        uintptr_t start = (uintptr_t)i * ENTRY_SIZE;
        uintptr_t end = start + ENTRY_SIZE;
        // vm_map_insert(map, object, offset, start, end, prot, max_prot, inheritance)
        if (vm_map_insert(map, NULL, 0, start, end, 0, 0, 0) != 0) {
            fprintf(stderr, "Failed to insert at %d (start=%lx)\n", i, (unsigned long)start);
            return 1;
        }
    }

    printf("Performing %d lookups...\n", N_LOOKUPS);

    // Seed random
    srand(12345);

    clock_t start_time = clock();

    for (int i = 0; i < N_LOOKUPS; i++) {
        // Generate random address within the range
        // Max range is N_ENTRIES * ENTRY_SIZE
        int index = rand() % N_ENTRIES;
        uintptr_t va = (uintptr_t)index * ENTRY_SIZE + (rand() % ENTRY_SIZE);

        vm_map_entry_t *entry = vm_map_lookup(map, va);
        if (!entry) {
            fprintf(stderr, "Lookup failed for va %lx (index %d)\n", (unsigned long)va, index);
            return 1;
        }

        if (va < entry->start || va >= entry->end) {
            fprintf(stderr, "Lookup returned wrong entry: va %lx, entry [%lx-%lx]\n",
                (unsigned long)va, (unsigned long)entry->start, (unsigned long)entry->end);
            return 1;
        }
    }

    clock_t end_time = clock();
    double time_taken = (double)(end_time - start_time) / CLOCKS_PER_SEC;

    printf("Time taken: %f seconds\n", time_taken);
    printf("Average lookup time: %f ns\n", (time_taken * 1e9) / N_LOOKUPS);

    vm_map_destroy(map);
    return 0;
}
