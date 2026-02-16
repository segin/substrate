#include <ctype.h>
#include <stdio.h>
#include <assert.h>
#include <limits.h>
#include <locale.h>

// Undefine host macros if they exist, so we can define our own renaming macros
#undef isalnum
#undef isalpha
#undef iscntrl
#undef isdigit
#undef isgraph
#undef islower
#undef isprint
#undef ispunct
#undef isspace
#undef isupper
#undef isxdigit
#undef tolower
#undef toupper

// Define macros to rename the functions being tested to libc_ prefix
#define isalnum libc_isalnum
#define isalpha libc_isalpha
#define iscntrl libc_iscntrl
#define isdigit libc_isdigit
#define isgraph libc_isgraph
#define islower libc_islower
#define isprint libc_isprint
#define ispunct libc_ispunct
#define isspace libc_isspace
#define isupper libc_isupper
#define isxdigit libc_isxdigit
#define tolower libc_tolower
#define toupper libc_toupper

// Forward declarations for renamed functions (to handle internal calls in ctype.c)
int libc_isalnum(int c);
int libc_isalpha(int c);
int libc_iscntrl(int c);
int libc_isdigit(int c);
int libc_isgraph(int c);
int libc_islower(int c);
int libc_isprint(int c);
int libc_ispunct(int c);
int libc_isspace(int c);
int libc_isupper(int c);
int libc_isxdigit(int c);
int libc_tolower(int c);
int libc_toupper(int c);

// Include the source file directly
#include "../../../lib/c/src/ctype.c"

// Undefine our renaming macros to restore access to host functions
#undef isalnum
#undef isalpha
#undef iscntrl
#undef isdigit
#undef isgraph
#undef islower
#undef isprint
#undef ispunct
#undef isspace
#undef isupper
#undef isxdigit
#undef tolower
#undef toupper

void check_func(int (*tested)(int), int (*reference)(int), const char *name) {
    printf("Testing %s...\n", name);
    // Test all unsigned char values and EOF
    for (int i = -1; i <= 255; i++) {
        int res_test = tested(i);
        int res_ref = reference(i);

        // For is* functions, the return value is non-zero for true, 0 for false.
        // The exact non-zero value might differ, so we normalize to boolean.
        if (name[0] == 'i' && name[1] == 's') {
             if (!!res_test != !!res_ref) {
                 fprintf(stderr, "FAIL: %s(%d): expected %d (bool), got %d\n",
                         name, i, !!res_ref, !!res_test);
                 // Print hex for debugging
                 fprintf(stderr, "  Input: 0x%x\n", i);
                 assert(0);
             }
        } else {
            // for to* functions, exact value matters
             if (res_test != res_ref) {
                 fprintf(stderr, "FAIL: %s(%d): expected %d, got %d\n",
                         name, i, res_ref, res_test);
                 assert(0);
             }
        }
    }
    printf("%s passed\n", name);
}

int main(void) {
    setlocale(LC_ALL, "C");
    check_func(libc_isalnum, isalnum, "isalnum");
    check_func(libc_isalpha, isalpha, "isalpha");
    check_func(libc_iscntrl, iscntrl, "iscntrl");
    check_func(libc_isdigit, isdigit, "isdigit");
    check_func(libc_isgraph, isgraph, "isgraph");
    check_func(libc_islower, islower, "islower");
    check_func(libc_isprint, isprint, "isprint");
    check_func(libc_ispunct, ispunct, "ispunct");
    check_func(libc_isspace, isspace, "isspace");
    check_func(libc_isupper, isupper, "isupper");
    check_func(libc_isxdigit, isxdigit, "isxdigit");
    check_func(libc_tolower, tolower, "tolower");
    check_func(libc_toupper, toupper, "toupper");

    printf("All ctype tests passed!\n");
    return 0;
}
