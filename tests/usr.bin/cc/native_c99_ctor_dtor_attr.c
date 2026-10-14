#include <stdio.h>

static int ctor_seen;
static int main_seen;

__attribute__((constructor))
static void ctor(void) {
    ctor_seen = 1;
}

__attribute__((destructor))
static void dtor(void) {
    if (ctor_seen && main_seen) {
        fputs("D\n", stderr);
    } else {
        fputs("BAD\n", stderr);
    }
}

int main(void) {
    main_seen = ctor_seen ? 1 : 0;
    return ctor_seen ? 0 : 1;
}
