/*
 * Structure layout must be the psABI's, which gcc follows, or code
 * compiled by one cannot share a structure with code compiled by the
 * other.  On x86-64 a long double is 16 bytes aligned to 16; on i386 it
 * is 12 bytes aligned to 4, and long long and double are aligned to 4
 * as members.  offsetof has type size_t, which is 4 bytes on i386: a
 * wider value passed to printf's %zu shifts every later argument.  A
 * subscript in offsetof steps by the element's size, and a member of an
 * array element can be named after it.
 */
#include <stddef.h>
#include <stdio.h>

struct tok {
    int kind;
    const char *start;
    long long num;
    double fnum;
    long double fnum_ld;
    int flag;
    const char *file;
    size_t line;
};

struct nested {
    char c;
    struct tok t;
    short s;
};

struct mixed {
    char c1;
    long long ll;
    char c2;
    double d;
    char c3;
    long double ld;
    char c4;
};

struct arr {
    char c;
    long double v[3];
    int tail[2];
};

struct inner {
    int a;
    double d;
};

struct outer {
    char c;
    char *p[3];
    struct inner s[2];
};

struct tok gtok = {1, "x", 2, 3.0, 4.0L, 5, "f", 6};

int main(void) {
    struct arr a;
    int i = 1;

    printf("long double: %zu %zu\n", sizeof(long double), _Alignof(long double));
    printf("tok: %zu %zu | %zu %zu %zu %zu %zu %zu %zu\n", sizeof(struct tok), _Alignof(struct tok),
           offsetof(struct tok, num), offsetof(struct tok, fnum), offsetof(struct tok, fnum_ld),
           offsetof(struct tok, flag), offsetof(struct tok, file), offsetof(struct tok, line), sizeof gtok);
    printf("nested: %zu %zu %zu\n", sizeof(struct nested), offsetof(struct nested, t),
           offsetof(struct nested, s));
    printf("mixed: %zu | %zu %zu %zu %zu %zu %zu\n", sizeof(struct mixed), offsetof(struct mixed, ll),
           offsetof(struct mixed, c2), offsetof(struct mixed, d), offsetof(struct mixed, c3),
           offsetof(struct mixed, ld), offsetof(struct mixed, c4));
    printf("arr: %zu %zu %zu %zu\n", sizeof(struct arr), offsetof(struct arr, v[2]), offsetof(struct arr, tail[1]),
           offsetof(struct arr, v[i]));
    printf("outer: %zu %zu %zu %zu\n", sizeof(struct outer), offsetof(struct outer, p[2]),
           offsetof(struct outer, s[1]), offsetof(struct outer, s[1].d));
    /* the members are where the layout says they are */
    a.v[2] = 1.5L;
    a.tail[1] = 9;
    printf("access: %d %d %d\n", (int)(*(long double *)((char *)&a + offsetof(struct arr, v[2])) * 2),
           *(int *)((char *)&a + offsetof(struct arr, tail[1])), (int)(gtok.fnum_ld * 2) + gtok.flag);
    return 0;
}
