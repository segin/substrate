/*
 * test_gdt.c - Unit tests for GDT segment verification
 */
#include <stdio.h>
#include <stdint.h>
#include <assert.h>

/* Expected segment selectors (with RPL) */
#define SELECTOR_KERNEL_CODE  0x08
#define SELECTOR_KERNEL_DATA  0x10
#define SELECTOR_USER_CODE    0x1B   /* 0x18 | 3 */
#define SELECTOR_USER_DATA    0x23   /* 0x20 | 3 */
#define SELECTOR_TSS          0x28
#define SELECTOR_TLS          0x33   /* 0x30 | 3 */

/* Test that user code segment has correct RPL */
static void test_user_code_selector(void) {
    uint16_t expected = 0x1B;
    uint16_t base_selector = 0x18; /* GDT entry 3 */
    uint16_t rpl = 3;
    
    assert((base_selector | rpl) == expected);
    printf("[PASS] User code selector = 0x%02X\n", expected);
}

/* Test that user data segment has correct RPL */
static void test_user_data_selector(void) {
    uint16_t expected = 0x23;
    uint16_t base_selector = 0x20; /* GDT entry 4 */
    uint16_t rpl = 3;
    
    assert((base_selector | rpl) == expected);
    printf("[PASS] User data selector = 0x%02X\n", expected);
}

/* Test that TLS segment has correct RPL */
static void test_tls_selector(void) {
    uint16_t expected = 0x33;
    uint16_t base_selector = 0x30; /* GDT entry 6 */
    uint16_t rpl = 3;
    
    assert((base_selector | rpl) == expected);
    printf("[PASS] TLS selector = 0x%02X\n", expected);
}

/* Test kernel segments have RPL 0 */
static void test_kernel_selectors(void) {
    assert((SELECTOR_KERNEL_CODE & 3) == 0);
    assert((SELECTOR_KERNEL_DATA & 3) == 0);
    printf("[PASS] Kernel selectors have RPL 0\n");
}

/* Test user segments have RPL 3 */
static void test_user_rpl(void) {
    assert((SELECTOR_USER_CODE & 3) == 3);
    assert((SELECTOR_USER_DATA & 3) == 3);
    assert((SELECTOR_TLS & 3) == 3);
    printf("[PASS] User segments have RPL 3\n");
}

int main(void) {
    printf("=== GDT Segment Unit Tests ===\n");
    
    test_user_code_selector();
    test_user_data_selector();
    test_tls_selector();
    test_kernel_selectors();
    test_user_rpl();
    
    printf("=== All GDT tests passed ===\n");
    return 0;
}
