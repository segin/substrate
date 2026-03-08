/*
 * uma_core.c - Universal Memory Allocator Core
 * 
 * Implements FreeBSD-style slab allocator with per-CPU caches.
 */

#include <vm/uma.h>
#include <vm/vm_kmem.h>
#include <arch/i386/pmm.h>
#include <kern/console.h>
#include <stddef.h>
#include <string.h>
#include <vm/vm_kmem.h>
#include <sys/smp.h>

static uma_zone_t *uma_zones = NULL;

/* Number of CPUs (detected at runtime) */
static int uma_ncpu = 1;

#define UMA_MAX_CPUS MAX_CPUS
#define UMA_ZONE_SIZE_MAX (sizeof(uma_zone_t) + (UMA_MAX_CPUS - 1) * sizeof(uma_cache_t))

/* Bootstrap zone pool */
#define UMA_BOOTSTRAP_ZONES 32
static uint8_t uma_bootstrap_mem[UMA_BOOTSTRAP_ZONES * UMA_ZONE_SIZE_MAX] __attribute__((aligned(16)));
static int uma_bootstrap_idx = 0;
static bool uma_dynamic_alloc = false;

/* Bucket pool */
#define UMA_BUCKET_POOL_SIZE 64
static struct uma_bucket uma_bucket_pool[UMA_BUCKET_POOL_SIZE];
static int uma_bucket_idx = 0;

/* Global Slab Hash Table */
#define UMA_HASH_SHIFT  12
#define UMA_HASH_SIZE   4096
#define UMA_HASH_MASK   (UMA_HASH_SIZE - 1)

static uma_slab_t *uma_page_hash[UMA_HASH_SIZE];

static inline uint32_t uma_hash(void *addr) {
    uintptr_t p = (uintptr_t)addr >> UMA_HASH_SHIFT;
    return (uint32_t)(p & UMA_HASH_MASK);
}

static void uma_hash_insert(uma_slab_t *slab) {
    uint32_t bucket = uma_hash(slab->us_data);
    slab->us_hnext = uma_page_hash[bucket];
    uma_page_hash[bucket] = slab;
}

static void uma_hash_remove(uma_slab_t *slab) {
    uint32_t bucket = uma_hash(slab->us_data);
    uma_slab_t **pp = &uma_page_hash[bucket];
    
    while (*pp) {
        if (*pp == slab) {
            *pp = slab->us_hnext;
            slab->us_hnext = NULL;
            return;
        }
        pp = &(*pp)->us_hnext;
    }
}

/* Forward declarations */
static uma_slab_t *uma_slab_alloc(uma_zone_t *zone);
static void uma_slab_free(uma_zone_t *zone, uma_slab_t *slab);
static void *uma_slab_alloc_item(uma_zone_t *zone, uma_slab_t *slab);
static void uma_slab_free_item(uma_zone_t *zone, uma_slab_t *slab, void *item);

/*
 * Initialize UMA subsystem
 */
void uma_startup(void) {
    uma_ncpu = smp_get_cpu_count();
    uma_bootstrap_idx = 0;
    uma_bucket_idx = 0;
    uma_zones = NULL;
    memset(uma_page_hash, 0, sizeof(uma_page_hash));

    kprint("UMA: subsystem initialized\n");
}

/*
 * Enable dynamic allocation of zone structures (called by kmem_init)
 */
void uma_enable_dynamic_alloc(void) {
    uma_dynamic_alloc = true;
    kprint("UMA: Dynamic zone allocation enabled\n");
}

/*
 * Allocate a bucket from the pool
 */
static struct uma_bucket *uma_bucket_alloc(void) {
    if (uma_bucket_idx >= UMA_BUCKET_POOL_SIZE) {
        return NULL;
    }
    struct uma_bucket *b = &uma_bucket_pool[uma_bucket_idx++];
    b->ub_cnt = 0;
    return b;
}

/*
 * Create a new zone
 */
