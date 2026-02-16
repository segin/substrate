#include <stdlib.h>
#include <stddef.h>

void* libc_malloc(size_t s) { return malloc(s); }
void libc_free(void* p) { free(p); }
int libc_rand(void) { return rand(); }
