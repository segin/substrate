#include <stddef.h>
#include <string.h>

#include <machine/intr.h>
#include <kern/console.h>
#include <kern/panic.h>
#include <sys/lock.h>
#include <vm/phys_mem.h>



// Generic PMM Data Structures
#define PMM_MAX_ORDER 11
#define PMM_BLOCK_SIZE 4096

static vm_page_t *vm_phys_free_lists[PMM_MAX_ORDER];
static size_t vm_phys_free_count;

static spinlock_t vm_phys_lock;
static vm_page_t *vm_phys_page_array;
static size_t vm_phys_page_count;

static size_t vm_phys_low_watermark = 128; // 512 KB target

/*
 * The high segment: physical memory that begins at vm_phys_hi_base, well
 * above the end of the page array (the x86_64 kernel's RAM at and above
 * 4 GiB; unused, and all zero, on i386).  It has a page array and free
 * lists of its own, so it is a second zone:
 *
 *   - vm_phys_alloc_page() and vm_phys_alloc_contiguous(), which is what
 *     the VM uses for pages it maps into processes, take from the high
 *     zone first and fall back to the low one;
 *   - the _below() forms, which is what pmm_alloc_block() and so every
 *     kernel and driver allocation use, only ever see the low zone, and
 *     find a page there at once instead of walking past every high page
 *     on a shared list.
 *
 * A buddy block never spans the two: the segments are not adjacent, so
 * the buddy of a block at either edge has no page structure.
 */
static vm_page_t *vm_phys_hi_free_lists[PMM_MAX_ORDER];
static size_t vm_phys_hi_free_count;
static vm_page_t *vm_phys_hi_array;
static uintptr_t vm_phys_hi_base;
static size_t vm_phys_hi_count;

vm_page_t *vm_phys_paddr_to_page(uintptr_t pa);

static int vm_phys_page_is_high(const vm_page_t *page) {
    return vm_phys_hi_array != NULL && page->phys_addr >= vm_phys_hi_base;
}

/* The free lists and the free-page counter of the zone `page` is in. */
static vm_page_t **vm_phys_lists_of(const vm_page_t *page) {
    return vm_phys_page_is_high(page) ? vm_phys_hi_free_lists
                                      : vm_phys_free_lists;
}

static size_t *vm_phys_count_of(const vm_page_t *page) {
    return vm_phys_page_is_high(page) ? &vm_phys_hi_free_count
                                      : &vm_phys_free_count;
}

static void vm_phys_reset_page_metadata(vm_page_t *page) {
    if (!page) {
        return;
    }

    page->next = NULL;
    page->prev = NULL;
    page->object = NULL;
    page->pindex = 0;
    page->flags = 0;
    page->wire_count = 0;
    page->ref_count = 0;
    page->access_count = 0;
    page->age = 0;
    page->last_modified = 0;
    page->order = 0;
    page->pv_list = NULL;
    page->magic_head = VM_PAGE_MAGIC;
    page->magic_tail = VM_PAGE_MAGIC;
}

static void vm_phys_prepare_allocated_block(vm_page_t *page, int order) {
    if (!page || order < 0) {
        return;
    }

    size_t count = (size_t)1U << order;
    for (size_t i = 0; i < count; i++) {
        vm_page_t *p = &page[i];
        /* Tripwire: a page about to be handed out must not already be
         * marked allocated.  Catches buddy-allocator accounting bugs
         * at the moment they would hand the same page out twice. */
        if (p->flags & PG_PMM_ALLOC) {
            kprintf("VM: buddy DOUBLE-ALLOC pfn=%lu pa=0x%08lx order=%d "
                    "flags=0x%04x\n",
                    (unsigned long)(p->phys_addr / PMM_BLOCK_SIZE),
                    (unsigned long)p->phys_addr, order, p->flags);
            panic("vm_phys: double allocation");
        }
        uintptr_t pa = p->phys_addr;
        vm_phys_reset_page_metadata(p);
        p->phys_addr = pa;
        p->ref_count = 1;
        p->flags |= PG_PMM_ALLOC;
    }
}

