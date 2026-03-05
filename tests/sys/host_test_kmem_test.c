#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Mock implementations */
int kprint_called = 0;
int kmalloc_called = 0;
int kmalloc_size = 0;
int allow_allocation = 0;

void *kmalloc(int size) {
    kmalloc_called = 1;
    kmalloc_size = size;
    if (allow_allocation) {
        return malloc(size);
    }
    return NULL;
}

void kprint(const char *msg) {
    kprint_called = 1;
    // Don't clutter test output with mock messages
    // printf("MOCK kprint: %s", msg);
}

#include "../../sys/drivers/devices/kmem_test.c"

int main() {
    int passed = 0;
    int failed = 0;

    printf("Running kmem_test tests...\n");

    // Test case 1: Very large size allocation failure
    kmem_test_size = 0x7FFFFFFF;

    // Reset state
    kmalloc_called = 0;
    kmalloc_size = 0;
    kprint_called = 0;
    allow_allocation = 0; // Force NULL return
    kmem_test_buffer = (void *)0xDEADBEEF; // Start with dummy value

    kmem_test_init();

    if (kmalloc_called && kmalloc_size == 0x7FFFFFFF && kprint_called && kmem_test_buffer == NULL) {
        printf("[OK] Failed allocation handled gracefully\n");
        passed++;
    } else {
        printf("[FAIL] Allocation failure not handled properly\n");
        failed++;
    }

    // Test case 2: Normal allocation success
    kmem_test_size = 256;

    // Reset state
    kmalloc_called = 0;
    kmalloc_size = 0;
    kprint_called = 0;
    allow_allocation = 1; // Allow malloc to succeed

    kmem_test_init();

    if (kmalloc_called && kmalloc_size == 256 && kprint_called && kmem_test_buffer != NULL) {
        printf("[OK] Normal allocation successful and printed log\n");
        passed++;

        // Check content
        if (strncmp((char*)kmem_test_buffer, "KMEM_TEST_START", 15) == 0) {
            printf("[OK] Buffer starts with known string\n");
            passed++;
        } else {
            printf("[FAIL] Buffer content missing start string\n");
            failed++;
        }

        char *end_msg = "KMEM_TEST_END: End of buffer.";
        char *end_ptr = (char*)kmem_test_buffer + kmem_test_size - strlen(end_msg) - 1;
        if (strcmp(end_ptr, end_msg) == 0) {
            printf("[OK] Buffer ends with known string\n");
            passed++;
        } else {
            printf("[FAIL] Buffer content missing end string\n");
            failed++;
        }

        free(kmem_test_buffer);
    } else {
        printf("[FAIL] Normal allocation failed\n");
        failed++;
    }

    printf("\nTest Summary: %d passed, %d failed\n", passed, failed);
    return failed > 0 ? 1 : 0;
}
