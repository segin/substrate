#ifndef _STDINT_H
#define _STDINT_H

// Core types
typedef signed char int8_t;
typedef unsigned char uint8_t;
typedef short int16_t;
typedef unsigned short uint16_t;
typedef int int32_t;
typedef unsigned int uint32_t;

#if defined(__x86_64__) || defined(_M_X64)
typedef long int64_t;
typedef unsigned long uint64_t;
typedef long intptr_t;
typedef unsigned long uintptr_t;
#else
typedef long long int64_t;
typedef unsigned long long uint64_t;
typedef int intptr_t;
typedef unsigned int uintptr_t;
#endif

typedef int64_t intmax_t;
typedef uint64_t uintmax_t;

typedef int8_t int_least8_t;
typedef int16_t int_least16_t;
typedef int32_t int_least32_t;
typedef int64_t int_least64_t;

typedef uint8_t uint_least8_t;
typedef uint16_t uint_least16_t;
typedef uint32_t uint_least32_t;
typedef uint64_t uint_least64_t;

typedef int8_t int_fast8_t;
typedef int32_t int_fast16_t;
typedef int32_t int_fast32_t;
typedef int64_t int_fast64_t;

typedef uint8_t uint_fast8_t;
typedef uint32_t uint_fast16_t;
typedef uint32_t uint_fast32_t;
typedef uint64_t uint_fast64_t;

// Limits
#define INT8_MIN  (-128)
#define INT8_MAX  127
#define UINT8_MAX 255

#define INT16_MIN (-32768)
#define INT16_MAX 32767
#define UINT16_MAX 65535

#define INT32_MIN (-2147483648)
#define INT32_MAX 2147483647
#define UINT32_MAX 4294967295U

#if defined(__x86_64__) || defined(_M_X64)
#define INT64_MAX  9223372036854775807L
#define INT64_MIN  (-INT64_MAX - 1L)
#define UINT64_MAX 18446744073709551615UL

#define INTPTR_MIN INT64_MIN
#define INTPTR_MAX INT64_MAX
#define UINTPTR_MAX UINT64_MAX

#define PTRDIFF_MIN INT64_MIN
#define PTRDIFF_MAX INT64_MAX

#define SIZE_MAX UINT64_MAX
#else
#define INT64_MAX  9223372036854775807LL
#define INT64_MIN  (-INT64_MAX - 1LL)
#define UINT64_MAX 18446744073709551615ULL

#define INTPTR_MIN INT32_MIN
#define INTPTR_MAX INT32_MAX
#define UINTPTR_MAX UINT32_MAX

#define PTRDIFF_MIN INT32_MIN
#define PTRDIFF_MAX INT32_MAX

#define SIZE_MAX UINT32_MAX
#endif

#define INTMAX_MIN INT64_MIN
#define INTMAX_MAX INT64_MAX
#define UINTMAX_MAX UINT64_MAX

#define SIG_ATOMIC_MIN INT32_MIN
#define SIG_ATOMIC_MAX INT32_MAX

/* Constants for minimum-width integer constant expressions */
#define INT8_C(value) ((int8_t) value)
#define INT16_C(value) ((int16_t) value)
#define INT32_C(value) ((int32_t) value)

#define UINT8_C(value) ((uint8_t) value)
#define UINT16_C(value) ((uint16_t) value)
#define UINT32_C(value) ((uint32_t) value ## U)

#if defined(__x86_64__) || defined(_M_X64)
#define INT64_C(value) ((int64_t) value ## L)
#define UINT64_C(value) ((uint64_t) value ## UL)
#else
#define INT64_C(value) ((int64_t) value ## LL)
#define UINT64_C(value) ((uint64_t) value ## ULL)
#endif

#define INTMAX_C(value)  INT64_C(value)
#define UINTMAX_C(value) UINT64_C(value)

#endif