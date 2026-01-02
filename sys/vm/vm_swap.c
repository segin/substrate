#include "vm_page.h"
#include <stddef.h>

// Simple Swap Metadata
#define MAX_SWAP_PAGES 1024
static uint32_t swap_map[MAX_SWAP_PAGES / 32]; // Bitmap

static int alloc_swap_block(void) {
    for (int i = 0; i < MAX_SWAP_PAGES; i++) {
        if (!(swap_map[i/32] & (1 << (i%32)))) {
            swap_map[i/32] |= (1 << (i%32));
            return i;
        }
    }
    return -1;
}

static void free_swap_block(int block) {
    swap_map[block/32] &= ~(1 << (block%32));
}

int swap_out(vm_page_t *m) {
    if (!m) return -1;

    int block = alloc_swap_block();
    if (block < 0) return -1; // Swap full

    // TODO: Write page to disk (block * 4096)
    // block_device_write(swap_dev, block * 8, 8, m->phys_addr);

    m->flags |= PG_SWAPPED;
    m->flags &= ~PG_VALID;
    
    // Store block index in the page structure (we'll reuse phys_addr or add a field)
    m->phys_addr = (uintptr_t)block; 

    return 0;
}

int swap_in(vm_page_t *m) {
    if (!(m->flags & PG_SWAPPED)) return -1;

    int block = (int)m->phys_addr;
    
    // 1. Allocate a new physical frame
    // void *new_phys = pmm_alloc_block();
    // if (!new_phys) return -1;

    // 2. Read from disk
    // block_device_read(swap_dev, block * 8, 8, new_phys);

    m->flags &= ~PG_SWAPPED;
    m->flags |= PG_VALID;
    // m->phys_addr = (uintptr_t)new_phys;

    free_swap_block(block);
    return 0;
}
