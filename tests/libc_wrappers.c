#include <stdlib.h>
#include <stddef.h>

static int libc_errno_storage;

void* libc_malloc(size_t s) { return malloc(s); }
void libc_free(void* p) { free(p); }
int libc_rand(void) { return rand(); }
int *libc___errno_location(void) { return &libc_errno_storage; }
int *libc___error(void) { return &libc_errno_storage; }
int *libc___errno(void) { return &libc_errno_storage; }
