#ifndef _VM_KMEM_H
#define _VM_KMEM_H

#include <stddef.h>
#include <stdint.h>

#define KMEM_MIN_SHIFT 4
#define KMEM_MAX_SHIFT 12
#define KMEM_ZONES (KMEM_MAX_SHIFT - KMEM_MIN_SHIFT + 1)

typedef struct kmem_bucket_stat {
    size_t bucket_size;
    uint64_t allocs;
    uint64_t frees;
    uint64_t bytes_requested;
    uint64_t bytes_outstanding;
    uint64_t objects_outstanding;
} kmem_bucket_stat_t;

typedef struct kmem_stat_snapshot {
    uint64_t total_allocs;
    uint64_t total_frees;
    uint64_t total_bytes_requested;
    uint64_t total_bytes_outstanding;
    uint64_t large_allocs;
    uint64_t large_frees;
    uint64_t large_bytes_requested;
    uint64_t large_bytes_outstanding;
    kmem_bucket_stat_t buckets[KMEM_ZONES];
} kmem_stat_snapshot_t;

/* General purpose variable-size kernel allocator (wrapper around UMA zones) */
void kmem_init(void);
void *kmalloc(size_t size);
void *kzalloc(size_t size);  /* Allocate and zero memory */
void *krealloc(void *ptr, size_t size);
void kfree(void *ptr, size_t size); /* BSD kmem usually requires size for freeing */

/* Statistics */
void kmem_get_stats(uint64_t *allocs, uint64_t *frees, uint64_t *bytes);
void kmem_get_snapshot(kmem_stat_snapshot_t *snapshot);

#endif
