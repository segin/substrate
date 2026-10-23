/*
 * A block-scope static array of pointers is laid out as data when every
 * name in its initializer is of file scope -- a string table, a table of
 * file-scope arrays -- as gcc lays it out: Linux's vga_probe() has both,
 * and cc made each a guard and code to fill it on the first call.  One
 * naming a block-scope static still gets that static, not a file-scope
 * object of the same name.  run_static_local_tables.sh checks that the
 * first two have no guard.
 */
#include <stdio.h>

static int cga[3] = {1, 2, 3};
static int ega[2] = {4, 5};
static int shadowed[1] = {99};

static const char *card(int i) {
    static const char *names[] = {"CGA/MDA/HGC", "EGA", "VGA"};

    return names[i];
}

static int first(int i) {
    static int *lists[] = {cga, ega, cga + 1};

    return *lists[i];
}

static int inner(void) {
    static int shadowed[1] = {7};
    static int *mine[] = {shadowed};

    shadowed[0]++;
    return *mine[0];
}

int main(void) {
    int i;
    int a;
    int b;

    for (i = 0; i < 3; ++i) {
        printf("%s %d\n", card(i), first(i));
    }
    a = inner();
    b = inner();
    printf("%d %d %d\n", a, b, shadowed[0]);
    return 0;
}
