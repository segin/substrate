#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// Define HOST_TEST to enable test-specific logic in mem.c
#define HOST_TEST

// Mock kernel structures/types needed by mem.c
#include <sys/types.h>
#include <sys/errno.h>
#include <sys/lock.h>
#include <vfs/vfs.h>
#include <arch/i386/pmap.h>
#include <vm/vm_map.h>
#include <sys/proc.h>

// Process Mock
process_t mock_process;
process_t *current_process = &mock_process;

// Mocks for devfs
void devfs_register_device(fs_node_t *node) {
    printf("devfs_register_device: %s\n", node->name);
}

// Mocks for mutex
void mutex_init(mutex_t *m, const char *name) {
    (void)m; (void)name;
}
void mutex_lock(mutex_t *m) { (void)m; }
void mutex_unlock(mutex_t *m) { (void)m; }

// Mocks for copyin/copyout
int copyin(const void *src, void *dst, size_t size) {
    memcpy(dst, src, size);
    return 0;
}

int copyout(const void *src, void *dst, size_t size) {
    memcpy(dst, src, size);
    return 0;
}

// Mocks for VM
int vm_map_find_space(struct vm_map *map, uintptr_t *addr, size_t length) { (void)map; (void)addr; (void)length; return 0; }
int vm_map_remove(struct vm_map *map, uintptr_t start, uintptr_t end) { (void)map; (void)start; (void)end; return 0; }
int vm_map_insert(struct vm_map *map, struct vm_object *obj, uint64_t offset, uintptr_t start, uintptr_t end, uint8_t prot, uint8_t max_prot, uint8_t inheritance) { (void)map; (void)obj; (void)offset; (void)start; (void)end; (void)prot; (void)max_prot; (void)inheritance; return 0; }
int pmap_enter(pmap_t pmap, uintptr_t va, uintptr_t pa, uint32_t prot, uint32_t flags) { (void)pmap; (void)va; (void)pa; (void)prot; (void)flags; return 0; }

// Mocks for pmap
// mem.c uses pmap_kenter/pmap_kremove
// In our HOST_TEST, MEM_WINDOW_ADDR is a pointer to a malloc buffer.
// pmap_kenter(MEM_WINDOW_ADDR, pa) is supposed to map pa to that window.
// Since we don't have real page tables, we'll simulate the "mapping"
// by copying data from our "physical memory" to the window buffer.

// Simulated physical memory
#define PHYS_MEM_SIZE (4 * 1024 * 1024 * 1024ULL) // 4GB
// We can't allocate 4GB. We'll use a sparse map or just a small buffer for HighMem tests.
// Let's use a small buffer representing the "HighMem Page" we are testing.
uint8_t highmem_page[4096];
uintptr_t highmem_pa = 0x50000000; // 1.25 GB

// Track if window is mapped
bool window_mapped = false;
uintptr_t window_mapped_pa = 0;

void pmap_kenter(uintptr_t va, uintptr_t pa) {
    // va is MEM_WINDOW_ADDR (which is mem_window_addr_mock)
    extern uintptr_t mem_window_addr_mock;
    if (va != mem_window_addr_mock) {
        printf("pmap_kenter: Unexpected VA %lx (expected %lx)\n", (unsigned long)va, (unsigned long)mem_window_addr_mock);
        exit(1);
    }

    window_mapped = true;
    window_mapped_pa = pa;

    // Simulate mapping: Copy from "physical" to window
    if (pa == highmem_pa) {
        memcpy((void*)mem_window_addr_mock, highmem_page, 4096);
    } else {
        // Zero out for unknown pages
        memset((void*)mem_window_addr_mock, 0, 4096);
    }
}

void pmap_kremove(uintptr_t va) {
    extern uintptr_t mem_window_addr_mock;
    if (va != mem_window_addr_mock) {
        printf("pmap_kremove: Unexpected VA %lx\n", (unsigned long)va);
        exit(1);
    }

    // Simulate unmapping: Copy back from window to "physical" (writeback)
    // mem_write calls copyin(buffer, window, size). copyin writes to window.
    // So window has new data. We must copy it back to highmem_page.

    if (window_mapped && window_mapped_pa == highmem_pa) {
        memcpy(highmem_page, (void*)mem_window_addr_mock, 4096);
    }

    window_mapped = false;
}

// Include the source file under test
#include "../../../sys/drivers/devices/mem.c"

// Helper to assert
#define ASSERT(cond, msg) do { \
    if (!(cond)) { \
        printf("FAIL: %s\n", msg); \
        exit(1); \
    } \
} while(0)

int main() {
    printf("Test: sys/drivers/devices/mem.c\n");

    // Setup mock window
    mem_window_addr_mock = (uintptr_t)malloc(4096);

    // Initialize mem driver
    mem_init();

    // Test 1: HighMem Read
    printf("Test 1: HighMem Read\n");
    // Setup data in highmem
    memset(highmem_page, 0xAA, 4096);
    strcpy((char*)highmem_page, "HIGHMEM_DATA");

    uint8_t buffer[128];
    // Read from highmem_pa
    size_t size = mem_read(&mem_node, highmem_pa, 128, buffer);

    ASSERT(size == 128, "Read size mismatch");
    ASSERT(strcmp((char*)buffer, "HIGHMEM_DATA") == 0, "Read data mismatch");
    ASSERT(buffer[20] == 0xAA, "Read pattern mismatch");

    // Test 2: HighMem Write
    printf("Test 2: HighMem Write\n");
    char *write_data = "NEW_HIGHMEM_CONTENT";
    size = mem_write(&mem_node, highmem_pa, strlen(write_data) + 1, (uint8_t*)write_data);

    ASSERT(size == strlen(write_data) + 1, "Write size mismatch");

    // Verify highmem_page was updated
    ASSERT(strcmp((char*)highmem_page, "NEW_HIGHMEM_CONTENT") == 0, "Write verify failed");

    // Test 3: Crossing Page Boundary (Simulated)
    // Since our mock pmap_kenter only handles one specific page (highmem_pa),
    // we can't easily test crossing to another valid page unless we define more.
    // But we can test the chunking logic by reading a small chunk.

    // Test 4: Direct Map Access (Low Mem)
    // mem.c uses 0xC0000000 + offset.
    // In host test, this will crash if we don't mock copyout for low addresses.
    // copyout in mocks.c is memcpy.
    // memcpy((void*)(0xC0000000 + 0), buffer, size) -> Segfault.

    // So we CANNOT test low mem path easily unless we map 0xC0000000 in the host process.
    // We can use mmap to allocate at 0xC0000000?
    // Or just skip low mem tests and focus on HighMem (which is the task).

    printf("Skipping LowMem tests (requires mapping 0xC0000000)\n");

    printf("All tests passed!\n");
    return 0;
}
