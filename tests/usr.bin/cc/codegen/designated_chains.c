/*
 * Initializers as C11 6.7.9 reads them, designators of any shape and
 * brace elision mixed: a designator is any sequence of [index] and .member
 * (6.7.9p1), it is resolved against the object the enclosing braces
 * initialize, initialization continues after the subobject it names
 * (p17), a later initializer overrides an earlier one for the same part
 * (p19), and an initializer that is not a brace list fills the first
 * scalar of an aggregate part, its braces elided (p20).  At file scope, in
 * functions and in compound literals.
 */
#include <stdio.h>

struct p { int x, y; };
struct q { struct p pt[3]; int z; };
union u { int i; struct p pp; char c[8]; };
struct anon { int a; struct { int b, c; }; union { int d; char e; }; };

int a[2][2] = {[1][1] = 5, [0][1] = 2};
int e1[2][2] = {1, 2, [1] = {3, 4}};      /* a designator after elided items */
int e2[2][3] = {1, 2, 3, [1][1] = 9, 8};  /* continues after [1][1] */
int r[6] = {[1 ... 3] = 7, [4] = 1};
struct q gq = {.pt[1].y = 7, .pt[2] = {8, 9}, .z = 4};
struct p gp[3] = {[1].x = 3, 4, {5, 6}};
struct p ep[2] = {1, 2, [1] = {3, 4}};
union u gu = {.pp.y = 6};
struct anon ga = {.c = 3, .e = 'x', .a = 1};
char names[3][4] = {"ab", [2] = "cd"};
int nob[] = {[4] = 1, [1] = 2, 3};        /* bound 5 */

static void show(const char *tag, const int *v, int n) {
    int i;

    printf("%s:", tag);
    for (i = 0; i < n; ++i) {
        printf(" %d", v[i]);
    }
    printf("\n");
}

int main(void) {
    int la[3][2] = {[2][0] = 6, [0] = {1, 2}, [0][1] = 7};
    struct q lq = {.pt[0].x = 11, .pt[1].y = 12, 13};  /* 13 goes to pt[2].x */
    union u lu = {.c[2] = 'z'};
    struct p *cl = (struct p[]){[1].y = 21, [0] = {19, 20}};
    int *cr = (int[]){[2] = 5, [0] = 1};

    show("a", &a[0][0], 4);
    show("e1", &e1[0][0], 4);
    show("e2", &e2[0][0], 6);
    show("r", r, 6);
    printf("gq %d %d %d %d %d\n", gq.pt[1].y, gq.pt[2].x, gq.pt[2].y, gq.z, gq.pt[0].x);
    show("gp", &gp[0].x, 6);
    show("ep", &ep[0].x, 4);
    printf("gu %d %d, ga %d %d %d %c\n", gu.pp.x, gu.pp.y, ga.a, ga.b, ga.c, ga.e);
    printf("names %s %s %s, nob %u:", names[0], names[1], names[2], (unsigned)(sizeof nob / sizeof nob[0]));
    show("", nob, 5);
    show("la", &la[0][0], 6);
    printf("lq %d %d %d %d\n", lq.pt[0].x, lq.pt[1].y, lq.pt[2].x, lq.z);
    printf("lu %c %d, cl %d %d %d %d, cr %d %d %d\n", lu.c[2], lu.c[0], cl[0].x, cl[0].y, cl[1].x, cl[1].y, cr[0],
           cr[1], cr[2]);
    return 0;
}
