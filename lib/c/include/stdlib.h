#ifndef _STDLIB_H
#define _STDLIB_H

#include <stddef.h>

[[noreturn]] void exit(int status);
[[noreturn]] void abort(void);

void *malloc(size_t size);
void free(void *ptr);
void *calloc(size_t nmemb, size_t size);
void *realloc(void *ptr, size_t size);
void *aligned_alloc(size_t alignment, size_t size);

int atoi(const char *nptr);
long atol(const char *nptr);
long long atoll(const char *nptr);
double atof(const char *nptr);

char *getenv(const char *name);
int system(const char *command);

void quick_exit(int status);
int at_quick_exit(void (*func)(void));

void qsort(void *base, size_t nmemb, size_t size, int (*compar)(const void *, const void *));
void *bsearch(const void *key, const void *base, size_t nmemb, size_t size, int (*compar)(const void *, const void *));

int abs(int j);
long labs(long j);
long long llabs(long long j);

int rand(void);
void srand(unsigned int seed);

#endif
