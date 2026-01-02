#include "vm_kmem.h"
#include "vm_zone.h"
#include <stdint.h>

#define KMEM_MIN_SHIFT 4  // 16 bytes
#define KMEM_MAX_SHIFT 12 // 4096 bytes
#define KMEM_ZONES (KMEM_MAX_SHIFT - KMEM_MIN_SHIFT + 1)

static vm_zone_t *kmem_zones[KMEM_ZONES];

void kmem_init(void) {
    for (int i = 0; i < KMEM_ZONES; i++) {
        size_t size = 1 << (i + KMEM_MIN_SHIFT);
        // We'll use a naming scheme like "kmem-16", "kmem-32", etc.
        // Static strings for bootstrap.
        static const char *names[] = {
            "kmem-16", "kmem-32", "kmem-64", "kmem-128", 
            "kmem-256", "kmem-512", "kmem-1024", "kmem-2048",
            "kmem-4096"
        };
        kmem_zones[i] = vm_zone_create(names[i], size, size);
    }
}

void *kmalloc(size_t size) {
    if (size > (1 << KMEM_MAX_SHIFT)) return NULL; // Too large for bucket

    for (int i = 0; i < KMEM_ZONES; i++) {
        if (size <= (size_t)(1 << (i + KMEM_MIN_SHIFT))) {
            return vm_zone_alloc(kmem_zones[i]);
        }
    }
    return NULL;
}

void kfree(void *ptr, size_t size) {
    if (!ptr) return;
    
    for (int i = 0; i < KMEM_ZONES; i++) {
        if (size <= (size_t)(1 << (i + KMEM_MIN_SHIFT))) {
            vm_zone_free(kmem_zones[i], ptr);
            return;
        }
    }
}
