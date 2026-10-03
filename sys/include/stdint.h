#ifdef HOST_TEST
#include_next <stdint.h>
#else
#ifndef _STDINT_H
#define _STDINT_H

typedef signed char int8_t;
typedef unsigned char uint8_t;
typedef short int16_t;
typedef unsigned short uint16_t;
typedef int int32_t;
typedef unsigned int uint32_t;
typedef long long int64_t;
typedef unsigned long long uint64_t;

/* Pointer-sized types come from the compiler, so one header serves the
 * ILP32 (i386) and LP64 (x86_64) kernels; on i386 these are int and
 * unsigned int, as before. */
typedef __INTPTR_TYPE__ intptr_t;
typedef __UINTPTR_TYPE__ uintptr_t;

typedef int64_t intmax_t;
typedef uint64_t uintmax_t;

#define UINT8_MAX  0xff
#define UINT16_MAX 0xffff
#define UINT32_MAX 0xffffffff
#define UINT64_MAX 0xffffffffffffffffULL

#define INT32_MAX  2147483647
#define INT32_MIN  (-INT32_MAX - 1)
#define INT64_MAX  9223372036854775807LL
#define INT64_MIN  (-INT64_MAX - 1LL)
#define INTMAX_MAX  INT64_MAX
#define INTMAX_MIN  INT64_MIN
#define UINTMAX_MAX UINT64_MAX

#define INTPTR_MAX  __INTPTR_MAX__
#define INTPTR_MIN  (-INTPTR_MAX - 1)
#define UINTPTR_MAX __UINTPTR_MAX__
#define SIZE_MAX    __SIZE_MAX__
#define PTRDIFF_MAX __PTRDIFF_MAX__
#define PTRDIFF_MIN (-PTRDIFF_MAX - 1)

#endif
#endif