uma_zone_t *uma_zcreate(
    const char *name,
    size_t size,
    uma_ctor ctor,
    uma_dtor dtor,
    uma_init init,
    uma_fini fini,
    int align,
    uint32_t flags
) {
    uma_zone_t *zone;
    
    /* Allocate zone structure */
    if (!uma_dynamic_alloc) {
        if (uma_bootstrap_idx < UMA_BOOTSTRAP_ZONES) {
            zone = (uma_zone_t *)&uma_bootstrap_mem[uma_bootstrap_idx * UMA_ZONE_SIZE_MAX];
            uma_bootstrap_idx++;
            // Zero the entire max zone size to be safe
            memset(zone, 0, UMA_ZONE_SIZE_MAX);
        } else {
            kprint("UMA: Bootstrap zones exhausted!\n");
            return NULL;
        }
    } else {
        zone = kzalloc(sizeof(uma_zone_t) + (uma_ncpu - 1) * sizeof(uma_cache_t));
        if (!zone) return NULL;
    }
    zone->uz_name = name;
    zone->uz_size = size;
    zone->uz_flags = flags;
    
    /* Alignment: minimum of requested or natural alignment */
    if (align == 0) {
        /* Default to word alignment */
        zone->uz_align = sizeof(void *);
    } else {
        /* Round up to power of 2 */
        zone->uz_align = align;
    }
    
    /* Cache line alignment for performance */
    if (zone->uz_align < 64 && size >= 64) {
        zone->uz_align = 64;
    }
    
    /* Calculate real size with redzone padding */
    zone->uz_rsize = size;
    if (flags & UMA_ZONE_REDZONE) {
        zone->uz_rsize += UMA_REDZONE_SIZE * 2;
    }
    
    /* Ensure size is at least pointer-sized for free list */
    if (zone->uz_rsize < sizeof(void *)) {
        zone->uz_rsize = sizeof(void *);
    }
    
    /* Round up to alignment */
    zone->uz_rsize = (zone->uz_rsize + zone->uz_align - 1) & ~(zone->uz_align - 1);
    
    /* Calculate items per slab (4k pages), accounting for slab overhead */
    
    /* Check if off-page management is better (or required) */
    /* 1. Required if object is too large (e.g. > 2048 with overhead) */
    /* 2. Better if we can fit more items by moving header out */
    
    // Items we can fit if header is ON page
    // (4096 - header) / size  vs  4096 / size
    
    // Simplistic check: If request > 1/2 page, force offpage to get 1 item.
    
    // Note: offpage still needs freelist if we don't put it in the slab header
    // But standard UMA often puts freelist in the item itself when free, or a bitmap.
    // implementation uses us_freelist pointer at end of header.
    // If offpage, header + freelist bitmap must be allocated from slab_zone.
    
    /*
     * Keep small/medium zones on-page.
     *
     * Off-page slab metadata currently allocates headers via kzalloc(),
     * which re-enters UMA. If most small zones are forced off-page,
     * first allocations can recurse deeply and stall early boot.
     *
     * Restrict off-page slabs to large objects where on-page metadata
     * cannot fit efficiently.
     */
    if (size > 2048) {
        zone->uz_flags |= UMA_ZONE_OFFPAGE;
        zone->uz_ipers = 4096 / zone->uz_rsize;
    } else {
        /* On-page slab header */
        /* Formula: uz_ipers * uz_rsize + sizeof(uma_slab_t) + uz_ipers <= 4096 */
        size_t per_item = zone->uz_rsize + 1; /* +1 for freelist byte */
        zone->uz_ipers = (4096 - sizeof(uma_slab_t)) / per_item;
    }
    
    if (zone->uz_ipers == 0) {
        zone->uz_ipers = 1; 
    }
    
    /* Check if we need off-page slab headers */
    size_t slab_overhead = sizeof(uma_slab_t) + zone->uz_ipers;
    if (zone->uz_rsize * zone->uz_ipers + slab_overhead > 4096) {
        zone->uz_flags |= UMA_ZONE_OFFPAGE;
    }

    /* Set callbacks */
    zone->uz_ctor = ctor;
    zone->uz_dtor = dtor;
    zone->uz_init = init;
    zone->uz_fini = fini;
    zone->uz_arg = NULL;
    
    /* Initialize per-CPU cache */
    for (int i = 0; i < uma_ncpu; i++) {
        zone->uz_cpu[i].uc_freebucket = NULL;
        zone->uz_cpu[i].uc_allocbucket = NULL;
        zone->uz_cpu[i].uc_allocs = 0;
        zone->uz_cpu[i].uc_frees = 0;
    }
    
    /* Initialize slab lists */
    zone->uz_full_slabs = NULL;
    zone->uz_part_slabs = NULL;
    zone->uz_free_slabs = NULL;
    
    /* Link to global zone list */
    zone->uz_next = uma_zones;
    uma_zones = zone;
    
    // Debug print
    kprint("UMA: Zone created: ");
    kprint(name);
    if (zone->uz_flags & UMA_ZONE_OFFPAGE) kprint(" [OFFPAGE]");
    kprint("\n");
    
    return zone;
}