// Internal Helpers
static void vm_phys_buddy_enqueue(int order, vm_page_t *page) {
    vm_page_t **lists = vm_phys_lists_of(page);

    page->next = lists[order];
    page->prev = NULL;
    if (lists[order]) {
        lists[order]->prev = page;
    }
    lists[order] = page;
    page->order = order;
    page->flags |= PG_FREE;
}

static void vm_phys_buddy_dequeue(int order, vm_page_t *page) {
    if (page->prev) {
        page->prev->next = page->next;
    } else {
        vm_phys_lists_of(page)[order] = page->next;
    }
    if (page->next) {
        page->next->prev = page->prev;
    }
    page->next = NULL;
    page->prev = NULL;
    page->flags &= ~PG_FREE;
}

/*
 * Is this really one of our vm_page_t's?
 *
 * The page array is a flat table, so membership is exactly decidable: the
 * pointer has to land inside it, on an element boundary, and carry the magic
 * canaries.  Checking the bounds FIRST matters -- the whole point is to be
 * callable on a pointer that may be garbage, and vm_page_valid() dereferences.
 */
static int vm_phys_page_in_array(const vm_page_t *p, const vm_page_t *array,
                                 size_t count) {
    uintptr_t off;

    if (!array || !count || (uintptr_t)p < (uintptr_t)array) {
        return 0;
    }
    off = (uintptr_t)p - (uintptr_t)array;
    if (off >= count * sizeof(vm_page_t)) {
        return 0;
    }
    return (off % sizeof(vm_page_t)) == 0;
}

int vm_phys_page_is_valid(const vm_page_t *p) {
    if (!p) {
        return 0;
    }
    if (!vm_phys_page_in_array(p, vm_phys_page_array, vm_phys_page_count) &&
        !vm_phys_page_in_array(p, vm_phys_hi_array, vm_phys_hi_count)) {
        return 0;
    }
    return vm_page_valid(p);
}

static vm_page_t *vm_phys_find_free_block_head(uintptr_t pa, int *out_order) {
    for (int order = PMM_MAX_ORDER - 1; order >= 0; order--) {
        uintptr_t block_size = ((uintptr_t)1 << order) * PMM_BLOCK_SIZE;
        uintptr_t block_base = pa & ~(block_size - 1);
        vm_page_t *head = vm_phys_paddr_to_page(block_base);
        if (!head) continue;
        if ((head->flags & PG_FREE) && head->order == order) {
            if (out_order) *out_order = order;
            return head;
        }
    }
    return NULL;
}

vm_page_t *vm_phys_paddr_to_page(uintptr_t pa) {
    if (!vm_phys_page_array) {
        return NULL;
    }
    size_t idx = pa / PMM_BLOCK_SIZE;
    if (idx < vm_phys_page_count) {
        return &vm_phys_page_array[idx];
    }
    if (vm_phys_hi_array && pa >= vm_phys_hi_base) {
        idx = (pa - vm_phys_hi_base) / PMM_BLOCK_SIZE;
        if (idx < vm_phys_hi_count) {
            return &vm_phys_hi_array[idx];
        }
    }
    return NULL;
}

static vm_page_t *vm_phys_alloc_from(vm_page_t **lists, size_t *free_count,
                                     int order);

/* A block from anywhere: the high zone while it has one, so that the low
 * zone is left for the allocations that can use nothing else. */
static vm_page_t* vm_phys_alloc_locked(int order) {
    vm_page_t *page;

    if (order >= PMM_MAX_ORDER) return NULL;

    page = vm_phys_alloc_from(vm_phys_hi_free_lists, &vm_phys_hi_free_count,
                              order);
    if (page) {
        return page;
    }
    return vm_phys_alloc_from(vm_phys_free_lists, &vm_phys_free_count, order);
}

