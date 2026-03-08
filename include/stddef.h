#ifndef _STDDEF_H
#define _STDDEF_H

typedef __SIZE_TYPE__ size_t;
typedef __PTRDIFF_TYPE__ ptrdiff_t;
typedef __WCHAR_TYPE__ wchar_t;

#define NULL ((void *)0)
#define offsetof(type, member) ((size_t)&(((type *)0)->member))

typedef struct {
    long long __ll;
    long double __ld;
} max_align_t;

#endif
