/*
 * A static initializer may hold a string literal's address converted to
 * an integer as wide as a pointer, and may pick it with a `?:` of
 * constant condition, as gcc folds them.  cc said "unsupported scalar in
 * global initializer": Linux's PCI device tables hold
 * `.driver_data = (0 ? 0 : (kernel_ulong_t)"...")`.
 */
#include <stdio.h>

typedef unsigned long ul;

struct id {
    int v;
    ul data;
};

static const struct id table[] = {
    {1, (ul)"PARPORT"},
    {2, (0 ? 0 : (ul)"SERIAL_""MID")},
    {3, (1 ? 0 : (ul)"none")},
    {4, (ul)(const void *)"void"},
};
static ul whole = (ul)"whole";
static const char *picked = 1 ? "yes" : "no";

int main(void) {
    printf("%s %s %lu %s\n", (const char *)table[0].data, (const char *)table[1].data, table[2].data,
           (const char *)table[3].data);
    printf("%s %s\n", (const char *)whole, picked);
    return 0;
}
