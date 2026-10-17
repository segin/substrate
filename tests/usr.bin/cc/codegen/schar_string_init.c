/*
 * A string literal initializes an array of any character type, signed
 * char included (C11 6.7.9p14).  cc took only char and unsigned char,
 * and rejected a signed char structure member initialized from one.
 */
#include <stdio.h>

struct s {
    int n;
    signed char name[6];
};

static struct s g = {1, "abc"};
static signed char ga[] = "xyz";
static signed char g2[2][4] = {"ab", "cde"};

int main(void) {
    struct s l = {2, "de"};
    signed char la[] = "local";
    signed char l2[2][3] = {"x", "yz"};
    const signed char *lp = (signed char[]){"lit"};

    printf("%d %s %d %s\n", g.n, (const char *)g.name, l.n, (const char *)l.name);
    printf("%u %s %u %s\n", (unsigned)sizeof ga, (const char *)ga, (unsigned)sizeof la, (const char *)la);
    printf("%s %s %s %s %s\n", (const char *)g2[0], (const char *)g2[1], (const char *)l2[0], (const char *)l2[1],
           (const char *)lp);
    return 0;
}
