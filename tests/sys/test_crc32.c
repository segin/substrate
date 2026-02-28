/*
 * Unit tests for CRC32 Implementation
 */

#include <kern/console.h>
#include <sys/crc32.h>
#include <string.h>
#include "tests.h"

extern int snprintf(char *str, size_t size, const char *format, ...);

static int failed_tests = 0;

static void test_crc32_empty(void) {
    const char *data = "";
    uint32_t expected = 0;
    uint32_t actual = crc32(data, 0);

    if (actual != expected) {
        char msg[128];
        snprintf(msg, sizeof(msg), "FAIL: crc32(\"\") expected 0, got 0x%x\n", actual);
        kprint(msg);
        failed_tests++;
    }
}

static void test_crc32_basic(void) {
    const char *data = "123456789";
    uint32_t expected = 0xCBF43926;
    uint32_t actual = crc32(data, 9);

    if (actual != expected) {
        char msg[128];
        snprintf(msg, sizeof(msg), "FAIL: crc32(\"123456789\") expected 0xCBF43926, got 0x%x\n", actual);
        kprint(msg);
        failed_tests++;
    }
}

static void test_crc32_fox(void) {
    const char *data = "The quick brown fox jumps over the lazy dog";
    uint32_t expected = 0x414FA339;
    uint32_t actual = crc32(data, 43);

    if (actual != expected) {
        char msg[128];
        snprintf(msg, sizeof(msg), "FAIL: crc32(\"The quick brown fox...\") expected 0x414FA339, got 0x%x\n", actual);
        kprint(msg);
        failed_tests++;
    }
}

static void test_crc32_zeros(void) {
    uint8_t data[32] = {0};
    uint32_t expected = 0x190A55AD;
    uint32_t actual = crc32(data, sizeof(data));

    if (actual != expected) {
        char msg[128];
        snprintf(msg, sizeof(msg), "FAIL: crc32(zeros) expected 0x190A55AD, got 0x%x\n", actual);
        kprint(msg);
        failed_tests++;
    }
}

static void test_crc32_ones(void) {
    uint8_t data[32];
    memset(data, 0xFF, sizeof(data));
    uint32_t expected = 0xFF6CAB0B;
    uint32_t actual = crc32(data, sizeof(data));

    if (actual != expected) {
        char msg[128];
        snprintf(msg, sizeof(msg), "FAIL: crc32(ones) expected 0xFF6CAB0B, got 0x%x\n", actual);
        kprint(msg);
        failed_tests++;
    }
}

void run_crc32_tests(void) {
    kprint("\n=== CRC32 TESTS ===\n");
    failed_tests = 0;

    // Ensure initialized (should be by kmain, but harmless to repeat)
    crc32_init();

    test_crc32_empty();
    test_crc32_basic();
    test_crc32_fox();
    test_crc32_zeros();
    test_crc32_ones();

    if (failed_tests == 0) {
        kprint("CRC32 Tests: PASS\n");
    } else {
        kprint("CRC32 Tests: FAIL\n");
    }
    kprint("=== CRC32 TESTS COMPLETE ===\n\n");
}
