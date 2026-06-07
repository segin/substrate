/*
 * <malloc.h> — glibc-compatible malloc extensions.
 *
 * The ISO interface lives in <stdlib.h>; this header carries the historical
 * GNU additions (memalign/valloc/pvalloc, mallinfo/mallopt) that old code
 * (ksh93's libast) still includes <malloc.h> for.
 */
#ifndef _MALLOC_H
#define _MALLOC_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

void *malloc(size_t size);
void *calloc(size_t nmemb, size_t size);
void *realloc(void *ptr, size_t size);
void  free(void *ptr);

void *memalign(size_t alignment, size_t size);
void *valloc(size_t size);
void *pvalloc(size_t size);
void *aligned_alloc(size_t alignment, size_t size);
int   posix_memalign(void **memptr, size_t alignment, size_t size);

/* SVID/XPG malloc statistics.  Fields are ints, as in glibc. */
struct mallinfo {
    int arena;     /* non-mmapped space allocated (bytes) */
    int ordblks;   /* number of free chunks */
    int smblks;    /* number of free fastbin blocks */
    int hblks;     /* number of mmapped regions */
    int hblkhd;    /* space allocated in mmapped regions (bytes) */
    int usmblks;   /* maximum total allocated space (bytes) */
    int fsmblks;   /* space in freed fastbin blocks (bytes) */
    int uordblks;  /* total allocated space (bytes) */
    int fordblks;  /* total free space (bytes) */
    int keepcost;  /* top-most, releasable space (bytes) */
};

struct mallinfo mallinfo(void);
int mallopt(int param, int value);

/* mallopt() option names. */
#define M_MXFAST       1
#define M_NLBLKS       2
#define M_GRAIN        3
#define M_KEEP         4
#define M_TRIM_THRESHOLD   -1
#define M_TOP_PAD          -2
#define M_MMAP_THRESHOLD   -3
#define M_MMAP_MAX         -4
#define M_CHECK_ACTION     -5
#define M_PERTURB          -6
#define M_ARENA_TEST       -7
#define M_ARENA_MAX        -8

#ifdef __cplusplus
}
#endif
#endif /* _MALLOC_H */
