#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <sys/types.h>

// Mocks will be included via -I
#include <vfs/vfs.h>
#include <vm/vm_kmem.h>

// Mock functions implementation

void vfs_register_filesystem(filesystem_t *fs) {
    (void)fs;
}

// Mock kmalloc/kfree tracking
static int kmalloc_calls = 0;
static size_t last_kmalloc_size = 0;
static int kfree_calls = 0;

void *kmalloc(size_t size) {
    kmalloc_calls++;
    last_kmalloc_size = size;
    return calloc(1, size);
}

void kfree(void *ptr, size_t size) {
    kfree_calls++;
    (void)size;
    free(ptr);
}

// Mock virtio_9p_send
int virtio_9p_send(void *out_buf, uint32_t out_len, void *in_buf, uint32_t in_len) {
    (void)out_buf;
    (void)out_len;
    (void)in_len;

    // Construct a minimal valid RREAD response
    uint8_t *p = (uint8_t*)in_buf;
    // We need to match what p9_vfs_read expects
    // p9_vfs_read expects:
    // rsize_max bytes in in_buf.
    // It parses:
    // uint32_t size (4 bytes)
    // uint8_t type (1 byte)
    // uint16_t tag (2 bytes)
    // uint32_t count (4 bytes)

    uint32_t rsize = 4 + 1 + 2 + 4;

    *(uint32_t*)p = rsize;
    p[4] = 117; // P9_RREAD
    p[5] = 0; p[6] = 0; // Tag
    *(uint32_t*)(p+7) = 0; // Count

    return 0;
}

// Include the source file under test
// This will bring in p9_vfs_read, p9_mount, p9_init
#include "../../sys/fs/9p.c"

int main() {
    printf("Running 9P Stack Overflow Reproduction Test...\n");

    // Reset stats
    kmalloc_calls = 0;
    kfree_calls = 0;

    fs_node_t node;
    memset(&node, 0, sizeof(node));

    uint8_t buffer[100];
    uint32_t size = 50;

    // Call the function
    printf("Invoking p9_vfs_read with size=%u...\n", size);
    p9_vfs_read(&node, 0, size, buffer);

    printf("kmalloc_calls: %d\n", kmalloc_calls);

    if (kmalloc_calls == 0) {
        printf("FAIL: kmalloc was NOT called. VLA likely used.\n");
        return 1; // Return 1 to indicate failure (vulnerability present)
    } else {
        printf("PASS: kmalloc WAS called.\n");

        if (kfree_calls == 0) {
            printf("FAIL: kfree was NOT called. Memory leak!\n");
            return 1;
        }
    }

    return 0;
}
