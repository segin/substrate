#include <stdio.h>
#include <stdint.h>
#include <assert.h>
#include <limits.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>
#include <ctype.h>

// Renames to avoid conflict with host libc and test our implementation.
// Note: Renaming 'free' also renames the 'free' member in 'struct block_meta'.
#define malloc tested_malloc
#define free tested_free
#define calloc tested_calloc
#define realloc tested_realloc
#define atoi tested_atoi
#define atol tested_atol
#define atoll tested_atoll
#define strtol tested_strtol
#define strtoll tested_strtoll
#define strtoul tested_strtoul
#define strtoull tested_strtoull
#define abs tested_abs
#define labs tested_labs
#define llabs tested_llabs
#define div tested_div
#define ldiv tested_ldiv
#define lldiv tested_lldiv

// Include source file directly to test internal allocator state.
#include "../../../lib/c/src/stdlib.c"

#undef malloc
#undef free
#undef calloc
#undef realloc
#undef atoi
#undef atol
#undef atoll
#undef strtol
#undef strtoll
#undef strtoul
#undef strtoull
#undef abs
#undef labs
#undef llabs
#undef div
#undef ldiv
#undef lldiv

void test_abs(void) {
    printf("Testing abs...\n");
    assert(tested_abs(5) == 5);
    assert(tested_abs(-5) == 5);
    assert(tested_abs(0) == 0);
    assert(tested_abs(INT_MAX) == INT_MAX);
    assert(tested_abs(INT_MIN + 1) == -(INT_MIN + 1));
}

void test_labs(void) {
    printf("Testing labs...\n");
    assert(tested_labs(5L) == 5L);
    assert(tested_labs(-5L) == 5L);
    assert(tested_labs(0L) == 0L);
    assert(tested_labs(LONG_MAX) == LONG_MAX);
    assert(tested_labs(LONG_MIN + 1) == -(LONG_MIN + 1));
}

void test_llabs(void) {
    printf("Testing llabs...\n");
    assert(tested_llabs(5LL) == 5LL);
    assert(tested_llabs(-5LL) == 5LL);
    assert(tested_llabs(0LL) == 0LL);
    assert(tested_llabs(LLONG_MAX) == LLONG_MAX);
    assert(tested_llabs(LLONG_MIN + 1) == -(LLONG_MIN + 1));
}

void test_div(void) {
    printf("Testing div...\n");
    div_t r;

    r = tested_div(10, 3);
    assert(r.quot == 3);
    assert(r.rem == 1);

    r = tested_div(-10, 3);
    assert(r.quot == -3);
    assert(r.rem == -1);

    r = tested_div(10, -3);
    assert(r.quot == -3);
    assert(r.rem == 1);

    r = tested_div(-10, -3);
    assert(r.quot == 3);
    assert(r.rem == -1);

    r = tested_div(0, 5);
    assert(r.quot == 0);
    assert(r.rem == 0);
}

void test_ldiv(void) {
    printf("Testing ldiv...\n");
    ldiv_t r;

    r = tested_ldiv(10L, 3L);
    assert(r.quot == 3L);
    assert(r.rem == 1L);

    r = tested_ldiv(-10L, 3L);
    assert(r.quot == -3L);
    assert(r.rem == -1L);
}

void test_lldiv(void) {
    printf("Testing lldiv...\n");
    lldiv_t r;

    r = tested_lldiv(10LL, 3LL);
    assert(r.quot == 3LL);
    assert(r.rem == 1LL);

    r = tested_lldiv(-10LL, 3LL);
    assert(r.quot == -3LL);
    assert(r.rem == -1LL);
}

void test_atoi_basic(void) {
    printf("Testing atoi basic...\n");
    assert(tested_atoi("123") == 123);
    assert(tested_atoi("0") == 0);
    assert(tested_atoi("-456") == -456);
}

void test_atoi_whitespace(void) {
    printf("Testing atoi whitespace...\n");
    assert(tested_atoi("   123") == 123);
    assert(tested_atoi("\t456") == 456);
}

void test_atoi_sign(void) {
    printf("Testing atoi sign...\n");
    assert(tested_atoi("+123") == 123);
    assert(tested_atoi("-123") == -123);
}

void test_atoi_invalid(void) {
    printf("Testing atoi invalid...\n");
    assert(tested_atoi("abc") == 0);
    assert(tested_atoi("123abc") == 123);
}

void test_atol_basic(void) {
    printf("Testing atol basic...\n");
    assert(tested_atol("1234567890") == 1234567890L);
    assert(tested_atol("-1234567890") == -1234567890L);
}

