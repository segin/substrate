/*
 * uma.h - Universal Memory Allocator
 * 
 * FreeBSD-inspired slab allocator with per-CPU caches,
 * constructor/destructor support, and debugging features.
 */

#ifndef _VM_UMA_H
#define _VM_UMA_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <sys/smp.h>

/* Forward declarations */
typedef struct uma_zone uma_zone_t;
typedef struct uma_slab uma_slab_t;
typedef struct uma_cache uma_cache_t;

/*
 * Zone creation flags
 */
#define UMA_ZONE_ZINIT      0x0001  /* Zero-fill on allocation */
#define UMA_ZONE_NOFREE     0x0002  /* Never free slabs back to VM */
#define UMA_ZONE_NODUMP     0x0004  /* Don't include in crash dumps */
#define UMA_CACHE_SIZE      32      /* Max per-CPU cache size */
#define UMA_CACHE_MIN       16      /* Min items to keep in cache */

/* Redzone/poison patterns */
#define UMA_REDZONE_BYTE    0xFE
#define UMA_POISON_FREE     0xDEADBEEF
#define UMA_POISON_ALLOC    0xBAADF00D
#define UMA_REDZONE_SIZE    8
#define UMA_ZONE_MALLOC     0x0020  /* For malloc() buckets */
#define UMA_ZONE_NOTOUCH    0x0040  /* Don't touch freed memory */
#define UMA_ZONE_MAXBUCKET  0x0080  /* Use maximum bucket size */
#define UMA_ZONE_NOBUCKET   0x0100  /* No per-CPU caching */
#define UMA_ZONE_OFFPAGE    0x0200  /* Use off-page slab header */

/* Debug flags (enabled via boot option or compile-time) */
#define UMA_ZONE_TRASH      0x1000  /* Fill free/alloc with patterns */
#define UMA_ZONE_REDZONE    0x2000  /* Add guard bytes around objects */
#define UMA_ZONE_VTOSLAB    0x4000  /* Track slab from virtual address */
#define UMA_ZONE_LEAK       0x8000  /* Enable leak tracking for this zone */

/*
 * Allocation flags (passed to uma_zalloc)
 */
#define M_WAITOK    0x0001  /* Can wait/sleep for memory */
#define M_NOWAIT    0x0002  /* Do not wait, return NULL if unavailable */
#define M_ZERO      0x0004  /* Zero the allocated memory */

/*
 * Constructor/Destructor function types
 * 
 * ctor: Called when object is allocated (after memory obtained)
 * dtor: Called when object is freed (before memory returned)
 * init: Called once when slab page is first carved into objects
 * fini: Called once when slab page is released
 */
typedef int (*uma_ctor)(void *obj, int size, void *arg, int flags);
typedef void (*uma_dtor)(void *obj, int size, void *arg);
typedef int (*uma_init)(void *obj, int size, int flags);
typedef void (*uma_fini)(void *obj, int size);

/*
 * Reclamation callback type
 * Called under memory pressure to free unused resources
 */
typedef void (*uma_reclaim_t)(void);

/*
 * Per-CPU cache structure (magazine layer)
 * Each CPU has its own cache to avoid contention
 */
#define UMA_CACHE_BUCKET_SIZE   64

struct uma_bucket {
    void    *ub_bucket[UMA_CACHE_BUCKET_SIZE];
    int     ub_cnt;         /* Number of items in bucket */
};

struct uma_cache {
    struct uma_bucket   *uc_freebucket;     /* Currently draining bucket */
    struct uma_bucket   *uc_allocbucket;    /* Currently filling bucket */
    uint64_t            uc_allocs;          /* Allocation count */
    uint64_t            uc_frees;           /* Free count */
};

/*
 * Slab structure - tracks a page of objects
 */
struct uma_slab {
    struct uma_slab     *us_next;       /* Next slab in list */
    struct uma_slab     *us_hnext;      /* Next slab in hash chain */
    uma_zone_t          *us_zone;       /* Owner zone (for safety) */
    void                *us_data;       /* Base of object memory */
    uint32_t            us_freecount;   /* Free objects in slab */
    uint32_t            us_firstfree;   /* Index of first free object */
    uint8_t             *us_freelist;   /* Bitmap or free indices */
};

