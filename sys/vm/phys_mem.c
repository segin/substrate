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
static uint8_t *vm_phys_bitmap;
static size_t vm_phys_bitmap_size;
static vm_page_t *vm_phys_page_array;
static size_t vm_phys_page_count;

// Diagnostics
static size_t vm_phys_used_blocks;

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

static void vm_phys_mark_used_bit(uintptr_t pa) {
    uint32_t block = pa / PMM_BLOCK_SIZE;
    uint32_t idx = block / 8;
    uint32_t bit = block % 8;
    if (vm_phys_bitmap && idx < vm_phys_bitmap_size) {
        if (!(vm_phys_bitmap[idx] & (1 << bit))) {
            vm_phys_bitmap[idx] |= (1 << bit);
            vm_phys_used_blocks++;
        }
    }
}

static void vm_phys_mark_free_bit(uintptr_t pa) {
    uint32_t block = pa / PMM_BLOCK_SIZE;
    uint32_t idx = block / 8;
    uint32_t bit = block % 8;
    if (vm_phys_bitmap && idx < vm_phys_bitmap_size) {
        if (vm_phys_bitmap[idx] & (1 << bit)) {
            vm_phys_bitmap[idx] &= ~(1 << bit);
            vm_phys_used_blocks--;
        }
    }
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
    spinlock_init(&vm_phys_lock, "vm_phys");
    vm_phys_bitmap = bitmap;
    vm_phys_bitmap_size = bitmap_size;
    vm_phys_page_array = pages;
    vm_phys_page_count = page_count;
    
    // Mark all used
    if (bitmap) {
        memset(bitmap, 0xFF, bitmap_size);
    }
    vm_phys_used_blocks = page_count;
    
    // Init page array
    if (pages) {
        memset(pages, 0, page_count * sizeof(vm_page_t));
        for (size_t i = 0; i < page_count; i++) {
            pages[i].phys_addr = i * PMM_BLOCK_SIZE;
            pages[i].flags = PG_FREE; // Temporarily FREE flag but not in list
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
             // Clear bitmap bits
             for (size_t i = 0; i < (1U << order); i++) {
                 vm_phys_mark_free_bit(addr + i * PMM_BLOCK_SIZE);
             }
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
    if (page) {
        vm_phys_mark_used_bit(page->phys_addr);
    }
    
    spinlock_release(&vm_phys_lock);
    intr_restore(flags);
    return page;
}

void vm_phys_free_page(vm_page_t *page) {
    if (!page) return;
    uint32_t flags = intr_disable();
    spinlock_acquire(&vm_phys_lock);
    
    // Check if double free or not used?
    if (!(page->flags & PG_FREE)) {
        vm_phys_mark_free_bit(page->phys_addr);
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
         for (size_t i = 0; i < (1UL << order); i++) {
             vm_phys_mark_used_bit(page->phys_addr + i * PMM_BLOCK_SIZE);
         }
    }
    
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
     
     if (page) {
         for (size_t i = 0; i < (1UL << order); i++) {
             // Logic to mark free in bitmap is tricky if we do it bit by bit, 
             // but we just need to ensure consistency.
             vm_phys_mark_free_bit(page->phys_addr + i * PMM_BLOCK_SIZE);
         }
         vm_phys_free_locked(page, order);
     }
     
     spinlock_release(&vm_phys_lock);
     intr_restore(flags);
}

size_t vm_phys_get_free(void) {
    return vm_phys_free_count; // Atomic read usually fine
}

size_t vm_phys_get_used(void) {
    return vm_phys_used_blocks;
}

void vm_phys_mark_used(uintptr_t pa) {
    uint32_t flags = intr_disable();
    spinlock_acquire(&vm_phys_lock);
    vm_phys_mark_used_bit(pa);
    spinlock_release(&vm_phys_lock);
    intr_restore(flags);
}
