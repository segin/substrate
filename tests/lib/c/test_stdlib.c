#include <stdio.h>
#include <assert.h>
#include <string.h>

// Rename standard library functions to avoid conflicts with host libc
#define exit tested_exit
#define abort tested_abort
#define __stack_chk_fail tested_stack_chk_fail
#define malloc tested_malloc
#define free tested_free
#define calloc tested_calloc
#define realloc tested_realloc
#define aligned_alloc tested_aligned_alloc
#define quick_exit tested_quick_exit
#define at_quick_exit tested_at_quick_exit
#define strtol tested_strtol
#define atoi tested_atoi
#define atol tested_atol
#define atoll tested_atoll
#define atof tested_atof
#define getenv tested_getenv
#define system tested_system
#define abs tested_abs
#define labs tested_labs
#define llabs tested_llabs
#define qsort tested_qsort
#define bsearch tested_bsearch
#define rand tested_rand
#define srand tested_srand
#define arc4random_buf tested_arc4random_buf
#define arc4random tested_arc4random
#define arc4random_uniform tested_arc4random_uniform

// Include the source file directly
#include "../../../lib/c/src/stdlib.c"

// Undefine macros to allow testing
#undef atoi
#undef atol

void test_atoi_basic(void) {
    assert(tested_atoi("123") == 123);
    assert(tested_atoi("-123") == -123);
    assert(tested_atoi("0") == 0);
    assert(tested_atoi("-0") == 0);
    printf("test_atoi_basic passed\n");
}

void test_atoi_whitespace(void) {
    assert(tested_atoi("  123") == 123);
    assert(tested_atoi("\t\n 456") == 456);
    printf("test_atoi_whitespace passed\n");
}

void test_atoi_sign(void) {
    assert(tested_atoi("+123") == 123);
    assert(tested_atoi("-456") == -456);
    printf("test_atoi_sign passed\n");
}

void test_atoi_invalid(void) {
    assert(tested_atoi("abc") == 0);
    assert(tested_atoi("12abc") == 12);
    assert(tested_atoi("-12abc") == -12);
    printf("test_atoi_invalid passed\n");
}

void test_atol_basic(void) {
    assert(tested_atol("1234567890") == 1234567890L);
    assert(tested_atol("-1234567890") == -1234567890L);
    printf("test_atol_basic passed\n");
}

void test_llabs(void) {
    assert(tested_llabs(0) == 0);
    assert(tested_llabs(1) == 1);
    assert(tested_llabs(-1) == 1);
    assert(tested_llabs(123456789012345LL) == 123456789012345LL);
    assert(tested_llabs(-123456789012345LL) == 123456789012345LL);
    printf("test_llabs passed\n");
}

int main(void) {
    printf("Running stdlib tests...\n");
    test_atoi_basic();
    test_atoi_whitespace();
    test_atoi_sign();
    test_atoi_invalid();
    test_atol_basic();
    test_llabs();
    printf("All tests passed!\n");
    return 0;
}
