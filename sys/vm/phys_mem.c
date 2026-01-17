#include "phys_mem.h"
#include <sys/lock.h>
#include <stddef.h>
#include <string.h>

#include <intr.h>

// Generic PMM Data Structures
#define PMM_MAX_ORDER 11
#define PMM_BLOCK_SIZE 4096

static vm_page_t *vm_phys_free_lists[PMM_MAX_ORDER];
static size_t vm_phys_free_count;

static spinlock_t vm_phys_lock;
static vm_page_t *vm_phys_page_array;
static size_t vm_phys_page_count;

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
    
    spinlock_release(&vm_phys_lock);
    intr_restore(flags);
    return page;
}

void vm_phys_free_page(vm_page_t *page) {
    if (!page) return;
    uint32_t flags = intr_disable();
    spinlock_acquire(&vm_phys_lock);
    
    // Check if double free
    if (!(page->flags & PG_FREE)) {
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
    
    spinlock_release(&vm_phys_lock);
    intr_restore(flags);
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
    return vm_phys_free_count;
}

size_t vm_phys_get_used(void) {
    /* Derive from total - free (no bitmap needed) */
    return vm_phys_page_count - vm_phys_free_count;
}

void vm_phys_mark_used(uintptr_t pa) {
    /* Mark page as used by removing from buddy if free */
    vm_page_t *page = vm_phys_paddr_to_page(pa);
    if (!page) return;
    
    uint32_t flags = intr_disable();
    spinlock_acquire(&vm_phys_lock);
    
    if (page->flags & PG_FREE) {
        /* Page is in buddy - this is a reservation during boot */
        /* For now, just mark the flag - proper removal would need buddy surgery */
        page->flags &= ~PG_FREE;
        if (vm_phys_free_count > 0) vm_phys_free_count--;
    }
    
    spinlock_release(&vm_phys_lock);
    intr_restore(flags);
}
