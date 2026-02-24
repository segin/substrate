/*
 * Host-based Unit Tests for CRC32 Implementation
 *
 * Compile with:
 * gcc -m32 -g -Wall -DHOST_TEST -I../../sys/include -I../../include -I../../sys -I. host_test_crc32.c -o host_test_crc32
 */

#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>

/*
 * Include the implementation directly to test internal behavior if needed,
 * and to avoid linking complexity.
 */
#include "../../sys/lib/crc32.c"

/*
 * Test Helper Macros
 */
#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            fprintf(stderr, "FAIL: %s (%s:%d)\n", msg, __FILE__, __LINE__); \
            exit(1); \
        } \
    } while(0)

#define TEST_ASSERT_EQ_HEX(actual, expected, msg) \
    do { \
        if ((actual) != (expected)) { \
            fprintf(stderr, "FAIL: %s. Expected 0x%08X, got 0x%08X (%s:%d)\n", \
                    msg, (expected), (actual), __FILE__, __LINE__); \
            exit(1); \
        } \
    } while(0)

void test_initialization(void) {
    printf("Testing initialization...\n");

    // Reset state for test (though static variable makes it tricky,
    // including the source file allows us to inspect internal state if we exposed it,
    // but here we just rely on public API).
    // crc32_initialized is static in crc32.c. We can't easily reset it without modifying source or weak symbols.
    // However, crc32_init() is idempotent.

    crc32_init();

    // Call it again to ensure no crash or error
    crc32_init();

    printf("PASS\n");
}

void test_auto_initialization(void) {
    printf("Testing auto-initialization...\n");

    // Manually reset initialization flag to force auto-init path
    // Since we include the .c file, we have access to static variables.
    crc32_initialized = 0;

    // Also clear the table to ensure it really gets re-initialized
    memset(crc32_table, 0, sizeof(crc32_table));

    // Use a known vector: "123456789" -> 0xCBF43926
    const char *digits = "123456789";
    uint32_t crc_digits = crc32(digits, 9);

    // Verify correct calculation (implies table was initialized)
    TEST_ASSERT_EQ_HEX(crc_digits, 0xCBF43926, "Auto-init CRC32 result");

    // Verify initialization flag was set
    TEST_ASSERT(crc32_initialized == 1, "Auto-init flag set");

    printf("PASS\n");
}

void test_vectors(void) {
    printf("Testing standard vectors...\n");

    // 1. Empty string
    const char *empty = "";
    uint32_t crc_empty = crc32(empty, 0);
    // Standard CRC32 of empty string is 0x00000000
    TEST_ASSERT_EQ_HEX(crc_empty, 0x00000000, "Empty string CRC32");

    // 2. "123456789"
    const char *digits = "123456789";
    uint32_t crc_digits = crc32(digits, 9);
    // Expected: 0xCBF43926
    TEST_ASSERT_EQ_HEX(crc_digits, 0xCBF43926, "\"123456789\" CRC32");

    // 3. "The quick brown fox jumps over the lazy dog"
    const char *fox = "The quick brown fox jumps over the lazy dog";
    uint32_t crc_fox = crc32(fox, 43);
    // Expected: 0x414FA339
    TEST_ASSERT_EQ_HEX(crc_fox, 0x414FA339, "Fox sentence CRC32");

    printf("PASS\n");
}

void test_large_buffer(void) {
    printf("Testing large buffer...\n");

    size_t size = 1024 * 1024; // 1MB
    uint8_t *buf = (uint8_t *)malloc(size);
    TEST_ASSERT(buf != NULL, "Malloc failed");

    memset(buf, 0xAB, size); // Fill with pattern

    // We don't have a pre-calculated vector for 1MB of 0xAB easily available,
    // but we can verify consistency.
    // Let's compute it.
    uint32_t crc1 = crc32(buf, size);
    uint32_t crc2 = crc32(buf, size);

    TEST_ASSERT_EQ_HEX(crc1, crc2, "Deterministic result");

    // Check first half and second half?
    // crc(A, B) is complex to compose.

    free(buf);
    printf("PASS\n");
}

int main(void) {
    printf("=== CRC32 Host Test Suite ===\n");

    test_initialization();
    test_auto_initialization();
    test_vectors();
    test_large_buffer();

    printf("=== All Tests Passed ===\n");
    return 0;
}