void test_strtol(void) {
    printf("Testing strtol...\n");
    char *endptr;
    assert(tested_strtol("123", &endptr, 10) == 123L);
    assert(*endptr == '\0');

    assert(tested_strtol("  -123", &endptr, 10) == -123L);
    assert(*endptr == '\0');

    assert(tested_strtol("0x123", &endptr, 16) == 0x123L);
    assert(*endptr == '\0');

    assert(tested_strtol("0123", &endptr, 8) == 0123L);
    assert(*endptr == '\0');

    assert(tested_strtol("123abc", &endptr, 10) == 123L);
    assert(strcmp(endptr, "abc") == 0);
}

void test_realloc_zero_size(void) {
    printf("Testing realloc zero size...\n");
    void *ptr = tested_malloc(100);
    assert(ptr != NULL);
    void *new_ptr = tested_realloc(ptr, 0);
    assert(new_ptr == NULL);
}

void test_realloc_edge_cases(void) {
    printf("Testing realloc edge cases...\n");
    // Test realloc(NULL, size) behaves like malloc
    void *ptr1 = tested_realloc(NULL, 128);
    assert(ptr1 != NULL);
    struct block_meta *block1 = (struct block_meta *)ptr1 - 1;
    // Renaming 'free' renames the member to 'tested_free'
    assert(block1->tested_free == 0);

    // Test realloc(ptr, 0) behaves like free and returns NULL
    void *ptr2 = tested_realloc(ptr1, 0);
    assert(ptr2 == NULL);
    assert(block1->tested_free == 1);
}

void test_realloc(void) {
    printf("Testing realloc...\n");
    void *ptr = tested_malloc(10);
    assert(ptr != NULL);

    // realloc with size 0 should free the pointer and return NULL
    void *new_ptr = tested_realloc(ptr, 0);
    assert(new_ptr == NULL);
}

void test_calloc(void) {
    printf("Testing calloc...\n");
    // Test basic allocation
    int *arr = (int *)tested_calloc(4, sizeof(int));
    assert(arr != NULL);
    for (int i = 0; i < 4; i++) {
        assert(arr[i] == 0);
    }
    tested_free(arr);

    // Test zero allocation
    void *p = tested_calloc(0, 10);
    assert(p == NULL);

    p = tested_calloc(10, 0);
    assert(p == NULL);

    // Test overflow
    assert(tested_calloc(SIZE_MAX, 2) == NULL);
    assert(tested_calloc(2, SIZE_MAX) == NULL);
    assert(tested_calloc(SIZE_MAX / 2 + 1, 2) == NULL);
    assert(tested_calloc(SIZE_MAX, SIZE_MAX) == NULL);
    assert(tested_calloc(SIZE_MAX / 4, 5) == NULL);
}

void test_calloc_overflow(void) {
    printf("Testing calloc overflow...\n");
    // Normal allocation
    size_t num = 10;
    size_t size = sizeof(int);
    int *ptr = (int *)tested_calloc(num, size);
    assert(ptr != NULL);

    // Verify memory is zeroed
    for (size_t i = 0; i < num; i++) {
        assert(ptr[i] == 0);
    }
    tested_free(ptr);

    // Overflow allocation
    void *ptr2 = tested_calloc(SIZE_MAX, 2);
    assert(ptr2 == NULL);

    void *ptr3 = tested_calloc(2, SIZE_MAX);
    assert(ptr3 == NULL);

    size_t half_max = SIZE_MAX / 2;
    void *ptr4 = tested_calloc(half_max + 1, 2);
    assert(ptr4 == NULL);

    void *ptr5 = tested_calloc(2, half_max + 1);
    assert(ptr5 == NULL);

    // Zero allocations
    void *ptr6 = tested_calloc(0, 10);
    assert(ptr6 == NULL); // implementation returns NULL for 0 size

    void *ptr7 = tested_calloc(10, 0);
    assert(ptr7 == NULL); // implementation returns NULL for 0 size
}

int main(void) {
    printf("Running stdlib tests...\n");
    test_atoi_basic();
    test_atoi_whitespace();
    test_atoi_sign();
    test_atoi_invalid();
    test_atol_basic();
    test_strtol();
    test_abs();
    test_labs();
    test_llabs();
    test_div();
    test_ldiv();
    test_lldiv();
    test_realloc_zero_size();
    test_realloc_edge_cases();
    test_realloc();
    test_calloc();
    test_calloc_overflow();
    printf("All stdlib tests passed!\n");
    return 0;
}