/*
 * Destroy a zone
 */
void uma_zdestroy(uma_zone_t *zone) {
    if (!zone) return;
    
    /* Free all slabs */
    uma_slab_t *slab, *next;
    
    for (slab = zone->uz_full_slabs; slab; slab = next) {
        next = slab->us_next;
        uma_slab_free(zone, slab);
    }
    for (slab = zone->uz_part_slabs; slab; slab = next) {
        next = slab->us_next;
        uma_slab_free(zone, slab);
    }
    for (slab = zone->uz_free_slabs; slab; slab = next) {
        next = slab->us_next;
        uma_slab_free(zone, slab);
    }
    
    /* Remove from global list */
    if (uma_zones == zone) {
        uma_zones = zone->uz_next;
    } else {
        for (uma_zone_t *z = uma_zones; z; z = z->uz_next) {
            if (z->uz_next == zone) {
                z->uz_next = zone->uz_next;
                break;
            }
        }
    }

    /* Free zone structure if it was dynamically allocated */
    uintptr_t zaddr = (uintptr_t)zone;
    uintptr_t bstart = (uintptr_t)uma_bootstrap_mem;
    uintptr_t bend = bstart + sizeof(uma_bootstrap_mem);

    if (zaddr < bstart || zaddr >= bend) {
        kfree(zone, sizeof(uma_zone_t) + (uma_ncpu - 1) * sizeof(uma_cache_t));
    }
}

/*
 * Allocate a slab page and carve into objects
 */
