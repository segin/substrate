#include <vm/vm_map.h>
#include <sys/proc.h>
#include <kern/console.h>
#include <sys/mman.h>

extern void *sys_brk(void *addr);

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST_ASSERT(cond, msg) do { \
    if (!(cond)) { \
        kprint("FAIL: "); kprint(msg); kprint("\n"); \
        tests_failed++; \
        return; \
    } \
    tests_passed++; \
} while(0)

void test_brk_query(void) {
    kprint("Test: brk query\n");
    void *brk = sys_brk(NULL);
    TEST_ASSERT(brk != NULL, "sys_brk(NULL) returned valid address");
    TEST_ASSERT((uintptr_t)brk == current_process->brk_start, "Initial brk is at brk_start");
    kprint("  PASS\n");
}

void test_brk_expand(void) {
    kprint("Test: brk expand\n");
    void *old_brk = sys_brk(NULL);
    uintptr_t new_brk_val = (uintptr_t)old_brk + 0x2000;

    void *new_brk = sys_brk((void *)new_brk_val);
    TEST_ASSERT(new_brk != NULL, "sys_brk(expand) returned valid address");
    TEST_ASSERT((uintptr_t)new_brk == new_brk_val, "sys_brk returned expected new break");
    TEST_ASSERT(current_process->brk == new_brk_val, "Process brk was updated");

    kprint("  PASS\n");
}

void test_brk_shrink(void) {
    kprint("Test: brk shrink\n");
    void *old_brk = sys_brk(NULL);
    uintptr_t new_brk_val = (uintptr_t)old_brk - 0x1000;

    void *new_brk = sys_brk((void *)new_brk_val);
    TEST_ASSERT(new_brk != NULL, "sys_brk(shrink) returned valid address");
    TEST_ASSERT((uintptr_t)new_brk == new_brk_val, "sys_brk returned expected new break after shrink");
    TEST_ASSERT(current_process->brk == new_brk_val, "Process brk was updated after shrink");

    kprint("  PASS\n");
}

void test_brk_below_start(void) {
    kprint("Test: brk below start\n");
    void *old_brk = sys_brk(NULL);
    uintptr_t new_brk_val = current_process->brk_start - 0x1000;

    void *new_brk = sys_brk((void *)new_brk_val);
    TEST_ASSERT(new_brk != NULL, "sys_brk(below start) returned valid address");
    TEST_ASSERT((uintptr_t)new_brk == (uintptr_t)old_brk, "sys_brk ignored request below brk_start");

    kprint("  PASS\n");
}

void run_brk_tests(void) {
    kprint("\n=== BRK Unit Tests ===\n");

    // tests are dependent on valid current_process with a brk_start
    if (!current_process) {
        kprint("SKIP: No current_process for brk tests\n");
        return;
    }
    if (current_process->brk_start == 0) {
        current_process->brk_start = 0x40000000; // Mock a brk_start
        current_process->brk = 0x40000000;
    }

    test_brk_query();
    test_brk_expand();
    test_brk_shrink();
    test_brk_below_start();

    char buf[64];
    extern int sprintf(char *, const char *, ...);
    sprintf(buf, "Passed: %d, Failed: %d\n", tests_passed, tests_failed);
    kprint(buf);
}
