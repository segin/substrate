/*
 * An attribute specifier sequence may follow an array declarator's `]`
 * (C23 6.7.7.3): `char b[3] [[gnu::aligned(64)]];` aligns b, as gcc does,
 * at file scope, for members and for locals.
 *
 * cc-std: c23
 * cc-gcc: 14 (-std=c23)
 */
#include <stdint.h>
#include <stdio.h>

char pad0;
char b3[3] [[gnu::aligned(64)]];
char pad1;

struct s {
    int x[2] [[gnu::aligned(16)]];
    char c;
};

int main(void) {
    int l[3] [[maybe_unused]] = {7, 8, 9};

    printf("%d %u %d\n", (int)((uintptr_t)b3 % 64), (unsigned)sizeof(struct s), l[2]);
    return pad0 + pad1;
}
