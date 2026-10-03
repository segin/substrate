/*
 * hello.c - a C program for the x86_64-unknown-substrate cross compiler
 * (tests/toolchain64/check.sh builds it statically and dynamically).
 * It uses what distinguishes the 64-bit ABI: 8-byte long and pointers,
 * a 64-bit division the compiler does inline, long double, and a
 * __thread variable, plus argv/envp and stdio.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static __thread long tls_counter = 40;

int main(int argc, char **argv) {
    unsigned long big = 0x123456789abcdefUL;
    long double ld = 1.5L;
    int bad = 0;

    tls_counter += 2;
    printf("hello from %s: sizeof(long)=%zu sizeof(void *)=%zu argc=%d\n",
           argc > 0 ? argv[0] : "?", sizeof(long), sizeof(void *), argc);
    printf("big/1000=%lu big%%1000=%lu ld*2=%d tls=%ld\n",
           big / 1000, big % 1000, (int)(ld * 2), tls_counter);

    if (sizeof(long) != 8 || sizeof(void *) != 8) bad = 1;
    if (big / 1000 != 81985529216486UL || big % 1000 != 895) bad = 1;
    if (tls_counter != 42 || (int)(ld * 2) != 3) bad = 1;

    char *copy = malloc(32);
    if (!copy) return 1;
    strncpy(copy, "heap works", 31);
    copy[31] = '\0';
    if (strcmp(copy, "heap works") != 0) bad = 1;
    free(copy);

    printf("hello: %s\n", bad ? "FAIL" : "OK");
    return bad;
}
