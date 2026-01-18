/*
 * test_futex.c - Futex Subsystem Tests
 *
 * Tests for:
 * - Basic FUTEX_WAIT/WAKE operations
 * - FUTEX_REQUEUE functionality
 * - Robust list registration/cleanup
 * - Priority Inheritance operations
 * - Edge cases and error handling
 */

#include "../include/sys/futex.h"
#include <stdint.h>
#include <string.h>

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
 * Test: validate_uaddr rejects kernel addresses
 */
static int test_futex_validate_kernel_addr(void) {
    /* Kernel addresses should return -EFAULT from sys_futex */
    int kernel_addr = 0xC0001000;  /* In kernel space */
    int result = sys_futex((int *)(uintptr_t)kernel_addr, FUTEX_WAKE, 1, 
                           NULL, NULL, 0);
    TEST_ASSERT(result == -14 /* EFAULT */, "Should reject kernel address");
    TEST_PASS();
}

/*
 * Test: validate_uaddr rejects unaligned addresses
 */
static int test_futex_validate_unaligned(void) {
    /* Unaligned addresses should fail */
    char buf[8] __attribute__((aligned(4)));
    int result = sys_futex((int *)(buf + 1), FUTEX_WAKE, 1, NULL, NULL, 0);
    TEST_ASSERT(result == -14 /* EFAULT */, "Should reject unaligned address");
    TEST_PASS();
}

/*
 * Test: FUTEX_WAKE on non-contended futex returns 0
 */
static int test_futex_wake_empty(void) {
    int futex_word = 0;
    int result = sys_futex(&futex_word, FUTEX_WAKE, 100, NULL, NULL, 0);
    TEST_ASSERT(result == 0, "Wake on empty queue should return 0");
    TEST_PASS();
}

/*
 * Test: FUTEX_WAIT with non-matching value returns -EAGAIN
 */
static int test_futex_wait_mismatch(void) {
    int futex_word = 42;
    int result = sys_futex(&futex_word, FUTEX_WAIT, 0, NULL, NULL, 0);
    TEST_ASSERT(result == -11 /* EAGAIN */, "Wait with wrong val should return EAGAIN");
    TEST_PASS();
}

/*
 * Test: Robust list registration
 */
static int test_futex_set_robust_list(void) {
    struct robust_list_head head;
    memset(&head, 0, sizeof(head));
    head.list.next = &head.list;  /* Empty circular list */
    head.futex_offset = 0;
    
    int result = sys_set_robust_list(&head, sizeof(head));
    TEST_ASSERT(result == 0, "set_robust_list should succeed");
    
    /* Clear it */
    result = sys_set_robust_list(NULL, sizeof(head));
    TEST_ASSERT(result == 0, "set_robust_list(NULL) should succeed");
    
    TEST_PASS();
}

/*
 * Test: Robust list with wrong size fails
 */
static int test_futex_set_robust_list_badsize(void) {
    struct robust_list_head head;
    int result = sys_set_robust_list(&head, 1);  /* Wrong size */
    TEST_ASSERT(result == -22 /* EINVAL */, "Wrong size should fail");
    TEST_PASS();
}

/*
 * Test: get_robust_list for current thread
 */
static int test_futex_get_robust_list(void) {
    struct robust_list_head head;
    memset(&head, 0, sizeof(head));
    head.list.next = &head.list;
    
    /* Set the list */
    int result = sys_set_robust_list(&head, sizeof(head));
    TEST_ASSERT(result == 0, "set should succeed");
    
    /* Get it back */
    struct robust_list_head *out_head = NULL;
    size_t out_len = 0;
    result = sys_get_robust_list(0, &out_head, &out_len);
    TEST_ASSERT(result == 0, "get should succeed");
    TEST_ASSERT(out_head == &head, "Should return same pointer");
    TEST_ASSERT(out_len == sizeof(head), "Should return correct size");
    
    /* Clear */
    sys_set_robust_list(NULL, sizeof(head));
    
    TEST_PASS();
}

/*
 * Test: Unknown futex operation returns -ENOSYS
 */
static int test_futex_unknown_op(void) {
    int futex_word = 0;
    int result = sys_futex(&futex_word, 9999, 0, NULL, NULL, 0);
    TEST_ASSERT(result == -38 /* ENOSYS */, "Unknown op should return ENOSYS");
    TEST_PASS();
}

/*
 * Test: FUTEX_CMP_REQUEUE with wrong compare value
 */
