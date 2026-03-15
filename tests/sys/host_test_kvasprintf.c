#define HOST_TEST

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <limits.h>

// Mock kernel functions
void *kmalloc(size_t size) {
    return malloc(size);
}

void kfree(void *ptr, size_t size) {
    (void)size;
    free(ptr);
}

// Rename kernel printf functions to avoid conflict with libc
#define snprintf kernel_snprintf
#define vsnprintf kernel_vsnprintf
#define sprintf kernel_sprintf
#define vsprintf kernel_vsprintf
#define kasprintf kernel_kasprintf
#define kvasprintf kernel_kvasprintf

#define _VM_KMEM_H

#include "../../sys/lib/printf.c"

#include "test_kvasprintf.c"

int main(void) {
    run_kvasprintf_tests();
    printf("All kvasprintf tests passed!\n");
    return 0;
}
