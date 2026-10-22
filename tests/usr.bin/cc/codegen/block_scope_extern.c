/*
 * A block-scope `extern` declares the global and owns no storage, so its
 * type may be incomplete: Linux's free_initmem_default() has
 * `extern char __init_begin[], __init_end[];` inside the function.  cc
 * reserved stack space for every such declaration, and stopped on one
 * whose size it could not know.  Reads and writes reach the global.
 */
#include <stdio.h>

char marks[4] = "abc";
int counter = 5;
struct pt { int x, y; } origin = { 3, 4 };

static int f(void) {
    extern char marks[];
    extern int counter;
    extern struct pt origin;

    counter += 1;
    origin.x = 9;
    marks[0] = 'Z';
    return marks[1] + counter + origin.y;
}

static long span(void) {
    extern char marks[];
    extern char marks_end[];

    return marks_end - marks;
}

char marks_end[1];

int main(void) {
    extern char marks[4];
    int r = f();

    printf("%d %d %d %c %c\n", r, counter, origin.x, marks[0], marks[2]);
    printf("%d\n", span() != 0);
    return 0;
}
