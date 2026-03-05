#undef bsearch
#undef qsort
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <limits.h>

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
#undef qsort
#define qsort tested_qsort
#undef bsearch
#define bsearch tested_bsearch
#define rand tested_rand
#define srand tested_srand
#define arc4random_buf tested_arc4random_buf
#define arc4random tested_arc4random
#define arc4random_uniform tested_arc4random_uniform
#define getopt tested_getopt

// Include the source file directly
#include "../../../lib/c/src/stdlib.c"

// Undefine macros to allow testing
#undef abs
#undef strtol

void test_strtol(void) {
    // Basic base 10
    assert(tested_strtol("123", NULL, 10) == 123);
    assert(tested_strtol("-123", NULL, 10) == -123);
    assert(tested_strtol("+123", NULL, 10) == 123);

    // Whitespace
    assert(tested_strtol("  123", NULL, 10) == 123);
    assert(tested_strtol("\t\n 456", NULL, 10) == 456);

    // Base 16
    assert(tested_strtol("1A", NULL, 16) == 26);
    assert(tested_strtol("1a", NULL, 16) == 26);
    assert(tested_strtol("0x1A", NULL, 16) == 26);
    assert(tested_strtol("-0x1A", NULL, 16) == -26);

    // Base 8
    assert(tested_strtol("10", NULL, 8) == 8);
    assert(tested_strtol("77", NULL, 8) == 63);

    // Base 36 (max base)
    assert(tested_strtol("Z", NULL, 36) == 35);
    assert(tested_strtol("z", NULL, 36) == 35);
    assert(tested_strtol("10", NULL, 36) == 36);

    // Auto-detect base (0)
    assert(tested_strtol("123", NULL, 0) == 123); // Decimal
    assert(tested_strtol("010", NULL, 0) == 8);   // Octal
    assert(tested_strtol("0x1A", NULL, 0) == 26); // Hex
    assert(tested_strtol("0X1A", NULL, 0) == 26); // Hex upper

    // Endptr
    char *endptr;
    const char *str = "123xyz";
    assert(tested_strtol(str, &endptr, 10) == 123);
    assert(*endptr == 'x');
    assert(endptr == str + 3);

    str = "  123   ";
    assert(tested_strtol(str, &endptr, 10) == 123);
    assert(*endptr == ' ');

    // Invalid input
    str = "xyz";
    assert(tested_strtol(str, &endptr, 10) == 0);
    assert(endptr == str); // No conversion performed

    // Invalid base
    assert(tested_strtol("123", NULL, 1) == 0);
    assert(tested_strtol("123", NULL, 37) == 0);

    // Overflow/Underflow (assuming 32-bit long as per implementation)
    // Implementation uses simplified LONG_MAX/MIN: 2147483647L, -2147483648L
    // Note: The implementation returns these values on overflow.
    assert(tested_strtol("2147483648", NULL, 10) == 2147483647L);
    assert(tested_strtol("-2147483649", NULL, 10) == -2147483648L);

    printf("test_strtol passed\n");
}

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

void test_abs(void) {
    assert(tested_abs(0) == 0);
    assert(tested_abs(1) == 1);
    assert(tested_abs(-1) == 1);
    assert(tested_abs(10) == 10);
    assert(tested_abs(-10) == 10);
    assert(tested_abs(12345) == 12345);
    assert(tested_abs(-12345) == 12345);
    assert(tested_abs(INT_MAX) == INT_MAX);
    assert(tested_abs(-INT_MAX - 1) == (-INT_MAX - 1));
    printf("test_abs passed\n");
}

void test_labs(void) {
    assert(tested_labs(10L) == 10L);
    assert(tested_labs(-10L) == 10L);
    assert(tested_labs(0L) == 0L);
    assert(tested_labs(LONG_MAX) == LONG_MAX);
    printf("test_labs passed\n");
}

void test_llabs(void) {
    assert(tested_llabs(10LL) == 10LL);
    assert(tested_llabs(-10LL) == 10LL);
    assert(tested_llabs(0LL) == 0LL);
    assert(tested_llabs(LLONG_MAX) == LLONG_MAX);
    assert(tested_llabs(-LLONG_MAX) == LLONG_MAX);
    printf("test_llabs passed\n");
}

