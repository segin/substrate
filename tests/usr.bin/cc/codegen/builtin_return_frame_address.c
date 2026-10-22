/*
 * __builtin_return_address and __builtin_frame_address, and
 * __builtin_extract_return_addr over them.  cc emitted the first two as
 * calls to functions nothing defines and did not know the third; Linux
 * uses all three (_RET_IP_, vsprintf's %pS).  A frame address is the
 * frame pointer N frames up; a return address the word above it.
 */
#include <stdio.h>

static void *where;
static void *frame0;
static void *frame1;

static __attribute__((noinline)) void note(void) {
    where = __builtin_extract_return_addr(__builtin_return_address(0));
    frame1 = __builtin_frame_address(1);
}

static __attribute__((noinline)) int run(void) {
    char local;

    frame0 = __builtin_frame_address(0);
    note();
    return (char *)frame0 > &local && (char *)frame0 - &local < 4096;
}

int main(void) {
    int x = 3;
    void *p = __builtin_extract_return_addr(&x);
    int local_below = run();

    printf("%d %d\n", p == (void *)&x, local_below);
    printf("%d %d\n", (char *)where > (char *)run && (char *)where < (char *)run + 4096, frame1 == frame0);
    return 0;
}
