#include <vm/phys_mem.h>
#include <sys/lock.h>
#include <stddef.h>
#include <string.h>

#include <intr.h>
#include <kern/console.h>

// Generic PMM Data Structures
#define PMM_MAX_ORDER 11
#define PMM_BLOCK_SIZE 4096

static vm_page_t *vm_phys_free_lists[PMM_MAX_ORDER];
static size_t vm_phys_free_count;

static spinlock_t vm_phys_lock;
static vm_page_t *vm_phys_page_array;
static size_t vm_phys_page_count;

static size_t vm_phys_low_watermark = 128; // 512 KB target

vm_page_t *vm_phys_paddr_to_page(uintptr_t pa);

// Internal Helpers
static void vm_phys_buddy_enqueue(int order, vm_page_t *page) {
    page->next = vm_phys_free_lists[order];
    page->prev = NULL;
    if (vm_phys_free_lists[order]) {
        vm_phys_free_lists[order]->prev = page;
    }
    vm_phys_free_lists[order] = page;
    page->order = order;
    page->flags |= PG_FREE;
}

static void vm_phys_buddy_dequeue(int order, vm_page_t *page) {
    if (page->prev) {
        page->prev->next = page->next;
    } else {
        vm_phys_free_lists[order] = page->next;
    }
    if (page->next) {
        page->next->prev = page->prev;
    }
    page->next = NULL;
    page->prev = NULL;
    page->flags &= ~PG_FREE;
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
    size_t idx = pa / PMM_BLOCK_SIZE;
    if (idx < vm_phys_page_count) {
        return &vm_phys_page_array[idx];
    }
    return NULL;
}

static vm_page_t* vm_phys_alloc_locked(int order) {
    if (order >= PMM_MAX_ORDER) return NULL;

    for (int i = order; i < PMM_MAX_ORDER; i++) {
        if (vm_phys_free_lists[i]) {
            vm_page_t *page = vm_phys_free_lists[i];
            vm_phys_buddy_dequeue(i, page);

            while (i > order) {
                i--;
                uintptr_t buddy_pa = page->phys_addr + ((1 << i) * PMM_BLOCK_SIZE);
                vm_page_t *buddy = vm_phys_paddr_to_page(buddy_pa);
                if (buddy) {
                    vm_phys_buddy_enqueue(i, buddy);
                }
            }
            
            vm_phys_free_count -= (1 << order);
            page->ref_count = 1; // Default for new allocation
            return page;
        }
    }
    return NULL;
}

static void vm_phys_free_locked(vm_page_t *page, int order) {
    if (!page || order >= PMM_MAX_ORDER) return;

    vm_phys_free_count += (1 << order);

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
             vm_phys_free_count += (1 << order);
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
    size_t free_left = vm_phys_free_count;
    
    spinlock_release(&vm_phys_lock);
    intr_restore(flags);
    
    if (page && free_left < vm_phys_low_watermark) {
        kprint("PMM: Low memory watermark reached. Waking daemon.\n");
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
    if (page) {
        // All pages in the contiguous block get ref_count 1
        for (size_t i = 0; i < (1UL << order); i++) {
            vm_page_t *p = &page[i];
            p->ref_count = 1;
        }
    }
    size_t free_left = vm_phys_free_count;
    
    spinlock_release(&vm_phys_lock);
    intr_restore(flags);
    
    if (page && free_left < vm_phys_low_watermark) {
        kprint("PMM: Low memory watermark reached. Waking daemon.\n");
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
         vm_phys_free_locked(page, order);
     }
     
     spinlock_release(&vm_phys_lock);
     intr_restore(flags);
}

size_t vm_phys_get_free(void) {
    uint32_t flags = intr_disable();
    spinlock_acquire(&vm_phys_lock);
    size_t free_count = vm_phys_free_count;
    spinlock_release(&vm_phys_lock);
    intr_restore(flags);
    return free_count;
}

size_t vm_phys_get_used(void) {
    uint32_t flags = intr_disable();
    spinlock_acquire(&vm_phys_lock);
    size_t used = vm_phys_page_count - vm_phys_free_count;
    spinlock_release(&vm_phys_lock);
    intr_restore(flags);
    return used;
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
