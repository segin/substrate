/*
 * `packed` or `aligned` written where a structure is only named, not
 * defined -- `struct s __attribute__((packed)) v;` -- change nothing, as
 * gcc has it.  cc laid the structure out again with them: struct s became
 * 5 bytes, not 8, for every use of it, before and after.
 */
#include <stdio.h>

struct __attribute__((packed)) fwd;
struct fwd {
    char c;
    int i;
};

struct s {
    char c;
    int i;
};
struct s __attribute__((packed)) v;

struct t {
    char c;
};
struct __attribute__((aligned(16))) t *pt;

int main(void) {
    struct s __attribute__((packed)) local;

    printf("fwd %zu/%zu\n", sizeof(struct fwd), _Alignof(struct fwd));
    printf("s %zu/%zu v %zu local %zu\n", sizeof(struct s), _Alignof(struct s), sizeof(v), sizeof(local));
    printf("t %zu/%zu\n", sizeof(struct t), _Alignof(struct t));
    return 0;
}
