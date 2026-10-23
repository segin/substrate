/*
 * `packed` and `aligned` written in a structure's specifier -- after
 * `struct`, its tag or its closing brace -- are the type's alone; written
 * before `struct`, or as `_Alignas`, they are the declaration's alone.
 * cc gave both kinds to both: an aligned(2) member structure in a packed
 * one made the outer structure 30 bytes, not 29, and `_Alignas(8) struct
 * s { char c; } v;` made struct s 8 bytes, not 1.
 */
#include <stddef.h>
#include <stdio.h>

/* in a packed structure: a member at any byte unless the member is aligned */
struct after_brace {
    char c;
    struct {
        char b;
        long long t;
    } __attribute__((packed, aligned(2))) m;
    char z;
} __attribute__((packed));

struct before_struct {
    char c;
    __attribute__((aligned(2))) struct {
        char b;
        long long t;
    } __attribute__((packed)) m;
    char z;
} __attribute__((packed));

struct alignas_member {
    char c;
    _Alignas(2) struct {
        char b;
        long long t;
    } __attribute__((packed)) m;
    char z;
} __attribute__((packed));

struct after_name {
    char c;
    struct {
        char b;
        long long t;
    } __attribute__((packed)) m __attribute__((aligned(2)));
    char z;
} __attribute__((packed));

struct after_keyword {
    char c;
    struct __attribute__((aligned(4))) {
        char b;
    } m;
    char z;
} __attribute__((packed));

/* not packed: the member's own alignment, the type's size */
struct alignas_plain {
    char c;
    _Alignas(8) struct {
        char b;
    } m;
    char z;
};

/* at file scope */
__attribute__((aligned(8))) struct s1 {
    char c;
};
_Alignas(8) struct s2 {
    char c;
} v2;
__attribute__((packed)) struct s3 {
    char c;
    int i;
} v3;
typedef __attribute__((aligned(8))) struct {
    char c;
} t4;
typedef struct {
    char c;
    int i;
} __attribute__((packed)) t5;
struct __attribute__((aligned(4))) s6 {
    char c;
};
struct s7 {
    char c;
    int i;
} __attribute__((packed)) v7, *p7;

#define SHOW(T, m) printf("%-16s size %2zu align %zu m at %zu z at %zu\n", #T, sizeof(struct T), \
                          _Alignof(struct T), offsetof(struct T, m), offsetof(struct T, z))

int main(void) {
    SHOW(after_brace, m);
    SHOW(before_struct, m);
    SHOW(alignas_member, m);
    SHOW(after_name, m);
    SHOW(after_keyword, m);
    SHOW(alignas_plain, m);
    printf("alignas_plain m size %zu\n", sizeof(((struct alignas_plain *)0)->m));
    printf("s1 %zu s2 %zu/%zu v2 %zu s3 %zu/%zu\n", sizeof(struct s1), sizeof(struct s2), _Alignof(struct s2),
           _Alignof(v2), sizeof(struct s3), _Alignof(struct s3));
    printf("t4 %zu/%zu t5 %zu/%zu s6 %zu/%zu s7 %zu/%zu\n", sizeof(t4), _Alignof(t4), sizeof(t5), _Alignof(t5),
           sizeof(struct s6), _Alignof(struct s6), sizeof(struct s7), _Alignof(struct s7));
    return 0;
}