extern char *optarg;
extern int optind, opterr, optopt;

static void reset_getopt_state(void) {
    optarg = NULL;
    optind = 1;
    opterr = 0; // Disable printing to stderr during tests
    optopt = 0;
}

void test_getopt_basic(void) {
    reset_getopt_state();
    char *argv[] = {"prog", "-a", "-b", NULL};
    int argc = 3;

    assert(tested_getopt(argc, argv, "ab") == 'a');
    assert(tested_getopt(argc, argv, "ab") == 'b');
    assert(tested_getopt(argc, argv, "ab") == -1);
    printf("test_getopt_basic passed\n");
}

void test_getopt_with_args(void) {
    reset_getopt_state();
    char *argv[] = {"prog", "-c", "foo", "-d", "bar", NULL};
    int argc = 5;

    assert(tested_getopt(argc, argv, "c:d:") == 'c');
    assert(optarg != NULL && strcmp(optarg, "foo") == 0);
    assert(tested_getopt(argc, argv, "c:d:") == 'd');
    assert(optarg != NULL && strcmp(optarg, "bar") == 0);
    assert(tested_getopt(argc, argv, "c:d:") == -1);

    // combined options, args follow
    reset_getopt_state();
    char *argv2[] = {"prog", "-xc", "foo", NULL};
    int argc2 = 3;
    assert(tested_getopt(argc2, argv2, "xc:") == 'x');
    assert(tested_getopt(argc2, argv2, "xc:") == 'c');
    assert(optarg != NULL && strcmp(optarg, "foo") == 0);
    assert(tested_getopt(argc2, argv2, "xc:") == -1);

    // arg immediately follows option char
    reset_getopt_state();
    char *argv3[] = {"prog", "-cfoo", NULL};
    int argc3 = 2;
    assert(tested_getopt(argc3, argv3, "c:") == 'c');
    assert(optarg != NULL && strcmp(optarg, "foo") == 0);
    assert(tested_getopt(argc3, argv3, "c:") == -1);

    printf("test_getopt_with_args passed\n");
}

void test_getopt_errors(void) {
    reset_getopt_state();
    char *argv[] = {"prog", "-x", NULL};
    int argc = 2;

    // Unknown option
    assert(tested_getopt(argc, argv, "ab") == '?');
    assert(optopt == 'x');

    // Missing argument
    reset_getopt_state();
    char *argv2[] = {"prog", "-c", NULL};
    int argc2 = 2;
    assert(tested_getopt(argc2, argv2, "c:") == '?');
    assert(optopt == 'c');

    printf("test_getopt_errors passed\n");
}

void test_getopt_end_of_options(void) {
    reset_getopt_state();
    char *argv[] = {"prog", "-a", "--", "-b", NULL};
    int argc = 4;

    assert(tested_getopt(argc, argv, "ab") == 'a');
    assert(tested_getopt(argc, argv, "ab") == -1);
    assert(optind == 3); // points to "-b" now

    reset_getopt_state();
    char *argv2[] = {"prog", "-a", "nonopt", "-b", NULL};
    int argc2 = 4;
    assert(tested_getopt(argc2, argv2, "ab") == 'a');
    assert(tested_getopt(argc2, argv2, "ab") == -1); // Stops parsing when it hits "nonopt"
    assert(optind == 2); // points to "nonopt"

    printf("test_getopt_end_of_options passed\n");
}

void test_realloc_edge_cases(void) {
    // Test realloc(NULL, size) behaves like malloc
    void *ptr1 = tested_realloc(NULL, 128);
    assert(ptr1 != NULL);
    struct block_meta *block1 = (struct block_meta *)ptr1 - 1;
    assert(block1->free == 0);

    // Test realloc(ptr, 0) behaves like free and returns NULL
    void *ptr2 = tested_realloc(ptr1, 0);
    assert(ptr2 == NULL);
    assert(block1->free == 1);

    printf("test_realloc_edge_cases passed\n");
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
    test_getopt_basic();
    test_getopt_with_args();
    test_getopt_errors();
    test_getopt_end_of_options();
    test_realloc_edge_cases();
    printf("All tests passed!\n");
    return 0;
}