static vm_page_t *vm_phys_alloc_from(vm_page_t **lists, size_t *free_count,
                                     int order) {
    for (int i = order; i < PMM_MAX_ORDER; i++) {
        if (lists[i]) {
            vm_page_t *page = lists[i];
            vm_phys_buddy_dequeue(i, page);

            while (i > order) {
                i--;
                uintptr_t buddy_pa = page->phys_addr + ((1 << i) * PMM_BLOCK_SIZE);
                vm_page_t *buddy = vm_phys_paddr_to_page(buddy_pa);
                if (buddy) {
                    vm_phys_buddy_enqueue(i, buddy);
                }
            }
            
            *free_count -= ((size_t)1U << order);
            vm_phys_prepare_allocated_block(page, order);
            return page;
        }
    }
    return NULL;
}

static int vm_phys_block_within_limit(vm_page_t *page, int order, uintptr_t phys_limit) {
    uintptr_t block_size;

    if (!page || phys_limit == 0) {
        return 0;
    }

    block_size = ((uintptr_t)1U << order) * PMM_BLOCK_SIZE;
    if (page->phys_addr >= phys_limit) {
        return 0;
    }
    if (block_size > (phys_limit - page->phys_addr)) {
        return 0;
    }
    return 1;
}

static vm_page_t *vm_phys_alloc_locked_below(int order, uintptr_t phys_limit) {
    if (phys_limit == 0) {
        return vm_phys_alloc_locked(order);
    }

    if (order >= PMM_MAX_ORDER) {
        return NULL;
    }

    /* Only the low zone is searched: every limit a caller passes is the
     * ceiling for kernel and device memory, which lies below the high
     * segment. */

    for (int i = order; i < PMM_MAX_ORDER; i++) {
        vm_page_t *page = vm_phys_free_lists[i];

        while (page && !vm_phys_block_within_limit(page, i, phys_limit)) {
            page = page->next;
        }

        if (page) {
            vm_phys_buddy_dequeue(i, page);

            while (i > order) {
                i--;
                uintptr_t buddy_pa = page->phys_addr + (((uintptr_t)1U << i) * PMM_BLOCK_SIZE);
                vm_page_t *buddy = vm_phys_paddr_to_page(buddy_pa);
                if (buddy) {
                    vm_phys_buddy_enqueue(i, buddy);
                }
            }

            vm_phys_free_count -= ((size_t)1U << order);
            vm_phys_prepare_allocated_block(page, order);
            return page;
        }
    }

    return NULL;
}

static void vm_phys_free_locked(vm_page_t *page, int order) {
    if (!page || order >= PMM_MAX_ORDER) return;

    *vm_phys_count_of(page) += ((size_t)1U << order);

    while (order < PMM_MAX_ORDER - 1) {
        uintptr_t buddy_pa = page->phys_addr ^ ((1 << order) * PMM_BLOCK_SIZE);
        vm_page_t *buddy = vm_phys_paddr_to_page(buddy_pa);

        if (buddy && (buddy->flags & PG_FREE) && (buddy->order == order)) {
            vm_phys_buddy_dequeue(order, buddy);
            if (buddy->phys_addr < page->phys_addr) {
                page = buddy;
            }
            order++;
        } else {
            break;
        }
    }
    vm_phys_buddy_enqueue(order, page);
}

// Public APIs

void vm_phys_early_init(void *bitmap, size_t bitmap_size, vm_page_t *pages, size_t page_count) {
    (void)bitmap;       /* Bitmap parameter kept for API compat, but unused */
    (void)bitmap_size;
    
    spinlock_init(&vm_phys_lock, "vm_phys");
    memset(vm_phys_free_lists, 0, sizeof(vm_phys_free_lists));
    vm_phys_page_array = pages;
    vm_phys_page_count = page_count;
    vm_phys_free_count = 0;  /* Will be set by vm_phys_add_range */
    
    // Init page array with magic canaries
    if (pages) {
        memset(pages, 0, page_count * sizeof(vm_page_t));
        for (size_t i = 0; i < page_count; i++) {
            pages[i].magic_head = VM_PAGE_MAGIC;
            pages[i].magic_tail = VM_PAGE_MAGIC;
            pages[i].phys_addr = i * PMM_BLOCK_SIZE;
            pages[i].flags = 0;  /* Not free yet - will be added by add_range */
        }
    }
}

