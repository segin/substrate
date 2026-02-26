/*
 * Swap Pager Implementation
 */

#include <vm/vm_pager.h>
#include <vm/vm_kmem.h>
#include <vfs/vfs.h>
#include <sys/lock.h>
#include <vm/vm_swap.h>
#include <stddef.h>
#include <string.h>

// Simple bitmap-based swap allocator
#define MAX_SWAP_PAGES 1024
#define SWAP_BLOCK_NONE 0xFFFFFFFF

static uint32_t swap_bitmap[MAX_SWAP_PAGES / 32];
static uint32_t swap_num_pages = 0;
// Real implementation would have a list of swap devices/files

// Global lock protecting swap_bitmap, swap_num_pages, and swap_node
static spinlock_t swap_lock = SPINLOCK_INIT("swap_lock");

typedef struct swap_pager {
    vm_pager_t base;
    uint32_t *swp_blocks; // Array mapping pindex -> swap_block
    uint32_t max_pages;
} swap_pager_t;

static int alloc_swap_block(void) {
    spinlock_acquire(&swap_lock);
    for (uint32_t i = 0; i < swap_num_pages; i++) {
        if (!(swap_bitmap[i/32] & (1 << (i%32)))) {
            swap_bitmap[i/32] |= (1 << (i%32));
            spinlock_release(&swap_lock);
            return i;
        }
    }
    spinlock_release(&swap_lock);
    return -1;
}

static void free_swap_block(int block) {
    spinlock_acquire(&swap_lock);
    if (block >= 0 && (uint32_t)block < swap_num_pages)
        swap_bitmap[block/32] &= ~(1 << (block%32));
    spinlock_release(&swap_lock);
}

static struct vm_pager *swap_pager_alloc(void *handle, size_t size, uint8_t prot, uint64_t offset) {
    (void)handle;
    (void)prot;
    (void)offset;

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
        pager->swp_blocks[i] = SWAP_BLOCK_NONE; // Metadata only, block allocated on putpage
    }

    return (vm_pager_t *)pager;
}

static void swap_pager_dealloc(struct vm_pager *p) {
    swap_pager_t *pager = (swap_pager_t *)p;
    for (uint32_t i = 0; i < pager->max_pages; i++) {
        if (pager->swp_blocks[i] != SWAP_BLOCK_NONE) {
            free_swap_block((int)pager->swp_blocks[i]);
        }
    }
    kfree(pager->swp_blocks, pager->max_pages * sizeof(uint32_t));
    kfree(pager, sizeof(swap_pager_t));
}

// Global swap state
struct fs_node *swap_node = NULL;

#define P2V(x) ((uintptr_t)(x) + 0xC0000000)

static int swap_pager_getpage(struct vm_pager *p, vm_page_t *m, bool sync) {
    (void)sync;
    swap_pager_t *pager = (swap_pager_t *)p;
    uint64_t pindex = m->pindex;

    if (pindex >= pager->max_pages) return -1;

    // Lock?
    uint32_t block = pager->swp_blocks[pindex];
    if (block == SWAP_BLOCK_NONE) return -1; // Block not on disk (freshly allocated?)

    // Check if swap file is active
    if (!swap_node) return -1;

    // Calculate offset in swap file
    uint64_t offset = (uint64_t)block * 4096;
    uint8_t *buf = (uint8_t *)P2V(m->phys_addr);

    uint32_t bytes = read_fs(swap_node, (int64_t)offset, 4096, buf);

    if (bytes != 4096) {
        // Partial read - corrupted swap? Zero fill rest
        for (uint32_t i = bytes; i < 4096; i++) buf[i] = 0;
        return -1; // Error for now
    }

    return 0;
}

static int swap_pager_putpage(struct vm_pager *p, vm_page_t *m, bool sync) {
    (void)sync;
    swap_pager_t *pager = (swap_pager_t *)p;
    uint64_t pindex = m->pindex;

    if (pindex >= pager->max_pages) return -1;

    uint32_t block = pager->swp_blocks[pindex];
    if (block == SWAP_BLOCK_NONE) {
        int new_block = alloc_swap_block();
        if (new_block == -1) return -1; // Swap full
        pager->swp_blocks[pindex] = (uint32_t)new_block;
        block = (uint32_t)new_block;
    }

    if (!swap_node) return -1;

    // Calculate offset
    uint64_t offset = (uint64_t)block * 4096;
    uint8_t *buf = (uint8_t *)P2V(m->phys_addr);

    uint32_t bytes = write_fs(swap_node, (int64_t)offset, 4096, buf);

    if (bytes != 4096) return -1;

    return 0;
}

void vm_swap_get_stats(uint64_t *total_pages, uint64_t *free_pages) {
    if (!total_pages && !free_pages) return;

    spinlock_acquire(&swap_lock);

    if (total_pages) *total_pages = swap_num_pages;

    if (free_pages) {
        uint32_t used = 0;
        for (uint32_t i = 0; i < swap_num_pages; i++) {
             if (swap_bitmap[i/32] & (1 << (i%32))) {
                 used++;
             }
        }
        *free_pages = swap_num_pages - used;
    }

    spinlock_release(&swap_lock);
}

static bool swap_pager_haspage(struct vm_pager *p, uint64_t pindex) {
    swap_pager_t *pager = (swap_pager_t *)p;
    if (pindex >= pager->max_pages) return false;
    return pager->swp_blocks[pindex] != SWAP_BLOCK_NONE;
}

vm_pager_ops_t swap_pager_ops = {
    .init = NULL,
    .alloc = swap_pager_alloc,
    .dealloc = swap_pager_dealloc,
    .getpage = swap_pager_getpage,
    .putpage = swap_pager_putpage,
    .haspage = swap_pager_haspage
};

// Set the file/node to use for swap
// Returns 0 on success, error code otherwise
int vm_swapon(void *node) {
    if (!node) return -1;

    fs_node_t *fs_node = (fs_node_t *)node;

    spinlock_acquire(&swap_lock);
    if (swap_node) {
        // Swap already active
        spinlock_release(&swap_lock);
        return -1;
    }

    // Validate node is a regular file and writable
    if ((fs_node->flags & 0x07) != FS_FILE) {
        spinlock_release(&swap_lock);
        return -1;
    }

    if (!fs_node->write) {
        spinlock_release(&swap_lock);
        return -1;
    }

    swap_node = fs_node;

    // Calculate swap size from file size
    swap_num_pages = (uint32_t)(fs_node->length / 4096);
    if (swap_num_pages > MAX_SWAP_PAGES) {
        swap_num_pages = MAX_SWAP_PAGES;
    }

    // Initialize bitmap
    memset(swap_bitmap, 0, sizeof(swap_bitmap));

    spinlock_release(&swap_lock);

    extern void kprint(const char *);
    kprint("Swap enabled.\n");

    return 0;
}