static uma_slab_t *uma_slab_alloc(uma_zone_t *zone) {
    void *page = NULL;
    uma_slab_t *slab = NULL;

    /* Check if we need off-page slab headers */
    if (zone->uz_flags & UMA_ZONE_OFFPAGE) {
        /* Off-page slab header */
        size_t total_size = zone->uz_rsize * zone->uz_ipers;
        size_t pages_needed = (total_size + 4095) / 4096;

        if (pages_needed > 1) {
            page = pmm_alloc_contiguous(pages_needed);
        } else {
            page = pmm_alloc_block();
        }

        if (!page) {
            extern void kprint(const char*);
            kprint("uma_slab_alloc: pmm_alloc failed for off-page slab!\n");
            return NULL;
        }

        /* Allocate separate slab header */
        slab = kzalloc(sizeof(uma_slab_t) + zone->uz_ipers);
        if (!slab) {
            if (pages_needed > 1) {
                pmm_free_contiguous(page, pages_needed);
            } else {
                pmm_free_block(page);
            }
            return NULL;
        }

        slab->us_data = page;
    } else {
        /* On-page slab header */
        /* Allocate raw page from PMM */
        page = pmm_alloc_block();
        if (!page) {
            extern void kprint(const char*);
            kprint("uma_slab_alloc: pmm_alloc_block failed!\n");
            return NULL;
        }

        size_t slab_overhead = sizeof(uma_slab_t) + zone->uz_ipers;

        if (zone->uz_rsize * zone->uz_ipers + slab_overhead <= 4096) {
            /* On-page slab header */
            slab = (uma_slab_t *)((uintptr_t)page + 4096 - slab_overhead);
            slab->us_data = page;
        } else {
            /* This should not be reached if UMA_ZONE_OFFPAGE logic in uma_zcreate is correct */
            extern void kprint(const char*);
            kprint("uma_slab_alloc: slab too large for on-page header but OFF_PAGE not set!\n");
            pmm_free_block(page);
            return NULL;
        }
    }
    slab->us_zone = zone;
    slab->us_freecount = zone->uz_ipers;
    slab->us_firstfree = 0;
    slab->us_next = NULL;
    
    /* Insert into global hash */
    uma_hash_insert(slab);
    
    /* Initialize free list (sequential indices) */
    
    /*
     * Freelist map follows the slab structure in memory.
     * For OFF_PAGE: allocated via kzalloc(sizeof(uma_slab_t) + ipers)
     * For ON_PAGE: placed at end of page, space included in slab_overhead
     */
    slab->us_freelist = (uint8_t *)(slab + 1);
    
    for (uint32_t i = 0; i < zone->uz_ipers; i++) {
        slab->us_freelist[i] = (i + 1 < zone->uz_ipers) ? (i + 1) : 0xFF;
    }
    
    /* Call init callback on each object and initialize redzones */
    if (zone->uz_init || (zone->uz_flags & UMA_ZONE_REDZONE)) {
        for (uint32_t i = 0; i < zone->uz_ipers; i++) {
            void *obj = (void *)((uintptr_t)page + i * zone->uz_rsize);
            void *item = obj;

            /* Initialize redzone pattern */
            if (zone->uz_flags & UMA_ZONE_REDZONE) {
                item = (void *)((uintptr_t)obj + UMA_REDZONE_SIZE);
                uma_debug_fill_redzone(zone, item);
            }
            
            if (zone->uz_init) {
                zone->uz_init(item, zone->uz_size, 0);
            }
        }
    }
    
    return slab;
}

/*
 * Free a slab back to PMM
 */
static void uma_slab_free(uma_zone_t *zone, uma_slab_t *slab) {
    /* Call fini callback on each object */
    if (zone->uz_fini) {
        for (uint32_t i = 0; i < zone->uz_ipers; i++) {
            void *obj = (void *)((uintptr_t)slab->us_data + i * zone->uz_rsize);
            zone->uz_fini(obj, zone->uz_size);
        }
    }
    
    /* Remove from global hash */
    uma_hash_remove(slab);

    if (zone->uz_flags & UMA_ZONE_OFFPAGE) {
        size_t total_size = zone->uz_rsize * zone->uz_ipers;
        size_t pages_needed = (total_size + 4095) / 4096;

        if (pages_needed > 1) {
            pmm_free_contiguous(slab->us_data, pages_needed);
        } else {
            pmm_free_block(slab->us_data);
        }

        kfree(slab, sizeof(uma_slab_t) + zone->uz_ipers);
    } else {
        pmm_free_block(slab->us_data);
    }
}

/*
 * Allocate one item from a slab
 */
static void *uma_slab_alloc_item(uma_zone_t *zone, uma_slab_t *slab) {
    if (slab->us_freecount == 0) return NULL;
    
    uint32_t idx = slab->us_firstfree;
    void *obj = (void *)((uintptr_t)slab->us_data + idx * zone->uz_rsize);
    
    /* Update free list */
    slab->us_firstfree = slab->us_freelist[idx];
    slab->us_freelist[idx] = 0xFF; /* Mark as allocated */
    slab->us_freecount--;
    
    /* Handle redzone offset */
    if (zone->uz_flags & UMA_ZONE_REDZONE) {
        obj = (void *)((uintptr_t)obj + UMA_REDZONE_SIZE);
    }
    
    return obj;
}

/*
 * Free one item back to a slab
 */
static void uma_slab_free_item(uma_zone_t *zone, uma_slab_t *slab, void *item) {
    /* Handle redzone offset */
    if (zone->uz_flags & UMA_ZONE_REDZONE) {
        item = (void *)((uintptr_t)item - UMA_REDZONE_SIZE);
    }
    
    /* Calculate index */
    uint32_t idx = ((uintptr_t)item - (uintptr_t)slab->us_data) / zone->uz_rsize;
    
    /* Add to free list */
    slab->us_freelist[idx] = slab->us_firstfree;
    slab->us_firstfree = idx;
    slab->us_freecount++;
}

