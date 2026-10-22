/*
 * A builtin named after a C library function that cc does not lower
 * itself is a call to that function, as gcc makes it: __builtin_memcmp was
 * a call to a function named `__builtin_memcmp`, which nothing defines
 * (Linux's init/main.o, at the vmlinux link).
 */
#include <stdio.h>

int main(int argc, char **argv) {
    char a[4] = "abc";
    char b[4] = "abd";

    (void)argv;
    printf("%d %d %d\n", __builtin_memcmp(a, b, 2) == 0, __builtin_memcmp(a, b, 3 + (argc - 1)) < 0,
           __builtin_memcmp(b, a, 3) > 0);
    return 0;
}
