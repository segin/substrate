/*
 * Unit tests for String Library Functions
 */

#include <kern/console.h>
#include <string.h>
#include "tests.h"

extern int snprintf(char *str, size_t size, const char *format, ...);

static int failed_tests = 0;

static void test_strchr_basic(void) {
    char buf[] = "Hello World";

    // Found 'W'
    char *res = strchr(buf, 'W');
    if (res != buf + 6) {
        char msg[128];
        snprintf(msg, sizeof(msg), "FAIL: strchr('W') expected %p, got %p\n", (void*)(buf + 6), (void*)res);
        kprint(msg);
        failed_tests++;
    }

    // Not found 'z'
    res = strchr(buf, 'z');
    if (res != NULL) {
        char msg[128];
        snprintf(msg, sizeof(msg), "FAIL: strchr('z') expected NULL, got %p\n", (void*)res);
        kprint(msg);
        failed_tests++;
    }

    // Found terminator
    res = strchr(buf, '\0');
    if (res != buf + 11) {
        char msg[128];
        snprintf(msg, sizeof(msg), "FAIL: strchr('\\0') expected %p, got %p\n", (void*)(buf + 11), (void*)res);
        kprint(msg);
        failed_tests++;
    }
}

static void test_strchr_empty(void) {
    char buf[] = "";
    char *res = strchr(buf, '\0');
    if (res != buf) {
        char msg[128];
        snprintf(msg, sizeof(msg), "FAIL: strchr(\"\", '\\0') expected %p, got %p\n", (void*)buf, (void*)res);
        kprint(msg);
        failed_tests++;
    }

    res = strchr(buf, 'a');
    if (res != NULL) {
        char msg[128];
        snprintf(msg, sizeof(msg), "FAIL: strchr(\"\", 'a') expected NULL, got %p\n", (void*)res);
        kprint(msg);
        failed_tests++;
    }
}

void run_string_tests(void) {
    kprint("\n=== STRING TESTS ===\n");
    failed_tests = 0;

    test_strchr_basic();
    test_strchr_empty();

    if (failed_tests == 0) {
        kprint("String Tests: PASS\n");
    } else {
        kprint("String Tests: FAIL\n");
    }
    kprint("=== STRING TESTS COMPLETE ===\n\n");
}
