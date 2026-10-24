/*
 * __builtin_strlen and __builtin_strcmp of a string literal fold however
 * long it is, as gcc has them.  cc decoded the literal into 512 bytes and
 * called anything longer "not an integer constant": Linux's DRM module
 * parameter `debug` has a 700-byte description, and MODULE_INFO asserts
 * `sizeof(s) - 1 == __builtin_strlen(s)` of it.
 */
#include <stdio.h>

#define TEN "0123456789"
#define HUNDRED TEN TEN TEN TEN TEN TEN TEN TEN TEN TEN
#define LONG_STR HUNDRED HUNDRED HUNDRED HUNDRED HUNDRED HUNDRED "\t\tend\n"

_Static_assert(sizeof(LONG_STR) - 1 == __builtin_strlen(LONG_STR), "a 606-byte literal folds");
_Static_assert(__builtin_strlen(LONG_STR) == 606, "its length");
_Static_assert(__builtin_strcmp(LONG_STR "a", LONG_STR "b") < 0, "compared past 512 bytes");
_Static_assert(__builtin_strcmp(LONG_STR, LONG_STR) == 0, "equal");

static const int len = (int)__builtin_strlen(LONG_STR HUNDRED);

int main(void) {
    printf("%d %d\n", len, __builtin_strcmp(LONG_STR "b", LONG_STR "a") > 0);
    return 0;
}
