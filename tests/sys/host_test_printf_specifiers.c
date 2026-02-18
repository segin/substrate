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

// Include the source file directly
// We need to ensure we don't include conflicting headers from the kernel tree
// sys/lib/printf.c includes <sys/types.h> and <vm/vm_kmem.h>

// Mocking vm/vm_kmem.h content since we already implemented kmalloc/kfree
#define _VM_KMEM_H
// (The header guard prevents re-inclusion if we defined it,
// but printf.c includes it. If we don't provide the file or include path, it fails.
// If we provide include path, it might pull in unwanted stuff.
// Best approach: Rely on the include paths in Makefile to find the real header,
// but ensure its content is compatible or empty for HOST_TEST.)

// sys/lib/printf.c includes <sys/types.h>.
// In host tests, we usually include standard headers first (which we did).
// Then we include the kernel source.

#include "../../sys/lib/printf.c"

// Now include the test logic
// The test logic uses TEST_SNPRINTF which we defined as kernel_snprintf in HOST_TEST block
#include "test_printf_specifiers.c"

int main(void) {
    run_printf_specifier_tests();
    printf("All printf specifier tests passed!\n");
    return 0;
}
