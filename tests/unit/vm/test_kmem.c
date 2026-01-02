#include "../../../sys/vm/vm_kmem.h"
#include <stdbool.h>

/*
 * Kmem Kernel-side Unit Tests
 */

bool test_kmem_basic_alloc(void) {
    void *ptr = kmalloc(64);
    if (!ptr) return false;
    
    // Check if we can write to it
    volatile char *c = (char *)ptr;
    for (int i = 0; i < 64; i++) {
        c[i] = (char)i;
    }
    
    for (int i = 0; i < 64; i++) {
        if (c[i] != (char)i) return false;
    }
    
    kfree(ptr, 64);
    return true;
}

bool test_kmem_multiple_alloc(void) {
    void *p1 = kmalloc(32);
    void *p2 = kmalloc(128);
    void *p3 = kmalloc(1024);
    
    if (!p1 || !p2 || !p3) return false;
    if (p1 == p2 || p2 == p3 || p1 == p3) return false;
    
    kfree(p1, 32);
    kfree(p2, 128);
    kfree(p3, 1024);
    return true;
}

bool test_kmem_too_large(void) {
    void *ptr = kmalloc(4096);
    if (ptr != NULL) {
        kfree(ptr, 4096);
        return false;
    }
    return true;
}