static int test_futex_cmp_requeue_mismatch(void) {
    int futex1 = 100;
    int futex2 = 0;
    
    /* val3 (compare value) doesn't match current value */
    int result = sys_futex(&futex1, FUTEX_CMP_REQUEUE, 1, (void *)1, 
                           &futex2, 0);  /* val3=0, but *futex1=100 */
    TEST_ASSERT(result == -11 /* EAGAIN */, "Compare mismatch should fail");
    TEST_PASS();
}

/*
 * Test: PI unlock without owning returns -EPERM
 */
static int test_futex_unlock_pi_not_owner(void) {
    int futex_word = 999;  /* Owned by TID 999 */
    
    int result = sys_futex(&futex_word, FUTEX_UNLOCK_PI, 0, NULL, NULL, 0);
    TEST_ASSERT(result == -1 /* EPERM */, "Non-owner unlock should fail");
    TEST_PASS();
}

/*
 * Test: FUTEX_TRYLOCK_PI when already locked
 */
static int test_futex_trylock_pi_contended(void) {
    int futex_word = 999;  /* Already locked by TID 999 */
    
    int result = sys_futex(&futex_word, FUTEX_TRYLOCK_PI, 0, NULL, NULL, 0);
    /* Should return -EWOULDBLOCK since it's already locked */
    TEST_ASSERT(result == -11 /* EWOULDBLOCK/EAGAIN */, 
                "Trylock on locked should fail");
    TEST_PASS();
}

/* ========== Property Tests ========== */

/*
 * Property: Wake always returns <= val
 */
static int test_futex_wake_prop_bounded(void) {
    int futex_word = 0;
    
    for (int i = 0; i < 100; i++) {
        int wake_count = i % 20;
        int result = sys_futex(&futex_word, FUTEX_WAKE, wake_count, 
                               NULL, NULL, 0);
        TEST_ASSERT(result >= 0 && result <= wake_count,
                    "Wake count should be bounded");
    }
    TEST_PASS();
}

/*
 * Property: Robust list survives multiple set operations
 */
static int test_futex_robust_list_prop_multiset(void) {
    struct robust_list_head heads[10];
    
    for (int i = 0; i < 10; i++) {
        memset(&heads[i], 0, sizeof(heads[i]));
        heads[i].list.next = &heads[i].list;
        
        int result = sys_set_robust_list(&heads[i], sizeof(heads[i]));
        TEST_ASSERT(result == 0, "Each set should succeed");
    }
    
    /* Verify last one is active */
    struct robust_list_head *out;
    size_t len;
    sys_get_robust_list(0, &out, &len);
    TEST_ASSERT(out == &heads[9], "Last set should be active");
    
    sys_set_robust_list(NULL, sizeof(heads[0]));
    TEST_PASS();
}

/* ========== Test Runner ========== */

typedef int (*test_fn)(void);

struct test_case {
    const char *name;
    test_fn fn;
};

static struct test_case futex_tests[] = {
    {"futex_validate_kernel_addr", test_futex_validate_kernel_addr},
    {"futex_validate_unaligned", test_futex_validate_unaligned},
    {"futex_wake_empty", test_futex_wake_empty},
    {"futex_wait_mismatch", test_futex_wait_mismatch},
    {"futex_set_robust_list", test_futex_set_robust_list},
    {"futex_set_robust_list_badsize", test_futex_set_robust_list_badsize},
    {"futex_get_robust_list", test_futex_get_robust_list},
    {"futex_unknown_op", test_futex_unknown_op},
    {"futex_cmp_requeue_mismatch", test_futex_cmp_requeue_mismatch},
    {"futex_unlock_pi_not_owner", test_futex_unlock_pi_not_owner},
    {"futex_trylock_pi_contended", test_futex_trylock_pi_contended},
    {"futex_wake_prop_bounded", test_futex_wake_prop_bounded},
    {"futex_robust_list_prop_multiset", test_futex_robust_list_prop_multiset},
    {NULL, NULL}
};

int test_futex_run_all(void) {
    tests_passed = 0;
    tests_failed = 0;
    
    for (int i = 0; futex_tests[i].fn != NULL; i++) {
        futex_tests[i].fn();
    }
    
    return tests_failed;
}

/*
 * Entry point for test runner
 */
void test_futex(void) {
    kprint("  [FUTEX] Running futex tests...\n");
    int failed = test_futex_run_all();
    if (failed == 0) {
        kprint("  [FUTEX] All tests passed\n");
    } else {
        kprint("  [FUTEX] Some tests failed\n");
    }
}
