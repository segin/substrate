#ifndef _INTTYPES_H
#define _INTTYPES_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/* Printf format macros for exact-width types */
#define PRId8   "d"
#define PRId16  "d"
#define PRId32  "d"
#define PRId64  "lld"

#define PRIi8   "i"
#define PRIi16  "i"
#define PRIi32  "i"
#define PRIi64  "lli"

#define PRIu8   "u"
#define PRIu16  "u"
#define PRIu32  "u"
#define PRIu64  "llu"

#define PRIo8   "o"
#define PRIo16  "o"
#define PRIo32  "o"
#define PRIo64  "llo"

#define PRIx8   "x"
#define PRIx16  "x"
#define PRIx32  "x"
#define PRIx64  "llx"

#define PRIX8   "X"
#define PRIX16  "X"
#define PRIX32  "X"
#define PRIX64  "llX"

/* Scanf format macros */
#define SCNd8   "hhd"
#define SCNd16  "hd"
#define SCNd32  "d"
#define SCNd64  "lld"

#define SCNu8   "hhu"
#define SCNu16  "hu"
#define SCNu32  "u"
#define SCNu64  "llu"

#define SCNx8   "hhx"
#define SCNx16  "hx"
#define SCNx32  "x"
#define SCNx64  "llx"

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

/* Pointer */
#define PRIdPTR "d"
#define PRIuPTR "u"
#define PRIxPTR "x"
#define PRIoPTR "o"
#define PRIXPTR "X"

/* Maximum-width type */
#define PRIdMAX "lld"
#define PRIiMAX "lli"
#define PRIuMAX "llu"
#define PRIxMAX "llx"
#define PRIoMAX "llo"
#define PRIXMAX "llX"

/* PRIi for pointer-width signed type — mirror of PRId*PTR. */
#define PRIiPTR "i"

#define SCNdMAX "lld"
#define SCNiMAX "lli"
#define SCNuMAX "llu"
#define SCNxMAX "llx"
#define SCNoMAX "llo"

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
