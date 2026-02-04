/*
 * vm_kmem.c - Kernel Memory Allocator
 * 
 * Power-of-two bucket allocator backed by UMA zones.
 * Provides kmalloc/kfree for general kernel allocations.
 */

#include <vm/vm_kmem.h>
#include <vm/uma.h>
#include <arch/i386/pmm.h>
#include <stdint.h>
#include <string.h>

#define KMEM_MIN_SHIFT 4   /* 16 bytes minimum */
#define KMEM_MAX_SHIFT 12  /* 4096 bytes maximum for UMA */
#define KMEM_ZONES (KMEM_MAX_SHIFT - KMEM_MIN_SHIFT + 1)

/* UMA zones for power-of-two sizes */
static uma_zone_t *kmem_zones[KMEM_ZONES];

/* Statistics tracking */
typedef struct kmem_stats {
    uint64_t allocs;
    uint64_t frees;
    uint64_t bytes_allocated;
    uint64_t large_allocs;
    uint64_t large_frees;
} kmem_stats_t;

static kmem_stats_t kmem_stats;

/* Zone names */
static const char *kmem_zone_names[] = {
    "kmem-16", "kmem-32", "kmem-64", "kmem-128", 
    "kmem-256", "kmem-512", "kmem-1024", "kmem-2048",
    "kmem-4096"
};

/*
 * Initialize kernel memory allocator
 */
void kmem_init(void) {
    memset(&kmem_stats, 0, sizeof(kmem_stats));
    
    for (int i = 0; i < KMEM_ZONES; i++) {
        size_t size = 1 << (i + KMEM_MIN_SHIFT);
        kmem_zones[i] = uma_zcreate(
            kmem_zone_names[i],
            size,
            NULL,           /* No constructor */
            NULL,           /* No destructor */
            NULL,           /* No init */
            NULL,           /* No fini */
            0,              /* Default alignment */
            UMA_ZONE_MALLOC /* Mark as malloc zone */
        );
    }

    /* KMEM is now ready, allow UMA to use kmalloc for zone structures */
    uma_enable_dynamic_alloc();
}

/*
 * Find appropriate zone index for a size
 */
static int kmem_zone_index(size_t size) {
    for (int i = 0; i < KMEM_ZONES; i++) {
        if (size <= (size_t)(1 << (i + KMEM_MIN_SHIFT))) {
            return i;
        }
    }
    return -1; /* Too large */
}

/*
 * Large allocation header for tracking
 */
typedef struct kmem_large_header {
    size_t size;
    uint32_t magic;
} kmem_large_header_t;

#define KMEM_LARGE_MAGIC 0x4C41524D /* "LAML" */

/*
 * Allocate kernel memory
 */
void *kmalloc(size_t size) {
    if (size == 0) return NULL;
    
    kmem_stats.allocs++;
    kmem_stats.bytes_allocated += size;
    
    /* Small allocation via UMA zone */
    int idx = kmem_zone_index(size);
    if (idx >= 0) {
        void *result = uma_zalloc(kmem_zones[idx], M_NOWAIT);
        if (!result) {
            extern void kprint(const char*);
            kprint("kmalloc: uma_zalloc failed for zone ");
            kprint(kmem_zone_names[idx]);
            kprint("\n");
        }
        return result;
    }
    
    /* Large allocation: bypass UMA, allocate pages directly */
    size_t total = size + sizeof(kmem_large_header_t);
    size_t pages = (total + 4095) / 4096;
    
    void *mem = NULL;
    for (size_t i = 0; i < pages; i++) {
        void *p = pmm_alloc_block();
        if (!p) {
            /* Free previously allocated pages on failure */
            if (mem) {
                for (size_t j = 0; j < i; j++) {
                    pmm_free_block((void *)((uintptr_t)mem + j * 4096));
                }
            }
            return NULL;
        }
        if (i == 0) mem = p;
    }
    
    if (!mem) return NULL;
    
    kmem_stats.large_allocs++;
    
    /* Store header */
    kmem_large_header_t *hdr = (kmem_large_header_t *)mem;
    hdr->size = size;
    hdr->magic = KMEM_LARGE_MAGIC;
    
    return (void *)(hdr + 1);
}

/*
 * Free kernel memory
 */
void kfree(void *ptr, size_t size) {
    if (!ptr) return;
    
    kmem_stats.frees++;
    
    /* Small allocation via UMA zone */
    int idx = kmem_zone_index(size);
    if (idx >= 0) {
        uma_zfree(kmem_zones[idx], ptr);
        return;
    }
    
    /* Large allocation: check header and free pages */
    kmem_large_header_t *hdr = (kmem_large_header_t *)ptr - 1;
    
    if (hdr->magic != KMEM_LARGE_MAGIC) {
        /* Corrupted or invalid pointer */
        return;
    }
    
    size_t total = hdr->size + sizeof(kmem_large_header_t);
    size_t pages = (total + 4095) / 4096;
    
    /* Free pages */
    for (size_t i = 0; i < pages; i++) {
        pmm_free_block((void *)((uintptr_t)hdr + i * 4096));
    }
    
    kmem_stats.large_frees++;
}

/*
 * Allocate zeroed memory
 */
void *kzalloc(size_t size) {
    void *ptr = kmalloc(size);
    if (ptr) {
        memset(ptr, 0, size);
    }
    return ptr;
}

/*
 * Get allocator statistics
 */
void kmem_get_stats(uint64_t *allocs, uint64_t *frees, uint64_t *bytes) {
    if (allocs) *allocs = kmem_stats.allocs;
    if (frees) *frees = kmem_stats.frees;
    if (bytes) *bytes = kmem_stats.bytes_allocated;
}