/*
 * Find which slab contains an item
 */
/*
 * Find which slab contains an item
 */
static uma_slab_t *uma_find_slab(uma_zone_t *zone, void *item) {
    uintptr_t page_addr = (uintptr_t)item & ~(uintptr_t)0xFFF;
    uint32_t bucket = uma_hash((void*)page_addr);
    
    for (uma_slab_t *s = uma_page_hash[bucket]; s; s = s->us_hnext) {
        if ((uintptr_t)s->us_data == page_addr) {
            if (s->us_zone != zone) {
                extern void kprint(const char*); // Temporary debug
                kprint("uma_find_slab: slab zone mismatch!\n");
                return NULL;
            }
            return s;
        }
    }
    
    return NULL;
}

/*
 * Remove slab from a list
 */
static void uma_slab_unlink(uma_slab_t **list, uma_slab_t *slab) {
    if (*list == slab) {
        *list = slab->us_next;
    } else {
        for (uma_slab_t *s = *list; s; s = s->us_next) {
            if (s->us_next == slab) {
                s->us_next = slab->us_next;
                break;
            }
        }
    }
    slab->us_next = NULL;
}

/*
 * Allocate from zone (slow path - slab layer)
 */
static void *uma_zalloc_slab(uma_zone_t *zone, int flags) {
    void *item = NULL;
    uma_slab_t *slab;
    (void)flags; /* M_NOWAIT/M_WAITOK handled at pmm layer */
    
    /* Try partial slabs first */
    if (zone->uz_part_slabs) {
        slab = zone->uz_part_slabs;
        item = uma_slab_alloc_item(zone, slab);
        
        /* Move to full list if exhausted */
        if (slab->us_freecount == 0) {
            uma_slab_unlink(&zone->uz_part_slabs, slab);
            slab->us_next = zone->uz_full_slabs;
            zone->uz_full_slabs = slab;
        }
    }
    
    /* Try free slabs */
    if (!item && zone->uz_free_slabs) {
        slab = zone->uz_free_slabs;
        uma_slab_unlink(&zone->uz_free_slabs, slab);
        slab->us_next = zone->uz_part_slabs;
        zone->uz_part_slabs = slab;
        item = uma_slab_alloc_item(zone, slab);
    }
    
    /* Allocate new slab */
    if (!item) {
        /* M_NOWAIT means "don't sleep", not "don't allocate" 
         * We still try to allocate, but pmm_alloc won't block */
        slab = uma_slab_alloc(zone);
        if (!slab) return NULL;
        
        slab->us_next = zone->uz_part_slabs;
        zone->uz_part_slabs = slab;
        item = uma_slab_alloc_item(zone, slab);
    }
    
    return item;
}

/*
 * Free to zone (slow path - slab layer)
 */
static void uma_zfree_slab(uma_zone_t *zone, void *item) {
    uma_slab_t *slab = uma_find_slab(zone, item);
    if (!slab) return;
    
    bool was_full = (slab->us_freecount == 0);
    uma_slab_free_item(zone, slab, item);
    
    /* Transition from full to partial */
    if (was_full) {
        uma_slab_unlink(&zone->uz_full_slabs, slab);
        slab->us_next = zone->uz_part_slabs;
        zone->uz_part_slabs = slab;
    }
    
    /* Transition from partial to free */
    if (slab->us_freecount == zone->uz_ipers) {
        uma_slab_unlink(&zone->uz_part_slabs, slab);
        
        if (zone->uz_flags & UMA_ZONE_NOFREE) {
            /* Keep slab cached */
            slab->us_next = zone->uz_free_slabs;
            zone->uz_free_slabs = slab;
        } else {
            /* Return to PMM */
            uma_slab_free(zone, slab);
        }
    }
}

/*
 * Get current CPU ID
 */
static inline int uma_curcpu(void) {
    return smp_get_cpu_id();
}

