/*
 * An asm memory input is the object itself, not a copy of its value.  cc
 * loaded `*(u8 *)addr` and gave the asm the stack slot it copied it to, so
 * Linux's rdfs8() -- `movb %%fs:%1, %0` on "m" (*(u8 *)addr) -- read the
 * stack's offset through %fs.  lea of the operand shows which memory the
 * asm was given.
 */
#include <stdio.h>

struct pair {
    int a;
    int b;
};

static unsigned char bytes[4] = {1, 2, 3, 4};
static struct pair pairs[2];

static void *addr_of_input(const unsigned char *p) {
    void *got;

    __asm__("lea %1, %0" : "=r"(got) : "m"(*p));
    return got;
}

static void *addr_of_member(struct pair *p) {
    void *got;

    __asm__("lea %1, %0" : "=r"(got) : "m"(p->b));
    return got;
}

static void *addr_of_static(void) {
    static int counter = 5;
    void *got;

    __asm__("lea %1, %0" : "=r"(got) : "m"(counter));
    return got == (void *)&counter ? (void *)1 : (void *)0;
}

static int read_input(const unsigned char *p) {
    unsigned char v;

    __asm__("movb %1, %0" : "=q"(v) : "m"(*p));
    return v;
}

int main(void) {
    printf("deref %d\n", addr_of_input(&bytes[2]) == (void *)&bytes[2]);
    printf("member %d\n", addr_of_member(&pairs[1]) == (void *)&pairs[1].b);
    printf("static %d\n", addr_of_static() != 0);
    printf("value %d\n", read_input(&bytes[3]));
    return 0;
}