void vm_phys_add_range(uintptr_t start, uintptr_t end) {
    // Round to page boundaries
    start = (start + PMM_BLOCK_SIZE - 1) & ~(PMM_BLOCK_SIZE - 1);
    end = end & ~(PMM_BLOCK_SIZE - 1);
    
    if (start >= end) return;

    uintptr_t addr = start;
    uint32_t flags = intr_disable();
    spinlock_acquire(&vm_phys_lock);
    
    while (addr < end) {
        int order = 0;
        while (order < PMM_MAX_ORDER - 1) {
            uintptr_t block_size = (1 << (order + 1)) * PMM_BLOCK_SIZE;
            if ((addr & (block_size - 1)) != 0) break;
            if (addr + block_size > end) break;
            order++;
        }
        
        vm_page_t *page = vm_phys_paddr_to_page(addr);
        if (page) {
             vm_phys_buddy_enqueue(order, page);
             *vm_phys_count_of(page) += ((size_t)1U << order);
        }
        
        addr += (1 << order) * PMM_BLOCK_SIZE;
    }
    
    spinlock_release(&vm_phys_lock);
    intr_restore(flags);
}

vm_page_t *vm_phys_alloc_page(void) {
    uint32_t flags = intr_disable();
    spinlock_acquire(&vm_phys_lock);

    vm_page_t *page = vm_phys_alloc_locked(0);
    size_t free_left = vm_phys_free_count + vm_phys_hi_free_count;

    spinlock_release(&vm_phys_lock);
    intr_restore(flags);

    if (page && free_left < vm_phys_low_watermark) {
        vm_page_wakeup_daemon();
    }

    return page;
}

vm_page_t *vm_phys_alloc_page_below(uintptr_t phys_limit) {
    uint32_t flags = intr_disable();
    spinlock_acquire(&vm_phys_lock);

    vm_page_t *page = vm_phys_alloc_locked_below(0, phys_limit);
    size_t free_left = vm_phys_free_count;

    spinlock_release(&vm_phys_lock);
    intr_restore(flags);

    if (page && free_left < vm_phys_low_watermark) {
        vm_page_wakeup_daemon();
    }

    return page;
}

void vm_phys_free_page(vm_page_t *page) {
    if (!page) return;
    uint32_t flags = intr_disable();
    spinlock_acquire(&vm_phys_lock);

    // Check if double free
    if (page->flags & PG_FREE) {
        spinlock_release(&vm_phys_lock);
        intr_restore(flags);
        return;
    }

    // Reference count: only free if reaches 0
    if (page->ref_count > 1) {
        page->ref_count--;
    } else {
        if (!(page->flags & PG_PMM_ALLOC)) {
            kprintf("VM: buddy FREE of unallocated pfn=%lu pa=0x%08lx "
                    "flags=0x%04x\n",
                    (unsigned long)(page->phys_addr / PMM_BLOCK_SIZE),
                    (unsigned long)page->phys_addr, page->flags);
            panic("vm_phys: free of unallocated page");
        }
        page->flags &= ~PG_PMM_ALLOC;
        page->ref_count = 0;
        vm_phys_free_locked(page, 0);
    }

    spinlock_release(&vm_phys_lock);
    intr_restore(flags);
}

vm_page_t *vm_phys_alloc_contiguous(size_t count) {
    if (count == 0) return NULL;

    int order = 0;
    while ((1UL << order) < count) order++;

    uint32_t flags = intr_disable();
    spinlock_acquire(&vm_phys_lock);

    vm_page_t *page = vm_phys_alloc_locked(order);
    size_t free_left = vm_phys_free_count + vm_phys_hi_free_count;

    spinlock_release(&vm_phys_lock);
    intr_restore(flags);

    if (page && free_left < vm_phys_low_watermark) {
        vm_page_wakeup_daemon();
    }

    return page;
}

vm_page_t *vm_phys_alloc_contiguous_below(size_t count, uintptr_t phys_limit) {
    if (count == 0) return NULL;

    int order = 0;
    while ((1UL << order) < count) order++;

    uint32_t flags = intr_disable();
    spinlock_acquire(&vm_phys_lock);

    vm_page_t *page = vm_phys_alloc_locked_below(order, phys_limit);
    size_t free_left = vm_phys_free_count;

    spinlock_release(&vm_phys_lock);
    intr_restore(flags);

    if (page && free_left < vm_phys_low_watermark) {
        vm_page_wakeup_daemon();
    }

    return page;
}