/*
 * Allocate from zone
 */
void *uma_zalloc(uma_zone_t *zone, int flags) {
    if (!zone) {
        extern void kprint(const char*);
        kprint("uma_zalloc: zone is NULL!\n");
        return NULL;
    }
    
    void *item = NULL;
    int cpu = uma_curcpu();
    uma_cache_t *cache = &zone->uz_cpu[cpu];
    
    /* Fast path: try per-CPU cache */
    if (!(zone->uz_flags & UMA_ZONE_NOBUCKET)) {
        if (cache->uc_allocbucket == NULL) {
            cache->uc_allocbucket = uma_bucket_alloc();
        }

        struct uma_bucket *bucket = cache->uc_allocbucket;

        if (bucket) {
            /* If alloc bucket is empty, try to swap with free bucket */
            if (bucket->ub_cnt == 0 && cache->uc_freebucket && cache->uc_freebucket->ub_cnt > 0) {
                struct uma_bucket *tmp = cache->uc_allocbucket;
                cache->uc_allocbucket = cache->uc_freebucket;
                cache->uc_freebucket = tmp;
                bucket = cache->uc_allocbucket;
            }

            if (bucket->ub_cnt > 0) {
                item = bucket->ub_bucket[--bucket->ub_cnt];
                cache->uc_allocs++;
                zone->uz_allocs++;
                zone->uz_count++;
                if (zone->uz_count > zone->uz_max) {
                    zone->uz_max = zone->uz_count;
                }
                goto out;
            }
        }
    }

    /* Check zone limits before allocating new memory */
    if (zone->uz_limit > 0 && zone->uz_count >= zone->uz_limit) {
        return NULL;
    }
    
    /* Slow path: allocate from slab */
    item = uma_zalloc_slab(zone, flags);
    if (!item) {
        extern void kprint(const char*);
        kprint("uma_zalloc: slab alloc failed for ");
        kprint(zone->uz_name);
        kprint("\n");
        return NULL;
    }
    
    zone->uz_allocs++;
    zone->uz_count++;
    if (zone->uz_count > zone->uz_max) {
        zone->uz_max = zone->uz_count;
    }
    
out:
    if (item) {
        /* Debug: poison allocated memory */
        uma_debug_poison_alloc(zone, item);
        
        /* Zero if requested */
        if (flags & M_ZERO) {
            memset(item, 0, zone->uz_size);
        } else if (zone->uz_flags & UMA_ZONE_ZINIT) {
            memset(item, 0, zone->uz_size);
        }
        
        /* Call constructor */
        if (zone->uz_ctor) {
            if (zone->uz_ctor(item, zone->uz_size, zone->uz_arg, flags) != 0) {
                uma_zfree(zone, item);
                return NULL;
            }
        }
    }
    
    return item;
}

/*
 * Free to zone
 */
void uma_zfree(uma_zone_t *zone, void *item) {
    if (!zone || !item) return;
    
    /* Call destructor */
    if (zone->uz_dtor) {
        zone->uz_dtor(item, zone->uz_size, zone->uz_arg);
    }
    
    /* Debug: check redzone before free */
    uma_debug_check_redzone(zone, item);
    
    /* Debug: poison freed memory */
    uma_debug_poison_free(zone, item);
    
    int cpu = uma_curcpu();
    uma_cache_t *cache = &zone->uz_cpu[cpu];
    
    /* Fast path: try per-CPU cache */
    if (!(zone->uz_flags & UMA_ZONE_NOBUCKET)) {
        if (!cache->uc_freebucket) {
            cache->uc_freebucket = uma_bucket_alloc();
        }
        
        if (cache->uc_freebucket && cache->uc_freebucket->ub_cnt < UMA_CACHE_BUCKET_SIZE) {
            cache->uc_freebucket->ub_bucket[cache->uc_freebucket->ub_cnt++] = item;
            cache->uc_frees++;
            zone->uz_frees++;
            if (zone->uz_count > 0) {
                zone->uz_count--;
            }
            return;
        }
    }
    
    /* Slow path: free to slab */
    uma_zfree_slab(zone, item);
    zone->uz_frees++;
    zone->uz_count--;
}

