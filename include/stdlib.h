#ifndef _STDLIB_H
#define _STDLIB_H

#include <stddef.h>
#include <stdint.h>

#define RAND_MAX 2147483647

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
double strtod(const char *nptr, char **endptr);
float strtof(const char *nptr, char **endptr);
long double strtold(const char *nptr, char **endptr);
long strtol(const char *nptr, char **endptr, int base);
unsigned long strtoul(const char *nptr, char **endptr, int base);
long long strtoll(const char *nptr, char **endptr, int base);
unsigned long long strtoull(const char *nptr, char **endptr, int base);

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

uint32_t arc4random(void);
void arc4random_buf(void *buf, size_t n);
uint32_t arc4random_uniform(uint32_t upper_bound);

#endif
