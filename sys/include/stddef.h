#ifdef HOST_TEST
#include_next <stddef.h>
#else
#ifndef _STDDEF_H
#define _STDDEF_H

#define NULL ((void*)0)

typedef unsigned int size_t;
typedef int ptrdiff_t;

#define offsetof(type, member) ((size_t) &((type *)0)->member)

#endif
#endif