/*
 * Zone structure - a pool of same-sized objects
 */
struct uma_zone {
    const char          *uz_name;       /* Zone name for debugging */
    
    /* Object parameters */
    size_t              uz_size;        /* Object size (without debug) */
    size_t              uz_align;       /* Object alignment */
    size_t              uz_rsize;       /* Real size (with redzone) */
    size_t              uz_ipers;       /* Items per slab */
    
    /* Callbacks */
    uma_ctor            uz_ctor;        /* Constructor */
    uma_dtor            uz_dtor;        /* Destructor */
    uma_init            uz_init;        /* One-time init per object */
    uma_fini            uz_fini;        /* One-time fini per object */
    void                *uz_arg;        /* Argument to ctor/dtor */
    
    /* Flags */
    uint32_t            uz_flags;       /* UMA_ZONE_* flags */
    
    /* Slab lists */
    uma_slab_t          *uz_full_slabs;     /* No free objects */
    uma_slab_t          *uz_part_slabs;     /* Some free objects */
    uma_slab_t          *uz_free_slabs;     /* All objects free */
    
    /* Statistics */
    uint64_t            uz_allocs;      /* Total allocations */
    uint64_t            uz_frees;       /* Total frees */
    uint32_t            uz_count;       /* Current allocated count */
    uint32_t            uz_limit;       /* Maximum allowed allocations (0=unlimited) */
    uint32_t            uz_max;         /* Maximum ever allocated */
    uint32_t            uz_sleeps;      /* Times slept on alloc */
    
    /* Linked list of all zones */
    struct uma_zone     *uz_next;

    /* Per-CPU caches (array indexed by CPU ID) */
    uma_cache_t         uz_cpu[1];      /* Extended at alloc time */
};

/*
 * Public API
 */

/* Initialize UMA subsystem */
void uma_startup(void);

/* Enable dynamic allocation of zone structures (called by kmem_init) */
void uma_enable_dynamic_alloc(void);

/* Create a zone */
uma_zone_t *uma_zcreate(
    const char *name,
    size_t size,
    uma_ctor ctor,
    uma_dtor dtor,
    uma_init init,
    uma_fini fini,
    int align,
    uint32_t flags
);

/* Destroy a zone */
void uma_zdestroy(uma_zone_t *zone);

/* Allocate from zone */
void *uma_zalloc(uma_zone_t *zone, int flags);

/* Free to zone */
void uma_zfree(uma_zone_t *zone, void *item);

/* Reclaim memory from all zones */
void uma_reclaim(void);

/* Set zone maximum (0 = unlimited) */
void uma_zone_set_max(uma_zone_t *zone, int max);

/* Get current zone count */
int uma_zone_get_cur(uma_zone_t *zone);

/* Reserve (pre-allocate) items into zone */
int uma_zone_reserve(uma_zone_t *zone, int count);

/* Zone statistics */
void uma_zone_stat(uma_zone_t *zone, uint64_t *allocs, uint64_t *frees, int *cur);

/* Leak detection: report zones with outstanding allocations */
void uma_leak_report(void);

/* Check if a specific zone has leaks */
int uma_zone_check_leaks(uma_zone_t *zone);

/*
 * Debug support
 */
void uma_debug_check_redzone_impl(uma_zone_t *zone, void *item);
void uma_debug_poison_free_impl(uma_zone_t *zone, void *item);
void uma_debug_poison_alloc_impl(uma_zone_t *zone, void *item);

/*
 * Validates UMA redzone (0xFE pattern at end of object)
 */
static inline void uma_debug_check_redzone(uma_zone_t *zone, void *item) {
    if (zone->uz_flags & UMA_ZONE_REDZONE) {
        uma_debug_check_redzone_impl(zone, item);
    }
}

static inline void uma_debug_poison_free(uma_zone_t *zone, void *item) {
    if (zone->uz_flags & UMA_ZONE_TRASH) {
        uma_debug_poison_free_impl(zone, item);
    }
}

static inline void uma_debug_poison_alloc(uma_zone_t *zone, void *item) {
     if (zone->uz_flags & UMA_ZONE_TRASH) {
        uma_debug_poison_alloc_impl(zone, item);
    }
}



#endif /* _VM_UMA_H */
