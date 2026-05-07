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