void vm_phys_free_contiguous(vm_page_t *page, size_t count) {
     if (!page || count == 0) return;
     int order = 0;
     while ((1UL << order) < count) order++;

     uint32_t flags = intr_disable();
     spinlock_acquire(&vm_phys_lock);

     if (!(page->flags & PG_FREE)) {
         size_t n = (size_t)1U << order;
         for (size_t i = 0; i < n; i++) {
             if (!(page[i].flags & PG_PMM_ALLOC)) {
                 kprintf("VM: buddy FREE-CONTIG unallocated pfn=%lu "
                         "pa=0x%08lx (block pa=0x%08lx order=%d)\n",
                         (unsigned long)(page[i].phys_addr / PMM_BLOCK_SIZE),
                         (unsigned long)page[i].phys_addr,
                         (unsigned long)page->phys_addr, order);
                 panic("vm_phys: free of unallocated page");
             }
             page[i].flags &= ~PG_PMM_ALLOC;
         }
         vm_phys_free_locked(page, order);
     }

     spinlock_release(&vm_phys_lock);
     intr_restore(flags);
}

size_t vm_phys_get_free(void) {
    uint32_t flags = intr_disable();
    spinlock_acquire(&vm_phys_lock);
    size_t free_count = vm_phys_free_count + vm_phys_hi_free_count;
    spinlock_release(&vm_phys_lock);
    intr_restore(flags);
    return free_count;
}

size_t vm_phys_get_used(void) {
    uint32_t flags = intr_disable();
    spinlock_acquire(&vm_phys_lock);
    size_t used = (vm_phys_page_count - vm_phys_free_count) +
                  (vm_phys_hi_count - vm_phys_hi_free_count);
    spinlock_release(&vm_phys_lock);
    intr_restore(flags);
    return used;
}

/* The low zone alone: what kernel and driver allocations can draw on. */
size_t vm_phys_get_low_free(void) {
    uint32_t flags = intr_disable();
    spinlock_acquire(&vm_phys_lock);
    size_t free_count = vm_phys_free_count;
    spinlock_release(&vm_phys_lock);
    intr_restore(flags);
    return free_count;
}

size_t vm_phys_get_low_total(void) {
    return vm_phys_page_count;
}

/*
 * Give the allocator a second run of physical memory, [base, base +
 * page_count pages), described by `pages`.  Called once, single-threaded,
 * before any page of it is added with vm_phys_add_range().  `base` must be
 * aligned to the largest buddy block and lie beyond the low page array.
 */
void vm_phys_add_high_segment(vm_page_t *pages, uintptr_t base,
                              size_t page_count) {
    uintptr_t max_block = ((uintptr_t)1 << (PMM_MAX_ORDER - 1)) * PMM_BLOCK_SIZE;

    if (!pages || !page_count || vm_phys_hi_array ||
        (base & (max_block - 1)) != 0 ||
        base / PMM_BLOCK_SIZE < vm_phys_page_count) {
        return;
    }

    memset(pages, 0, page_count * sizeof(vm_page_t));
    for (size_t i = 0; i < page_count; i++) {
        pages[i].magic_head = VM_PAGE_MAGIC;
        pages[i].magic_tail = VM_PAGE_MAGIC;
        pages[i].phys_addr = base + i * PMM_BLOCK_SIZE;
    }

    uint32_t flags = intr_disable();
    spinlock_acquire(&vm_phys_lock);
    memset(vm_phys_hi_free_lists, 0, sizeof(vm_phys_hi_free_lists));
    vm_phys_hi_free_count = 0;
    vm_phys_hi_base = base;
    vm_phys_hi_count = page_count;
    vm_phys_hi_array = pages;
    spinlock_release(&vm_phys_lock);
    intr_restore(flags);
}

