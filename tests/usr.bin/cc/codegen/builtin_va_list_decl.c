/*
 * `__builtin_va_list ap;` declares a variable in a block, as it does at
 * file scope and after typedef: Linux's <linux/stdarg.h> is
 * `typedef __builtin_va_list va_list;`, and code may name the builtin type
 * directly.  At the start of a block statement cc took it for an
 * expression: "expected ';' after expression".
 */
#include <stdio.h>

static int sum(int n, ...) {
    __builtin_va_list ap;
    int r = 0;

    __builtin_va_start(ap, n);
    while (n-- > 0) {
        r += __builtin_va_arg(ap, int);
    }
    __builtin_va_end(ap);
    return r;
}

int main(void) {
    printf("%d %d\n", sum(3, 4, 5, 6), sum(0));
    return 0;
}
