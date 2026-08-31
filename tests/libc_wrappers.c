#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdint.h>

static int libc_errno_storage;

void* libc_malloc(size_t s) { return malloc(s); }
void libc_free(void* p) { free(p); }
int libc_rand(void) { return rand(); }
int *libc___errno_location(void) { return &libc_errno_storage; }
int *libc___error(void) { return &libc_errno_storage; }
int *libc___errno(void) { return &libc_errno_storage; }

/* strfry now uses arc4random_uniform; under the libc_ symbol prefix
 * scheme used by test_libc_string this resolves to libc_arc4random_uniform.
 * We don't need cryptographic quality in test runs. */
uint32_t libc_arc4random_uniform(uint32_t upper) {
    if (upper == 0) return 0;
    return (uint32_t)rand() % upper;
}

/*
 * The NULL guards in memcpy/memset/strdup report to stderr before bailing.
 * --prefix-symbols rewrites the references as well as the definitions, so
 * those become libc_fprintf and libc_stderr and nothing defined them.
 *
 * libc_stderr cannot simply be initialised to stderr at file scope: glibc's
 * stderr is an ordinary extern FILE *, not a constant expression.  A
 * constructor sets it before any test body runs.
 */
FILE *libc_stderr;

__attribute__((constructor))
static void libc_wrappers_init(void) {
    libc_stderr = stderr;
}

int libc_fprintf(FILE *stream, const char *fmt, ...) {
    va_list ap;
    int n;

    va_start(ap, fmt);
    n = vfprintf(stream ? stream : stderr, fmt, ap);
    va_end(ap);
    return n;
}
