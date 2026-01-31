/*
 * fuzz_gdt.c - Fuzzing tests for GDT selector validation
 *
 * Generates random selector values and verifies:
 * 1. RPL extraction is correct
 * 2. Index extraction is correct
 * 3. Invalid selectors are detected
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

#define NUM_FUZZ_ITERATIONS 10000

/* Valid GDT indices */
#define MAX_GDT_INDEX 6

/* Extract RPL from selector */
static uint8_t get_rpl(uint16_t selector) {
    return selector & 0x03;
}

/* Extract index from selector */
static uint8_t get_index(uint16_t selector) {
    return selector >> 3;
}

/* Validate selector is within GDT bounds */
static int is_valid_selector(uint16_t selector) {
    uint8_t index = get_index(selector);
    return (index <= MAX_GDT_INDEX);
}

/* Fuzz test: Random selector generation */
static void fuzz_selector_extraction(void) {
    int passed = 0;
    int failed = 0;
    
    for (int i = 0; i < NUM_FUZZ_ITERATIONS; i++) {
        uint16_t selector = rand() % 0xFFFF;
        
        uint8_t rpl = get_rpl(selector);
        uint8_t index = get_index(selector);
        
        /* Verify RPL is always 0-3 */
        if (rpl > 3) {
            failed++;
            continue;
        }
        
        /* Verify index extraction is reversible */
        uint16_t reconstructed = (index << 3) | rpl;
        if ((selector & 0x7FF) != reconstructed) {
            failed++;
            continue;
        }
        
        passed++;
    }
    
    printf("[FUZZ] selector_extraction: %d passed, %d failed\n", passed, failed);
}

/* Fuzz test: Valid user selectors */
static void fuzz_user_selectors(void) {
    uint16_t valid_user[] = {0x1B, 0x23, 0x33};
    int all_valid = 1;
    
    for (int i = 0; i < 3; i++) {
        if (!is_valid_selector(valid_user[i])) {
            all_valid = 0;
            printf("[FAIL] Valid user selector 0x%02X rejected\n", valid_user[i]);
        }
        if (get_rpl(valid_user[i]) != 3) {
            all_valid = 0;
            printf("[FAIL] User selector 0x%02X has wrong RPL\n", valid_user[i]);
        }
    }
    
    if (all_valid) {
        printf("[FUZZ] user_selectors: All valid\n");
    }
}

/* Fuzz test: Invalid selectors (out of bounds) */
static void fuzz_invalid_selectors(void) {
    int detected = 0;
    
    for (int i = 0; i < 100; i++) {
        /* Generate selector with index > MAX_GDT_INDEX */
        uint16_t selector = ((MAX_GDT_INDEX + 1 + i) << 3) | (rand() % 4);
        
        if (!is_valid_selector(selector)) {
            detected++;
        }
    }
    
    printf("[FUZZ] invalid_selectors: %d/100 detected\n", detected);
}

int main(void) {
    printf("=== GDT Fuzz Tests ===\n");
    
    srand(12345); /* Fixed seed for reproducibility */
    
    fuzz_selector_extraction();
    fuzz_user_selectors();
    fuzz_invalid_selectors();
    
    printf("=== GDT Fuzz Tests Complete ===\n");
    return 0;
}
