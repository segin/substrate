/*
 * A string literal is an array lvalue, so `&"s"` is a pointer to that
 * array, at the address "s" decays to (C11 6.5.1p4, 6.5.3.2).  cc said
 * "address-of currently requires an identifier or member lvalue": Linux's
 * PCI device tables hold `.driver_data = (kernel_ulong_t)&"PARPORT_SERIAL"`.
 */
#include <stdio.h>

typedef unsigned long ul;

struct id {
    int v;
    ul data;
};

static const struct id table[] = {
    {1, (ul)&"PARPORT"},
    {2, (0 ? 0 : (ul)&"SERIAL_""MID")},
};
static const char (*gp)[4] = &"abc";

int main(void) {
    const char (*p)[3] = &"hi";

    printf("%s %s\n", (const char *)table[0].data, (const char *)table[1].data);
    printf("%s %s %d %d\n", *p, *gp, (int)sizeof(*&"abcde"), (int)sizeof(*gp));
    printf("%c %d\n", (*&"xyz")[1], (const char *)&"q" != 0);
    return 0;
}
