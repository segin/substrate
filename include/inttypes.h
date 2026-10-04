#ifndef _INTTYPES_H
#define _INTTYPES_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/*
 * Length modifiers for the types whose underlying type depends on the
 * data model.  These follow <stdint.h> exactly: on x86_64 (LP64) int64_t,
 * intmax_t and intptr_t are all `long`; on i386 (ILP32) int64_t and
 * intmax_t are `long long` and intptr_t is `int`.  A modifier that names
 * the wrong type of the right width (%llu for an unsigned long) is a
 * -Wformat error, and one of the wrong width (%x for a 64-bit uintptr_t)
 * prints half the value.
 */
#if defined(__x86_64__) || defined(_M_X64)
#define __PRI64_MOD  "l"
#define __PRIPTR_MOD "l"
#else
#define __PRI64_MOD  "ll"
#define __PRIPTR_MOD ""
#endif

/* Printf format macros for exact-width types */
#define PRId8   "d"
#define PRId16  "d"
#define PRId32  "d"
#define PRId64  __PRI64_MOD "d"

#define PRIi8   "i"
#define PRIi16  "i"
#define PRIi32  "i"
#define PRIi64  __PRI64_MOD "i"

#define PRIu8   "u"
#define PRIu16  "u"
#define PRIu32  "u"
#define PRIu64  __PRI64_MOD "u"

#define PRIo8   "o"
#define PRIo16  "o"
#define PRIo32  "o"
#define PRIo64  __PRI64_MOD "o"

#define PRIx8   "x"
#define PRIx16  "x"
#define PRIx32  "x"
#define PRIx64  __PRI64_MOD "x"

#define PRIX8   "X"
#define PRIX16  "X"
#define PRIX32  "X"
#define PRIX64  __PRI64_MOD "X"

/* Scanf format macros */
#define SCNd8   "hhd"
#define SCNd16  "hd"
#define SCNd32  "d"
#define SCNd64  __PRI64_MOD "d"

#define SCNu8   "hhu"
#define SCNu16  "hu"
#define SCNu32  "u"
#define SCNu64  __PRI64_MOD "u"

#define SCNx8   "hhx"
#define SCNx16  "hx"
#define SCNx32  "x"
#define SCNx64  __PRI64_MOD "x"

/* Least-width types */
#define PRIdLEAST8  PRId8
#define PRIdLEAST16 PRId16
#define PRIdLEAST32 PRId32
#define PRIdLEAST64 PRId64

#define PRIuLEAST8  PRIu8
#define PRIuLEAST16 PRIu16
#define PRIuLEAST32 PRIu32
#define PRIuLEAST64 PRIu64

#define PRIxLEAST8  PRIx8
#define PRIxLEAST16 PRIx16
#define PRIxLEAST32 PRIx32
#define PRIxLEAST64 PRIx64

/* Fast-width types */
#define PRIdFAST8  PRId8
#define PRIdFAST16 PRId32
#define PRIdFAST32 PRId32
#define PRIdFAST64 PRId64

#define PRIuFAST8  PRIu8
#define PRIuFAST16 PRIu32
#define PRIuFAST32 PRIu32
#define PRIuFAST64 PRIu64

#define PRIxFAST8  PRIx8
#define PRIxFAST16 PRIx32
#define PRIxFAST32 PRIx32
#define PRIxFAST64 PRIx64

/* SCN<conv>LEAST<N> and SCN<conv>FAST<N> — same widths as the
 * exact-width SCN<conv>N, since stdint.h's _least_ / _fast_
 * typedefs alias the exact-width N versions on substrate.  Required
 * by C99 §7.8.1 for code that scans into int_fast64_t etc.  */
#define SCNdLEAST8  SCNd8
#define SCNdLEAST16 SCNd16
#define SCNdLEAST32 SCNd32
#define SCNdLEAST64 SCNd64
#define SCNuLEAST8  SCNu8
#define SCNuLEAST16 SCNu16
#define SCNuLEAST32 SCNu32
#define SCNuLEAST64 SCNu64
#define SCNxLEAST8  SCNx8
#define SCNxLEAST16 SCNx16
#define SCNxLEAST32 SCNx32
#define SCNxLEAST64 SCNx64

#define SCNdFAST8   SCNd8
#define SCNdFAST16  SCNd32
#define SCNdFAST32  SCNd32
#define SCNdFAST64  SCNd64
#define SCNuFAST8   SCNu8
#define SCNuFAST16  SCNu32
#define SCNuFAST32  SCNu32
#define SCNuFAST64  SCNu64
#define SCNxFAST8   SCNx8
#define SCNxFAST16  SCNx32
#define SCNxFAST32  SCNx32
#define SCNxFAST64  SCNx64

/* Pointer */
#define PRIdPTR __PRIPTR_MOD "d"
#define PRIuPTR __PRIPTR_MOD "u"
#define PRIxPTR __PRIPTR_MOD "x"
#define PRIoPTR __PRIPTR_MOD "o"
#define PRIXPTR __PRIPTR_MOD "X"

/* Maximum-width type: intmax_t is int64_t on both data models. */
#define PRIdMAX __PRI64_MOD "d"
#define PRIiMAX __PRI64_MOD "i"
#define PRIuMAX __PRI64_MOD "u"
#define PRIxMAX __PRI64_MOD "x"
#define PRIoMAX __PRI64_MOD "o"
#define PRIXMAX __PRI64_MOD "X"

/* PRIi for pointer-width signed type — mirror of PRId*PTR. */
#define PRIiPTR __PRIPTR_MOD "i"

#define SCNdMAX __PRI64_MOD "d"
#define SCNiMAX __PRI64_MOD "i"
#define SCNuMAX __PRI64_MOD "u"
#define SCNxMAX __PRI64_MOD "x"
#define SCNoMAX __PRI64_MOD "o"

typedef struct { intmax_t quot; intmax_t rem; } imaxdiv_t;

/* Need wchar_t for the wcstoimax/wcstoumax prototypes.  Pull it from
 * stddef.h rather than redefine — substrate's wchar_t is `long int`,
 * and a local typedef-with-different-spelling would conflict. */
#include <stddef.h>

intmax_t  imaxabs(intmax_t j);
imaxdiv_t imaxdiv(intmax_t numer, intmax_t denom);
intmax_t  strtoimax(const char *__restrict nptr, char **__restrict endptr, int base);
uintmax_t strtoumax(const char *__restrict nptr, char **__restrict endptr, int base);
intmax_t  wcstoimax(const wchar_t *__restrict nptr, wchar_t **__restrict endptr, int base);
uintmax_t wcstoumax(const wchar_t *__restrict nptr, wchar_t **__restrict endptr, int base);

#ifdef __cplusplus
}
#endif
#endif /* _INTTYPES_H */
