/*
 * At file scope a compound literal has static storage (C11 6.5.2.5p5),
 * so its address is an address constant: an array literal decays to one,
 * and & takes one of any.  cc accepted only &(struct S){...} there
 * ("unsupported global initializer for p").  A structure literal used as
 * a value (a GNU extension) still initializes by value.
 */
#include <stdio.h>

struct s {
    int a, b;
};

static char *p1 = (char[]){"lit"};
static int *p2 = (int[]){1, 2, 3};
static int *p3 = &(int){5};
static int (*p4)[2] = &(int[2]){7, 8};
static struct s *p5 = &(struct s){1, 2};
static const char *p6 = (const char[]){"ro"};
static char *p7 = (char[8]){"lit"};
static double *p8 = &(double){2.5};
static struct s v = (struct s){3, 4};
static const char *tab[] = {(char[]){"a"}, (char[]){"bc"}};

int main(void) {
    p1[0] = 'L';
    *p3 += 1;
    p5->b += 40;
    printf("%s %d %d %d %d %s %s %g %d %s %s\n", p1, p2[2], *p3, (*p4)[1], p5->b, p6, p7, *p8, v.b, tab[0], tab[1]);
    printf("%u %u\n", (unsigned)sizeof *p4, (unsigned)sizeof((int[]){1, 2, 3}));
    return 0;
}
