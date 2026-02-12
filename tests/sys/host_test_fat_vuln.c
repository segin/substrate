#define HOST_TEST

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>

// Include necessary kernel headers
// We rely on -I sys -idirafter sys/include
#include <vfs/vfs.h>

// Mocks
void kprint(const char *str) {
    printf("[KERNEL] %s", str);
}

static size_t kmalloc_size_log = 0;

void *kmalloc(size_t size) {
    kmalloc_size_log = size;
    // We don't actually need to allocate 4GB for the test to pass/fail logic check.
    // If size is huge, return NULL or handle it.
    // But for the bug reproduction, the size will be small (wrapped).
    if (size < 100000) {
        return malloc(size);
    }
    return NULL; // Simulate failure for huge allocs
}

void kfree(void *ptr) {
    free(ptr);
}

void vfs_register_filesystem(filesystem_t *fs) {
    printf("Registered filesystem: %s\n", fs->name);
}

// Global to control mock read behavior
static uint32_t mock_fat_size_32 = 0;
static uint16_t mock_bytes_per_sector = 0;

size_t mock_device_read(fs_node_t *node, off_t offset, size_t size, uint8_t *buffer) {
    // fat_mount reads BPB at offset 0 (sizeof fat_bpb_t)
    // and Extended BPB if needed.

    // Structure sizes (packed) from fat.h need to match exactly.
    // We will include fat.h later via fat.c, so we rely on offsets.

    // For simplicity, we just fill the buffer with zeros first
    memset(buffer, 0, size);

    if (offset == 0 && size >= 90) { // BPB read
        // set bytes_per_sector at offset 11 (uint16_t)
        // set fat_size_16 at offset 22 (uint16_t) -> 0 to force FAT32
        // set total_sectors_32 at offset 32 (uint32_t)

        uint16_t *bps = (uint16_t *)(buffer + 11);
        *bps = mock_bytes_per_sector;

        uint8_t *spc = (uint8_t *)(buffer + 13);
        *spc = 1; // 1 sector per cluster

        uint16_t *fs16 = (uint16_t *)(buffer + 22);
        *fs16 = 0; // Use FAT32

        uint8_t *fat_count = (uint8_t *)(buffer + 16);
        *fat_count = 1;

        uint16_t *reserved = (uint16_t *)(buffer + 14);
        *reserved = 32;

        // Make total sectors large enough to be FAT32
        uint32_t *tot32 = (uint32_t *)(buffer + 32);
        *tot32 = 10000000;

        return size;
    }

    if (offset == 36 && size >= 48) { // Extended BPB read (offset 36?)
        // In fat.c:
        // off_t ext_offset = sizeof(fat_bpb_t);
        // sizeof(fat_bpb_t) is 36 bytes (packed).
        // dev->read(dev, ext_offset, sizeof(fat32_ext_bpb_t), ...);

        // set fat_size_32 at offset 0 of this read
        uint32_t *fs32 = (uint32_t *)buffer;
        *fs32 = mock_fat_size_32;

        return size;
    }

    return size;
}

// Include fat.c source
// We need to define expected macros/includes
#include "../../sys/fs/fat/fat.c"

int main() {
    // Unbuffer stdout to ensure we see output before any potential crash
    setvbuf(stdout, NULL, _IONBF, 0);

    printf("Running FAT Vulnerability Test (Integer Overflow)...\n");

    // Setup mock device
    fs_node_t device_node;
    memset(&device_node, 0, sizeof(device_node));
    device_node.read = mock_device_read;

    // Case 1: Overflow
    // fat_size_32 = 8388609 (0x800001)
    // bytes_per_sector = 512 (0x200)
    // Product = 0x100000200 = 4294967808
    // 32-bit wrap = 0x200 = 512

    mock_fat_size_32 = 8388609;
    mock_bytes_per_sector = 512;
    kmalloc_size_log = 0;

    // Initialize global FS state to allow initial read
    // fat_mount relies on fat_read_sectors which relies on bpb.bytes_per_sector
    fat_global_fs.bpb.bytes_per_sector = 512;

    printf("Mounting with fat_size=%u, bps=%u (Should overflow 32-bit if not fixed)...\n", mock_fat_size_32, mock_bytes_per_sector);
    fs_node_t *mount_node = fat_mount(NULL, 0, &device_node);

    printf("kmalloc called with size: %zu\n", kmalloc_size_log);

    if (kmalloc_size_log == 512) {
        printf("VULNERABILITY CONFIRMED: kmalloc size wrapped to 512!\n");
        return 1;
    } else if (kmalloc_size_log > 4000000000UL) {
        printf("SUCCESS: kmalloc size is large (fixed): %zu\n", kmalloc_size_log);
        return 0;
    } else if (kmalloc_size_log == 0) {
        printf("SUCCESS: kmalloc not called (mount detected overflow/size and skipped allocation)\n");
        return 0;
    } else {
        printf("Unexpected size: %zu\n", kmalloc_size_log);
        return 1;
    }

    return 0;
}
