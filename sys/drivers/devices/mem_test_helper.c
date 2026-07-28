#include <stdio.h>
#include <string.h>

#include <arch/i386/pmap.h>
#include <arch/i386/pmm.h>
#include <kern/console.h>
#include <sys/types.h>
#include <vfs/vfs.h>

static void *test_page_virt = NULL;
static uintptr_t test_page_phys = 0;

static size_t mem_test_read(fs_node_t *node, off_t offset, size_t size, uint8_t *buffer) {
    (void)node;
    char buf[128];

    // PHYS_ADDR=0x........\nSIZE=4096\n
    int ret = snprintf(buf, sizeof(buf), "PHYS_ADDR=0x%08X\nSIZE=4096\n", (unsigned int)test_page_phys);
    if (ret < 0) return 0;

    size_t len = (size_t)ret;
    if (len >= sizeof(buf)) len = sizeof(buf) - 1;

    if (offset >= (off_t)len) return 0;
    if (size > len - offset) size = len - offset;

    memcpy(buffer, buf + offset, size);
    return size;
}

static fs_node_t mem_test_node;

void mem_test_init(void) {
    // Allocate a page
    test_page_virt = pmm_alloc_block();
    if (!test_page_virt) {
        kprint("mem_test: Failed to allocate page\n");
        return;
    }

    // Fill with pattern
    memset(test_page_virt, 0xAA, 4096);
    // Write a recognizable string at start
    strlcpy((char*)test_page_virt, "MEM_TEST_PATTERN", 4096);

    // Get Physical Address
    // test_page_virt is kernel direct mapped address (0xC0000000 + PA)
    test_page_phys = (uintptr_t)test_page_virt - 0xC0000000;

    // Register device
    memset(&mem_test_node, 0, sizeof(fs_node_t));
    strlcpy(mem_test_node.name, "mem_test", sizeof(mem_test_node.name));
    mem_test_node.flags = FS_CHARDEVICE;
    mem_test_node.read = &mem_test_read;

    devfs_register_device(&mem_test_node);
}
