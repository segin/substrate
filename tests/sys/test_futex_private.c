/*
 * test_futex_private.c - Private Futex Tests
 *
 * Tests for FUTEX_PRIVATE_FLAG functionality.
 */

#include <sys/futex.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>

/* Test framework macros */
#define TEST_ASSERT(cond, msg) do { \
    if (!(cond)) { \
        test_fail(__func__, __LINE__, msg); \
        return 1; \
    } \
} while(0)

#define TEST_PASS() do { test_pass(__func__); return 0; } while(0)

static int tests_passed = 0;
static int tests_failed = 0;

extern void kprint(const char *s);
extern int sys_futex(int *uaddr, int op, int val, void *timeout, int *uaddr2, int val3);

static void test_pass(const char *name) {
    (void)name;
    tests_passed++;
}

static void test_fail(const char *name, int line, const char *msg) {
    (void)name;
    (void)line;
    (void)msg;
    tests_failed++;
}

/* ========== Unit Tests ========== */

/*
 * Test: FUTEX_WAIT_PRIVATE works (returns EAGAIN on mismatch)
 */
static int test_futex_wait_private_mismatch(void) {
    int futex_word = 42;
    /* FUTEX_WAIT_PRIVATE = FUTEX_WAIT | FUTEX_PRIVATE_FLAG */
    int result = sys_futex(&futex_word, FUTEX_WAIT_PRIVATE, 0, NULL, NULL, 0);
    TEST_ASSERT(result == -11 /* EAGAIN */, "Wait private with mismatch should return EAGAIN");
    TEST_PASS();
}

/*
 * Test: FUTEX_WAKE_PRIVATE works (returns 0 on empty)
 */
static int test_futex_wake_private_empty(void) {
    int futex_word = 0;
    int result = sys_futex(&futex_word, FUTEX_WAKE_PRIVATE, 1, NULL, NULL, 0);
    TEST_ASSERT(result == 0, "Wake private on empty should return 0");
    TEST_PASS();
}

/*
 * Test: FUTEX_REQUEUE_PRIVATE works (returns 0 on empty)
 */
static int test_futex_requeue_private_empty(void) {
    int futex1 = 0;
    int futex2 = 0;

    int result = sys_futex(&futex1, FUTEX_REQUEUE_PRIVATE, 1, (void *)1, &futex2, 0);
    TEST_ASSERT(result == 0, "Requeue private on empty should return 0");
    TEST_PASS();
}

/* ========== Test Runner ========== */

typedef int (*test_fn)(void);

struct test_case {
    const char *name;
    test_fn fn;
};

static struct test_case futex_private_tests[] = {
    {"futex_wait_private_mismatch", test_futex_wait_private_mismatch},
    {"futex_wake_private_empty", test_futex_wake_private_empty},
    {"futex_requeue_private_empty", test_futex_requeue_private_empty},
    {NULL, NULL}
};

int test_futex_private_run_all(void) {
    tests_passed = 0;
    tests_failed = 0;

    for (int i = 0; futex_private_tests[i].fn != NULL; i++) {
        futex_private_tests[i].fn();
    }

    return tests_failed;
}

void test_futex_private(void) {
    kprint("  [FUTEX] Running private futex tests...\n");
    int failed = test_futex_private_run_all();
    if (failed == 0) {
        kprint("  [FUTEX] All private tests passed\n");
    } else {
        kprint("  [FUTEX] Some private tests failed\n");
    }
}
