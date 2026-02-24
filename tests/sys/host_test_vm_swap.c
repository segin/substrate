#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <assert.h>
#include <stddef.h>

// Include kernel headers for types and declarations
#include <sys/types.h>
#include <sys/lock.h>
#include <vfs/vfs.h>
#include <vm/vm_page.h>
#include <vm/vm_pager.h>
#include <vm/vm_kmem.h>

// Mock implementations

// 1. Lock mocks
void spinlock_acquire(spinlock_t *lock) {
    (void)lock;
    // Single threaded test, no-op
}

void spinlock_release(spinlock_t *lock) {
    (void)lock;
}

// 2. Kmem mocks
void *kmalloc(size_t size) {
    // Return calloc to ensure zeroed if expected, but kmalloc implies garbage.
    // However, vm_swap.c allocates pager structure and fills it.
    // Safe to use malloc.
    void *p = malloc(size);
    // Fill with pattern to detect uninitialized use?
    // memset(p, 0xAA, size);
    return p;
}

void kfree(void *ptr, size_t size) {
    (void)size;
    free(ptr);
}

// 3. VFS mocks
static uint8_t *mock_disk_buffer = NULL;
static size_t mock_disk_size = 0;

size_t read_fs(fs_node_t *node, off_t offset, size_t size, uint8_t *buffer) {
    (void)node;
    if (offset + size > mock_disk_size) {
        if (offset >= (off_t)mock_disk_size) return 0;
        size_t read_size = mock_disk_size - offset;
        memcpy(buffer, mock_disk_buffer + offset, read_size);
        return read_size;
    }
    memcpy(buffer, mock_disk_buffer + offset, size);
    return size;
}

size_t write_fs(fs_node_t *node, off_t offset, size_t size, const uint8_t *buffer) {
    (void)node;
    if (offset + size > mock_disk_size) {
        return 0; // Error
    }
    memcpy(mock_disk_buffer + offset, buffer, size);
    return size;
}

// Mock kprint
void kprint(const char *s) {
    printf("%s", s);
}

// Now include the file under test
// Note: vm_swap.c includes headers too, but since they are guarded, it's fine.
#include "../../sys/vm/vm_swap.c"

// Helper to reset state
void reset_swap_state() {
    memset(swap_bitmap, 0, sizeof(swap_bitmap));
    swap_num_pages = 0;
    swap_node = NULL;
    // swap_lock is static and initialized
}

// Tests

void test_alloc_free_swap_block() {
    printf("Running test_alloc_free_swap_block...\n");
    reset_swap_state();
    swap_num_pages = 10; // Simulate small swap

    // Alloc 1 block
    int b1 = alloc_swap_block();
    assert(b1 == 0);
    assert((swap_bitmap[0] & 1) != 0);

    // Alloc another
    int b2 = alloc_swap_block();
    assert(b2 == 1);
    assert((swap_bitmap[0] & 2) != 0);

    // Free first
    free_swap_block(b1);
    assert((swap_bitmap[0] & 1) == 0);

    // Alloc again, should get 0
    int b3 = alloc_swap_block();
    assert(b3 == 0);

    // Fill up
    for (int i=0; i<8; i++) alloc_swap_block();

    // Reset and fill properly
    reset_swap_state();
    swap_num_pages = 3;
    assert(alloc_swap_block() == 0);
    assert(alloc_swap_block() == 1);
    assert(alloc_swap_block() == 2);
    assert(alloc_swap_block() == -1); // Full

    free_swap_block(1);
    assert(alloc_swap_block() == 1);

    printf("Passed.\n");
}

void test_vm_swapon() {
    printf("Running test_vm_swapon...\n");
    reset_swap_state();

    fs_node_t node;
    memset(&node, 0, sizeof(node));
    node.flags = FS_FILE;
    node.write = (write_type_t)1; // Just needs to be non-null
    node.length = 4096 * 10; // 10 pages

    int res = vm_swapon(&node);
    assert(res == 0);
    assert(swap_num_pages == 10);
    assert(swap_node == &node);

    // Try enabling again
    res = vm_swapon(&node);
    assert(res == -1);

    // Try invalid node
    reset_swap_state();
    node.flags = FS_DIRECTORY;
    res = vm_swapon(&node);
    assert(res == -1);

    printf("Passed.\n");
}

void test_swap_pager() {
    printf("Running test_swap_pager...\n");
    reset_swap_state();

    // Setup swap file
    fs_node_t node;
    memset(&node, 0, sizeof(node));
    node.flags = FS_FILE;
    node.write = (write_type_t)1;
    node.length = 4096 * 100;
    mock_disk_size = node.length;
    mock_disk_buffer = malloc(mock_disk_size);
    memset(mock_disk_buffer, 0, mock_disk_size);

    vm_swapon(&node);

    // Create pager
    vm_pager_t *pager = swap_pager_alloc(NULL, 4096 * 5, 0, 0); // 5 pages
    assert(pager != NULL);
    swap_pager_t *sp = (swap_pager_t *)pager;
    assert(sp->max_pages == 5);

    // Create a dummy page
    vm_page_t page;
    memset(&page, 0, sizeof(page));
    page.pindex = 0;

    // Data buffer for page
    uint8_t *page_data = malloc(4096);
    // Setup phys_addr so P2V works
    // P2V(phys) = phys + 0xC0000000. We want result to be page_data.
    // So phys = page_data - 0xC0000000.
    page.phys_addr = (uintptr_t)page_data - 0xC0000000;

    // Test Putpage (write)
    // Write 0xAA pattern
    memset(page_data, 0xAA, 4096);

    int res = swap_pager_putpage(pager, &page, true);
    assert(res == 0);

    // Verify block allocated in pager
    assert(sp->swp_blocks[0] != SWAP_BLOCK_NONE);
    uint32_t block = sp->swp_blocks[0];

    // Verify data in mock disk
    assert(memcmp(mock_disk_buffer + (block * 4096), page_data, 4096) == 0);

    // Test Getpage (read)
    // Clear page buffer
    memset(page_data, 0, 4096);

    res = swap_pager_getpage(pager, &page, true);
    assert(res == 0);

    // Verify data read back
    for (int i=0; i<4096; i++) {
        if (page_data[i] != 0xAA) {
            printf("Mismatch at %d: expected 0xAA, got 0x%02X\n", i, page_data[i]);
            assert(0);
        }
    }

    // Test Haspage
    assert(swap_pager_haspage(pager, 0) == true);
    assert(swap_pager_haspage(pager, 1) == false);

    // Cleanup
    swap_pager_dealloc(pager);
    free(page_data);
    free(mock_disk_buffer);

    printf("Passed.\n");
}

void test_stats() {
    printf("Running test_stats...\n");
    reset_swap_state();
    swap_num_pages = 10;

    uint64_t total = 0;
    uint64_t free_p = 0;
    vm_swap_get_stats(&total, &free_p);
    assert(total == 10);
    assert(free_p == 10);

    alloc_swap_block();
    vm_swap_get_stats(&total, &free_p);
    assert(total == 10);
    assert(free_p == 9);

    printf("Passed.\n");
}

int main() {
    test_alloc_free_swap_block();
    test_vm_swapon();
    test_swap_pager();
    test_stats();
    printf("All tests passed!\n");
    return 0;
}
