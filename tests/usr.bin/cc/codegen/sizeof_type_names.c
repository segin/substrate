/*
 * sizeof of type names whose declarators nest: pointers to arrays,
 * arrays of pointers to functions, function types.  The parser used to
 * count asterisks instead of parsing the declarator, so several of these
 * were syntax errors and the rest worked by luck of the encoding.
 */
#include <stdio.h>

typedef int row_t[5];
struct s {
    char c;
    double d;
};

/**
 * Print each size on a line of its own, labelled.
 *
 * @return 0
 */
int main(void) {
    printf("int *[7] %u\n", (unsigned)sizeof(int *[7]));
    printf("int (*)[7] %u\n", (unsigned)sizeof(int (*)[7]));
    printf("int[2][3] %u\n", (unsigned)sizeof(int[2][3]));
    printf("char[2][3][4][5] %u\n", (unsigned)sizeof(char[2][3][4][5]));
    printf("char (*)[4][5] %u\n", (unsigned)sizeof(char (*)[4][5]));
    printf("int (*)(int) %u\n", (unsigned)sizeof(int (*)(int)));
    printf("int(void) %u\n", (unsigned)sizeof(int(void)));
    printf("void (*(*)(int))(void) %u\n", (unsigned)sizeof(void (*(*)(int))(void)));
    printf("int (*[3])(void) %u\n", (unsigned)sizeof(int (*[3])(void)));
    printf("int *(*[2])(void) %u\n", (unsigned)sizeof(int *(*[2])(void)));
    printf("row_t %u\n", (unsigned)sizeof(row_t));
    printf("row_t *[2] %u\n", (unsigned)sizeof(row_t *[2]));
    printf("struct s[3] %u\n", (unsigned)sizeof(struct s[3]));
    printf("struct s (*)[3] %u\n", (unsigned)sizeof(struct s (*)[3]));
    printf("long double[2] %u\n", (unsigned)sizeof(long double[2]));
    printf("const char *const[4] %u\n", (unsigned)sizeof(const char *const[4]));
    return 0;
}
