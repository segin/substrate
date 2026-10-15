/*
 * Calls through `*`: `(*fp)()` calls fp whatever fp's function returns,
 * including a pointer, and however many `*`s are written; a pointer to a
 * function pointer needs one real load.
 */
#include <stdio.h>

static int seven = 7;
static int eight = 8;

static int *get_seven(void) {
    return &seven;
}

static int *get_eight(void) {
    return &eight;
}

static char **get_strs(void) {
    static char *strs[] = {"zero", "one"};
    return strs;
}

static int add1(int x) {
    return x + 1;
}

static int (*get_add1(void))(int) {
    return add1;
}

static int via_param(int *(*pf)(void)) {
    return *(*pf)() + *(**pf)() + *pf();
}

int *(*global_pf)(void) = get_eight;

/**
 * Call through each shape and print the results.
 *
 * @return 0
 */
int main(void) {
    int *(*pf)(void) = get_seven;
    int *(**ppf)(void) = &pf;
    int *(*arr[2])(void) = {get_seven, get_eight};
    char **(*sf)(void) = get_strs;
    int (*(*gf)(void))(int) = get_add1;
    int (*const cf)(int) = add1;

    printf("%d %d %d %d\n", *(*pf)(), *(**pf)(), *(***pf)(), *pf());
    printf("%d %d %d\n", *(*ppf)(), *(**ppf)(), *(*ppf[0])());
    printf("%d %d %d\n", *(*arr[1])(), *(arr[0])(), *(*global_pf)());
    printf("%s %s\n", (*sf)()[1], *(*sf)());
    printf("%d %d %d\n", (*(*gf)())(41), (*gf)()(1), (*cf)(2));
    printf("%d\n", via_param(get_seven));
    return 0;
}
