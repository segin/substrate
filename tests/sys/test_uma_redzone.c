#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

// Mock panic to exit(1) or set a flag
static int panic_called = 0;
void panic(const char *msg) {
    printf("PANIC CALLED: %s\n", msg);
    panic_called = 1;
    // exit(1); // Don't exit to allow test to verify
}

// Mock kprint
void kprint(const char *msg) {
    printf("KPRINT: %s", msg);
}

// Mocks for UMA dependencies
void *pmm_alloc_block(void) {
    return calloc(1, 4096);
}

void pmm_free_block(void *p) {
    free(p);
}

void *pmm_alloc_contiguous(size_t pages) {
    return calloc(pages, 4096);
}

void pmm_free_contiguous(void *p, size_t pages) {
    free(p);
}

int smp_get_cpu_count(void) { return 1; }
int smp_get_cpu_id(void) { return 0; }

void *kzalloc(size_t size) {
    return calloc(1, size);
}

void kfree(void *p, size_t size) {
    free(p);
}

// Include UMA implementation
// Adjust include path when compiling
#include <vm/uma.h>
#include "../../sys/vm/uma_core.c"
#include "../../sys/vm/uma_debug.c"

int main() {
    printf("Initializing UMA...\n");
    uma_startup();
    uma_enable_dynamic_alloc();

    printf("Creating zone with Redzone...\n");
    uma_zone_t *zone = uma_zcreate("test_zone", 32, NULL, NULL, NULL, NULL, 0, UMA_ZONE_REDZONE);
    if (!zone) {
        printf("Failed to create zone\n");
        return 1;
    }

    printf("Allocating item...\n");
    void *item = uma_zalloc(zone, 0);
    if (!item) {
        printf("Failed to allocate item\n");
        return 1;
    }

    printf("Corrupting redzone...\n");
    // Redzone is at item + size
    // Wait, uma_zalloc returns pointer to user data.
    // Redzone is AFTER user data.
    // uma_debug.c says:
    // uint8_t *post = (uint8_t *)item + zone->uz_size;

    // Let's corrupt the post redzone
    uint8_t *post = (uint8_t *)item + 32;
    *post = 0x00; // Corrupt it (should be 0xFE)

    printf("Freeing item (expecting panic)...\n");
    uma_zfree(zone, item);

    if (panic_called) {
        printf("Test PASSED: Panic was called correctly.\n");
        return 0;
    } else {
        printf("Test FAILED: Panic was NOT called.\n");
        return 1;
    }
}
