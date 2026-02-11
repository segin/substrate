#include <sys/types.h>
#include <vfs/vfs.h>
#include <arch/i386/pmm.h>
#include <arch/i386/pmap.h>
#include <kern/console.h>
#include <string.h>

static void *test_page_virt = NULL;
static uintptr_t test_page_phys = 0;

static void itoa_hex(uintptr_t val, char *buf) {
    const char *digits = "0123456789ABCDEF";
    int i;
    for (i = 7; i >= 0; i--) {
        buf[7-i] = digits[(val >> (i*4)) & 0xF];
    }
    buf[8] = 0;
}

static size_t mem_test_read(fs_node_t *node, off_t offset, size_t size, uint8_t *buffer) {
    (void)node;
    char buf[128];
    char hex[9];

    // PHYS_ADDR=0x........\nSIZE=4096\n
    strcpy(buf, "PHYS_ADDR=0x");
    itoa_hex(test_page_phys, hex);

    // Manual strcat
    size_t l = strlen(buf);
    strcpy(buf + l, hex);

    l = strlen(buf);
    strcpy(buf + l, "\nSIZE=4096\n");

    size_t len = strlen(buf);

    if (offset >= len) return 0;
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
    strcpy((char*)test_page_virt, "MEM_TEST_PATTERN");

    // Get Physical Address
    // test_page_virt is kernel direct mapped address (0xC0000000 + PA)
    test_page_phys = (uintptr_t)test_page_virt - 0xC0000000;

    // Register device
    memset(&mem_test_node, 0, sizeof(fs_node_t));
    strcpy(mem_test_node.name, "mem_test");
    mem_test_node.flags = FS_CHARDEVICE;
    mem_test_node.read = &mem_test_read;

    devfs_register_device(&mem_test_node);
}
