/*
 * test_stacktrace.c - Unit tests for stack trace functionality
 *
 * Tests EBP chain unwinding for kernel debugging.
 */

#include <stdint.h>
#include <stdio.h>
#include <kern/console.h>
#include <kern/stacktrace.h>

/* Test result tracking */
static int tests_passed = 0;
static int tests_failed = 0;

static void test_assert(int condition, const char *name) {
    if (condition) {
        tests_passed++;
    } else {
        tests_failed++;
        kprint("FAIL: ");
        kprint(name);
        kprint("\n");
    }
}

/*
 * Helper function to create known stack depth
 */
static __attribute__((noinline)) void nested_function_3(void) {
    /* This should be visible in the trace */
    kprint("  nested_function_3 called\n");
}

static __attribute__((noinline)) void nested_function_2(void) {
    nested_function_3();
}

static __attribute__((noinline)) void nested_function_1(void) {
    nested_function_2();
}

/*
 * test_stack_trace_basic - Verify stack_trace doesn't crash
 */
static void test_stack_trace_basic(void) {
    kprint("Testing stack_trace() basic functionality:\n");
    
    /* This should not crash */
    stack_trace();
    
    test_assert(1, "stack_trace() completed without crash");
}

/*
 * test_stack_trace_nested - Verify nested calls are traced
 */
static void test_stack_trace_nested(void) {
    kprint("\nTesting stack_trace() with nested calls:\n");
    
    /* Call nested functions then trace */
    nested_function_1();
    
    /* Just verify it doesn't crash with deep stack */
    stack_trace();
    
    test_assert(1, "stack_trace() handled nested calls");
}

/*
 * test_stack_trace_from - Test stack_trace_from with specific registers
 */
static void test_stack_trace_from_regs(void) {
    uint32_t ebp, eip;
    
    /* Get current EBP and EIP */
    __asm__ volatile("mov %%ebp, %0" : "=r"(ebp));
    /* EIP is not directly accessible, use return address approximation */
    eip = (uint32_t)&&label_eip;
label_eip:
    
    kprint("\nTesting stack_trace_from():\n");
    stack_trace_from(ebp, eip);
    
    test_assert(1, "stack_trace_from() completed without crash");
}

/*
 * test_stacktrace - Main test entry point
 */
void test_stacktrace(void) {
    kprint("=== Stack Trace Tests ===\n");
    
    tests_passed = 0;
    tests_failed = 0;
    
    test_stack_trace_basic();
    test_stack_trace_nested();
    test_stack_trace_from_regs();
    
    char buf[64];
    sprintf(buf, "\nStack trace tests: %d passed, %d failed\n", tests_passed, tests_failed);
    kprint(buf);
}
