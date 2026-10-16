/*
 * __builtin_object_size(p, 0): the bytes left in the object p points
 * into, from p to its end.  It gave the size of the expression itself --
 * 8 for dst + 7 into an 8-byte array, where 1 byte is left, so a
 * __builtin___memcpy_chk past the end was let through -- or of the
 * whole row for a pointer into a two-dimensional array.
 */
#include <stdio.h>

#define P(e) printf("%s=%ld\n", #e, (long)__builtin_object_size(e, 0))

/**
 * Print the object size each pointer gets.
 *
 * @return 0
 */
int main(void) {
    char dst[8];
    int x;
    long arr[3][4];

    P(dst);
    P(dst + 1);
    P(&dst[2]);
    P(&dst);
    P(&x);
    P(dst + 7);
    P(arr[1]);
    P(&arr[2][1]);
    P(arr);
    return 0;
}
