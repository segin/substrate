/*
 * Swap Pager Implementation
 */

#include "vm_pager.h"
#include "vm_kmem.h"
#include <stddef.h>

// Simple bitmap-based swap allocator
#define MAX_SWAP_PAGES 1024
static uint32_t swap_bitmap[MAX_SWAP_PAGES / 32];
// Real implementation would have a list of swap devices/files

typedef struct swap_pager {
    vm_pager_t base;
    uint32_t *swp_blocks; // Array mapping pindex -> swap_block
    uint32_t max_pages;
} swap_pager_t;

static int alloc_swap_block(void) {
    for (int i = 0; i < MAX_SWAP_PAGES; i++) {
        if (!(swap_bitmap[i/32] & (1 << (i%32)))) {
            swap_bitmap[i/32] |= (1 << (i%32));
            return i;
        }
    }
    return -1;
}

static void free_swap_block(int block) {
    if (block >= 0 && block < MAX_SWAP_PAGES)
        swap_bitmap[block/32] &= ~(1 << (block%32));
}

static struct vm_pager *swap_pager_alloc(void *handle, size_t size, uint8_t prot, uint64_t offset) {
    swap_pager_t *pager = kmalloc(sizeof(swap_pager_t));
    if (!pager) return NULL;
    
    pager->base.ops = &swap_pager_ops;
    pager->max_pages = (size + 4095) / 4096;
    pager->swp_blocks = kmalloc(pager->max_pages * sizeof(uint32_t));
    
    if (!pager->swp_blocks) {
        kfree(pager, sizeof(swap_pager_t));
        return NULL;
    }
    
    for (uint32_t i = 0; i < pager->max_pages; i++) {
        pager->swp_blocks[i] = -1; // Metadata only, block allocated on putpage
    }
    
    return (vm_pager_t *)pager;
}

static void swap_pager_dealloc(struct vm_pager *p) {
    swap_pager_t *pager = (swap_pager_t *)p;
    for (uint32_t i = 0; i < pager->max_pages; i++) {
        if (pager->swp_blocks[i] != -1) {
            free_swap_block(pager->swp_blocks[i]);
        }
    }
    kfree(pager->swp_blocks, pager->max_pages * sizeof(uint32_t));
    kfree(pager, sizeof(swap_pager_t));
}

static int swap_pager_getpage(struct vm_pager *p, vm_page_t *m, bool sync) {
    swap_pager_t *pager = (swap_pager_t *)p;
    uint64_t pindex = m->pindex;
    
    if (pindex >= pager->max_pages) return -1;
    int block = pager->swp_blocks[pindex];
    if (block == -1) return -1; // Block not on disk
    
    // TODO: Read from swap device (block * 4096)
    // block_read(swap_dev, block, m->phys_addr);
    
    // For now, simulate zero-fill (data lost without real disk)
    return 0; 
}

static int swap_pager_putpage(struct vm_pager *p, vm_page_t *m, bool sync) {
    swap_pager_t *pager = (swap_pager_t *)p;
    uint64_t pindex = m->pindex;
    
    if (pindex >= pager->max_pages) return -1;
    
    int block = pager->swp_blocks[pindex];
    if (block == -1) {
        block = alloc_swap_block();
        if (block == -1) return -1; // Swap full
        pager->swp_blocks[pindex] = block;
    }
    
    // TODO: Write to swap device
    return 0;
}

static bool swap_pager_haspage(struct vm_pager *p, uint64_t pindex) {
    swap_pager_t *pager = (swap_pager_t *)p;
    if (pindex >= pager->max_pages) return false;
    return pager->swp_blocks[pindex] != -1;
}

vm_pager_ops_t swap_pager_ops = {
    .init = NULL,
    .alloc = swap_pager_alloc,
    .dealloc = swap_pager_dealloc,
    .getpage = swap_pager_getpage,
    .putpage = swap_pager_putpage,
    .haspage = swap_pager_haspage
};
