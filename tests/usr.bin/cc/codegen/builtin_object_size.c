/*
 * __builtin_object_size(p, 0): the bytes left in the object p points
 * into, from p to its end, or (size_t)-1 when that object is not known
 * (mode 2: 0).  It gave the size of the expression itself -- 8 for
 * dst + 7 into an 8-byte array, where 1 byte is left, so a
 * __builtin___memcpy_chk past the end was let through; 8 for a pointer
 * from malloc, so a checked copy of more than 8 bytes through one
 * aborted the program -- or of the whole row for a pointer into a
 * two-dimensional array.
 */
#include <stdio.h>
#include <stdlib.h>

struct s {
    int a;
    char buf[16];
};

#define P(e) printf("%s=%ld\n", #e, (long)__builtin_object_size(e, 0))

/**
 * Print the object size each pointer gets, then copy through a pointer
 * whose object is not known.
 *
 * @return 0
 */
int main(void) {
    char dst[8];
    int x;
    long arr[3][4];
    char *heap = malloc(100);
    char src[50] = "hello";
    struct s v;
    struct s *pv = &v;

    P(dst);
    P(dst + 1);
    P(&dst[2]);
    P(&dst);
    P(&x);
    P(dst + 7);
    P(arr[1]);
    P(&arr[2][1]);
    P(arr);
    P(heap);
    P(pv);
    P(v.buf);
    printf("heap mode 2=%ld\n", (long)__builtin_object_size(heap, 2));
    __builtin___memcpy_chk(heap, src, sizeof src, __builtin_object_size(heap, 0));
    printf("copied %s\n", heap);
    free(heap);
    return 0;
}
