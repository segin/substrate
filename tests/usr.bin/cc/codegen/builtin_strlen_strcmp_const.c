/*
 * __builtin_strlen and __builtin_strcmp of string literals are integer
 * constants, as gcc folds them.  Linux's MODULE_INFO asserts
 * `sizeof(s) - 1 == __builtin_strlen(s)` in a _Static_assert, and its
 * scheduler picks a comparison with `!__builtin_strcmp(">", "<")`;
 * cc called the former "not an integer constant" and did not know the
 * latter at all.  Of anything else they are the library's functions.
 */
#include <stdio.h>

_Static_assert(sizeof("a" ":" "int") - 1 == __builtin_strlen("a" ":" "int"), "concatenated");
_Static_assert(__builtin_strlen("ab\0cd") == 2, "stops at an embedded NUL");
_Static_assert(__builtin_strlen("\x41\102") == 2, "escapes are one byte each");
_Static_assert(__builtin_strcmp("<", "<") == 0, "equal");
_Static_assert(__builtin_strcmp("<", "<=") < 0, "prefix is less");
_Static_assert(__builtin_strcmp(">", "<") > 0, "greater");

static int pick(int a, int b) {
    int r;

    if (!__builtin_strcmp(">", "<")) {
        r = a < b;
    } else if (!__builtin_strcmp(">", "<=")) {
        r = a <= b;
    } else if (!__builtin_strcmp(">", ">")) {
        r = a > b;
    } else {
        r = -1;
    }
    return r;
}

static int dyn(const char *a, const char *b) {
    return __builtin_strcmp(a, b) < 0 ? -7 : __builtin_strcmp(a, b) > 0 ? 7 : 0;
}

int main(int argc, char **argv) {
    printf("%d %d %d\n", pick(3, 2), pick(2, 3), (int)__builtin_strlen(argv[0]) > 0);
    printf("%d %d %d\n", dyn("ab", argc > 5 ? "x" : "ac"), dyn("b", argc > 5 ? "x" : "a"), dyn(argv[0], argv[0]));
    return 0;
}
