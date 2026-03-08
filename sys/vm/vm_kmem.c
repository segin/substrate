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

/* UMA zones for power-of-two sizes */
static uma_zone_t *kmem_zones[KMEM_ZONES];

/* Statistics tracking */
typedef struct kmem_stats {
    uint64_t allocs;
    uint64_t frees;
    uint64_t bytes_allocated;
    uint64_t bytes_outstanding;
    uint64_t large_allocs;
    uint64_t large_frees;
    uint64_t large_bytes_requested;
    uint64_t large_bytes_outstanding;
    kmem_bucket_stat_t buckets[KMEM_ZONES];
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
        kmem_stats.buckets[i].bucket_size = size;
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
    kmem_stats.bytes_outstanding += size;
    
    /* Small allocation via UMA zone */
    int idx = kmem_zone_index(size);
    if (idx >= 0) {
        void *result = uma_zalloc(kmem_zones[idx], M_NOWAIT);
        if (!result) {
            kmem_stats.bytes_outstanding -= size;
            extern void kprint(const char*);
            kprint("kmalloc: uma_zalloc failed for zone ");
            kprint(kmem_zone_names[idx]);
            kprint("\n");
        } else {
            kmem_stats.buckets[idx].allocs++;
            kmem_stats.buckets[idx].bytes_requested += size;
            kmem_stats.buckets[idx].bytes_outstanding += size;
            kmem_stats.buckets[idx].objects_outstanding++;
        }
        return result;
    }
    
    /* Large allocation: bypass UMA, allocate pages directly */
    size_t total = size + sizeof(kmem_large_header_t);
    size_t pages = (total + 4095) / 4096;
    
    void *mem = pmm_alloc_contiguous(pages);
    if (!mem) {
        kmem_stats.bytes_outstanding -= size;
        return NULL;
    }
    
    kmem_stats.large_allocs++;
    kmem_stats.large_bytes_requested += size;
    kmem_stats.large_bytes_outstanding += size;
    
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
    if (kmem_stats.bytes_outstanding >= size) {
        kmem_stats.bytes_outstanding -= size;
    } else {
        kmem_stats.bytes_outstanding = 0;
    }
    
    /* Small allocation via UMA zone */
    int idx = kmem_zone_index(size);
    if (idx >= 0) {
        kmem_stats.buckets[idx].frees++;
        if (kmem_stats.buckets[idx].bytes_outstanding >= size) {
            kmem_stats.buckets[idx].bytes_outstanding -= size;
        } else {
            kmem_stats.buckets[idx].bytes_outstanding = 0;
        }
        if (kmem_stats.buckets[idx].objects_outstanding > 0) {
            kmem_stats.buckets[idx].objects_outstanding--;
        }
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
    pmm_free_contiguous(hdr, pages);
    
    kmem_stats.large_frees++;
    if (kmem_stats.large_bytes_outstanding >= hdr->size) {
        kmem_stats.large_bytes_outstanding -= hdr->size;
    } else {
        kmem_stats.large_bytes_outstanding = 0;
    }
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

void *krealloc(void *ptr, size_t size) {
    size_t old_size;
    void *new_ptr;

    if (!ptr) {
        return kmalloc(size);
    }

    if (size == 0) {
        old_size = uma_item_size(ptr);
        if (old_size == 0) {
            kmem_large_header_t *hdr = (kmem_large_header_t *)ptr - 1;
            if (hdr->magic == KMEM_LARGE_MAGIC) {
                old_size = hdr->size;
            }
        }
        if (old_size != 0) {
            kfree(ptr, old_size);
        }
        return NULL;
    }

    old_size = uma_item_size(ptr);
    if (old_size == 0) {
        kmem_large_header_t *hdr = (kmem_large_header_t *)ptr - 1;
        if (hdr->magic == KMEM_LARGE_MAGIC) {
            old_size = hdr->size;
        }
    }
    if (old_size == 0) {
        return NULL;
    }

    if (old_size == size) {
        return ptr;
    }

    new_ptr = kmalloc(size);
    if (!new_ptr) {
        return NULL;
    }

    memcpy(new_ptr, ptr, old_size < size ? old_size : size);
    kfree(ptr, old_size);
    return new_ptr;
}

/*
 * Get allocator statistics
 */
void kmem_get_stats(uint64_t *allocs, uint64_t *frees, uint64_t *bytes) {
    if (allocs) *allocs = kmem_stats.allocs;
    if (frees) *frees = kmem_stats.frees;
    if (bytes) *bytes = kmem_stats.bytes_allocated;
}

void kmem_get_snapshot(kmem_stat_snapshot_t *snapshot) {
    if (!snapshot) {
        return;
    }

    memset(snapshot, 0, sizeof(*snapshot));
    snapshot->total_allocs = kmem_stats.allocs;
    snapshot->total_frees = kmem_stats.frees;
    snapshot->total_bytes_requested = kmem_stats.bytes_allocated;
    snapshot->total_bytes_outstanding = kmem_stats.bytes_outstanding;
    snapshot->large_allocs = kmem_stats.large_allocs;
    snapshot->large_frees = kmem_stats.large_frees;
    snapshot->large_bytes_requested = kmem_stats.large_bytes_requested;
    snapshot->large_bytes_outstanding = kmem_stats.large_bytes_outstanding;
    memcpy(snapshot->buckets, kmem_stats.buckets, sizeof(snapshot->buckets));
}
