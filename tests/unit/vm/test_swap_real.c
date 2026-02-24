#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <vfs/vfs.h>
#include <vm/vm_pager.h>
#include <vm/vm_object.h>
#include <vm/vm_swap.h>
#include <sys/lock.h>

// Mock FS Node
static uint8_t swap_file_buffer[1024 * 4096]; // 4MB swap file for testing

static size_t mock_read(fs_node_t *node, off_t offset, size_t size, uint8_t *buffer) {
    (void)node;
    if ((size_t)offset + size > sizeof(swap_file_buffer)) return 0;
    memcpy(buffer, swap_file_buffer + offset, size);
    return size;
}

static size_t mock_write(fs_node_t *node, off_t offset, size_t size, const uint8_t *buffer) {
    (void)node;
    if ((size_t)offset + size > sizeof(swap_file_buffer)) return 0;
    memcpy(swap_file_buffer + offset, buffer, size);
    return size;
}

static fs_node_t mock_swap_node = {
    .name = "swapfile",
    .flags = FS_FILE,
    .length = sizeof(swap_file_buffer),
    .read = mock_read,
    .write = mock_write,
    .uid = 0,
    .gid = 0,
    .mask = 0600,
    .inode = 0,
    .impl = 0,
    .open = 0,
    .close = 0,
    .readdir = 0,
    .finddir = 0,
    .ptr = 0
};

extern struct fs_node *swap_node; // From sys/vm/vm_swap.c

bool test_vm_swap_real_io(void) {
    // Reset state
    swap_node = NULL;
    memset(swap_file_buffer, 0, sizeof(swap_file_buffer));

    // Ensure mock node length is reset if modified by other tests
    mock_swap_node.length = sizeof(swap_file_buffer);

    // Enable swap
    if (vm_swapon(&mock_swap_node) != 0) return false;

    // Create pager
    // Size 0x10000 = 64KB = 16 pages
    vm_pager_t *pager = vm_pager_allocate(VM_OBJ_TYPE_SWAP, NULL, 0x10000, 0, 0);
    if (!pager) return false;

    // Test Write
    vm_page_t page = {0};
    page.pindex = 0;

    uint8_t *page_data = (uint8_t *)malloc(4096);
    if (!page_data) return false;
    memset(page_data, 0xAA, 4096);

    // Adjust phys_addr so P2V(phys_addr) == page_data
    // P2V adds 0xC0000000
    page.phys_addr = (uintptr_t)page_data - 0xC0000000;

    vm_page_t *page_ptr = &page;
    if (vm_pager_put_pages(pager, &page_ptr, 1, true) != 0) {
        free(page_data);
        return false;
    }

    // Verify data in swap_file_buffer
    // We expect the first allocated block to be 0
    if (memcmp(swap_file_buffer, page_data, 4096) != 0) {
        free(page_data);
        return false;
    }

    // Test Read
    memset(page_data, 0, 4096); // Clear buffer
    if (vm_pager_get_pages(pager, &page_ptr, 1, true) != 0) {
        free(page_data);
        return false;
    }

    // Verify data read back
    for (int i=0; i<4096; i++) {
        if (page_data[i] != 0xAA) {
            free(page_data);
            return false;
        }
    }

    // Verify stats
    uint64_t total, free_pg;
    vm_swap_get_stats(&total, &free_pg);
    // total should be 1024 (max pages)
    // free should be 1023 (one used)
    if (total != 1024) {
        free(page_data);
        return false;
    }
    if (free_pg != 1023) {
        free(page_data);
        return false;
    }

    vm_pager_deallocate(pager);
    free(page_data);
    return true;
}

bool test_vm_swap_real_full(void) {
    // Reset state
    swap_node = NULL;
    memset(swap_file_buffer, 0, sizeof(swap_file_buffer));

    // Set small size for testing full condition (4 pages)
    mock_swap_node.length = 4 * 4096;

    if (vm_swapon(&mock_swap_node) != 0) return false;

    vm_pager_t *pager = vm_pager_allocate(VM_OBJ_TYPE_SWAP, NULL, 0x10000, 0, 0);
    if (!pager) return false;

    uint8_t *page_data = (uint8_t *)malloc(4096);
    if (!page_data) return false;

    vm_page_t page = {0};
    page.phys_addr = (uintptr_t)page_data - 0xC0000000;
    vm_page_t *page_ptr = &page;

    // Fill 4 pages
    for (int i=0; i<4; i++) {
        page.pindex = i;
        if (vm_pager_put_pages(pager, &page_ptr, 1, true) != 0) {
            free(page_data);
            return false;
        }
    }

    // 5th page should fail
    page.pindex = 4;
    if (vm_pager_put_pages(pager, &page_ptr, 1, true) == 0) {
         free(page_data);
         return false;
    }

    vm_pager_deallocate(pager);
    free(page_data);
    return true;
}