size_t vm_phys_get_order_free_count(int order) {
    size_t count = 0;

    if (order < 0 || order >= PMM_MAX_ORDER) {
        return 0;
    }

    uint32_t flags = intr_disable();
    spinlock_acquire(&vm_phys_lock);

    for (vm_page_t *page = vm_phys_free_lists[order]; page; page = page->next) {
        count++;
    }

    spinlock_release(&vm_phys_lock);
    intr_restore(flags);
    return count;
}

uintptr_t vm_phys_get_order_head_phys(int order) {
    uintptr_t phys = 0;

    if (order < 0 || order >= PMM_MAX_ORDER) {
        return 0;
    }

    uint32_t flags = intr_disable();
    spinlock_acquire(&vm_phys_lock);

    if (vm_phys_free_lists[order]) {
        phys = vm_phys_free_lists[order]->phys_addr;
    }

    spinlock_release(&vm_phys_lock);
    intr_restore(flags);
    return phys;
}

void vm_phys_mark_used(uintptr_t pa) {
    uintptr_t target_pa = pa & ~(PMM_BLOCK_SIZE - 1);
    vm_page_t *target = vm_phys_paddr_to_page(target_pa);
    if (!target) return;
    
    uint32_t flags = intr_disable();
    spinlock_acquire(&vm_phys_lock);

    /*
     * Find the free buddy block containing target_pa, split down to order 0,
     * and reserve exactly one page.
     */
    int order = -1;
    vm_page_t *head = vm_phys_find_free_block_head(target_pa, &order);
    if (!head) {
        spinlock_release(&vm_phys_lock);
        intr_restore(flags);
        return;
    }

    vm_phys_buddy_dequeue(order, head);

    while (order > 0) {
        order--;
        uintptr_t half_size = ((uintptr_t)1 << order) * PMM_BLOCK_SIZE;
        vm_page_t *right = vm_phys_paddr_to_page(head->phys_addr + half_size);
        if (!right) break;

        if (target_pa < right->phys_addr) {
            vm_phys_buddy_enqueue(order, right);
        } else {
            vm_phys_buddy_enqueue(order, head);
            head = right;
        }
    }

    head->flags &= ~PG_FREE;
    /*
     * Mark the reserved page allocated.  Every normally-allocated page
     * gets PG_PMM_ALLOC in vm_phys_prepare_allocated_block; vm_phys_free_page
     * panics ('free of unallocated page') when it is absent.  Without this, a
     * page reserved here (kernel image, watermark, boot regions) that is ever
     * legitimately released would trip a false-positive double-free panic.
     */
    head->flags |= PG_PMM_ALLOC;
    head->order = 0;
    head->ref_count = 1;
    if (vm_phys_free_count > 0) vm_phys_free_count--;
    
    spinlock_release(&vm_phys_lock);
    intr_restore(flags);
}

int vm_phys_check_integrity(void) {
    int ok = 1;
    size_t accounted_free = 0;

    uint32_t flags = intr_disable();
    spinlock_acquire(&vm_phys_lock);

    for (int order = 0; order < PMM_MAX_ORDER && ok; order++) {
        vm_page_t *slow = vm_phys_free_lists[order];
        vm_page_t *fast = vm_phys_free_lists[order];

        while (fast && fast->next) {
            slow = slow->next;
            fast = fast->next->next;
            if (slow == fast) {
                ok = 0;
                break;
            }
        }

        for (vm_page_t *page = vm_phys_free_lists[order]; page; page = page->next) {
            uintptr_t block_size = ((uintptr_t)1 << order) * PMM_BLOCK_SIZE;

            if (!vm_page_valid(page)) {
                ok = 0;
                break;
            }
            if (!(page->flags & PG_FREE) || page->order != order) {
                ok = 0;
                break;
            }
            if ((page->phys_addr & (block_size - 1)) != 0) {
                ok = 0;
                break;
            }
            if (page->prev && page->prev->next != page) {
                ok = 0;
                break;
            }
            if (page->next && page->next->prev != page) {
                ok = 0;
                break;
            }

            accounted_free += ((size_t)1U << order);
        }
    }

    if (ok && accounted_free != vm_phys_free_count) {
        ok = 0;
    }

    spinlock_release(&vm_phys_lock);
    intr_restore(flags);
    return ok;
}