/*
 * Reclaim memory from all zones
 */
void uma_reclaim(void) {
    for (uma_zone_t *zone = uma_zones; zone; zone = zone->uz_next) {
        if (zone->uz_flags & UMA_ZONE_NOFREE) continue;
        
        /* Drain per-CPU buckets */
        for (int cpu = 0; cpu < uma_ncpu; cpu++) {
            uma_cache_t *cache = &zone->uz_cpu[cpu];
            
            if (cache->uc_allocbucket) {
                struct uma_bucket *b = cache->uc_allocbucket;
                while (b->ub_cnt > 0) {
                     uma_zfree_slab(zone, b->ub_bucket[--b->ub_cnt]);
                }
            }
            
            if (cache->uc_freebucket) {
                struct uma_bucket *b = cache->uc_freebucket;
                while (b->ub_cnt > 0) {
                     uma_zfree_slab(zone, b->ub_bucket[--b->ub_cnt]);
                }
            }
        }

        /* Free all completely free slabs */
        uma_slab_t *slab, *next;
        for (slab = zone->uz_free_slabs; slab; slab = next) {
            next = slab->us_next;
            uma_slab_free(zone, slab);
        }
        zone->uz_free_slabs = NULL;
    }
}

/*
 * Zone statistics
 */
void uma_zone_stat(uma_zone_t *zone, uint64_t *allocs, uint64_t *frees, int *cur) {
    if (allocs) *allocs = zone->uz_allocs;
    if (frees) *frees = zone->uz_frees;
    if (cur) *cur = zone->uz_count;
}

int uma_zone_get_cur(uma_zone_t *zone) {
    return zone->uz_count;
}

void uma_zone_set_max(uma_zone_t *zone, int max) {
    if (zone) {
        /* Set the limit. 0 means unlimited. */
        zone->uz_limit = (max < 0) ? 0 : (uint32_t)max;
    }
}

int uma_zone_reserve(uma_zone_t *zone, int count) {
    int reserved = 0;
    while (reserved < count) {
        uma_slab_t *slab = uma_slab_alloc(zone);
        if (!slab) break;
        slab->us_next = zone->uz_free_slabs;
        zone->uz_free_slabs = slab;
        reserved += zone->uz_ipers;
    }
    return reserved;
}

/*
 * Leak detection: check if a zone has outstanding allocations
 */
int uma_zone_check_leaks(uma_zone_t *zone) {
    if (!zone) return 0;
    return zone->uz_count;
}

/*
 * Leak detection: report all zones with outstanding allocations
 */
void uma_leak_report(void) {
    int total_leaks = 0;
    
    kprint("UMA Leak Report:\n");
    
    for (uma_zone_t *zone = uma_zones; zone; zone = zone->uz_next) {
        if (zone->uz_count > 0) {
            kprint("  Zone '");
            kprint(zone->uz_name);
            kprint("': ");
            // Print count (simple decimal conversion)
            char buf[16];
            int n = zone->uz_count;
            int i = 0;
            if (n == 0) {
                buf[i++] = '0';
            } else {
                char tmp[16];
                int j = 0;
                while (n > 0) {
                    tmp[j++] = '0' + (n % 10);
                    n /= 10;
                }
                while (j > 0) {
                    buf[i++] = tmp[--j];
                }
            }
            buf[i] = '\0';
            kprint(buf);
            kprint(" outstanding allocations\n");
            total_leaks += zone->uz_count;
        }
    }
    
    if (total_leaks == 0) {
        kprint("  No leaks detected.\n");
    } else {
        kprint("  Total: ");
        char buf[16];
        int n = total_leaks;
        int i = 0;
        if (n == 0) {
            buf[i++] = '0';
        } else {
            char tmp[16];
            int j = 0;
            while (n > 0) {
                tmp[j++] = '0' + (n % 10);
                n /= 10;
            }
            while (j > 0) {
                buf[i++] = tmp[--j];
            }
        }
        buf[i] = '\0';
        kprint(buf);
        kprint(" leaked objects\n");
    }
}
