/*
 * host_test_fat_vuln - fat_mount() must not size the FAT cache with a
 * 32-bit product that wraps.
 *
 * The cache is fat_size * bytes_per_sector bytes.  With fat_size_32 =
 * 8388609 (0x800001) and 512-byte sectors that is 0x100000200, which used
 * to wrap to 512: the driver allocated 512 bytes and then read the whole
 * FAT into them.  A mount of such a volume must leave the FAT uncached,
 * while an ordinary volume still gets a cache of the full size.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>

#include <vfs/vfs.h>

void kprint(const char *str) {
    (void)str;
}

/*
 * fat_mount() reads a whole boot sector into the 36-byte BPB at the head
 * of a kmalloc'ed fat_fs_t; the kernel allocator rounds that up to a full
 * slab, so round host allocations up too (as host_test_fat_mount does).
 * Refuse anything large rather than really allocating it.
 */
void *kmalloc(size_t size) {
    if (size > 16u * 1024 * 1024) return NULL;
    return calloc(1, size < 4096 ? 4096 : size);
}

void kfree(void *ptr, size_t size) {
    (void)size;
    free(ptr);
}

void vfs_register_filesystem(filesystem_t *fs) {
    (void)fs;
}

#include "../../sys/fs/fat/fat.c"
#include "fat_host_stubs.h"

size_t blkdev_read_bytes(blkdev_t *dev, uint64_t offset, size_t size, void *buffer) {
    (void)dev; (void)offset; (void)size; (void)buffer;
    return 0;
}

/* Sector 0 holds the boot sector; every other byte of the device is zero. */
static uint8_t boot_sector[512];

static size_t mock_device_read(fs_node_t *node, off_t offset, size_t size, uint8_t *buffer) {
    (void)node;
    memset(buffer, 0, size);
    if (offset >= 0 && (size_t)offset < sizeof(boot_sector)) {
        size_t n = sizeof(boot_sector) - (size_t)offset;
        if (n > size) n = size;
        memcpy(buffer, boot_sector + offset, n);
    }
    return size;
}

/* A FAT32 boot sector: 512-byte sectors, one sector per cluster, one FAT. */
static void make_boot_sector(uint32_t fat_size_32, uint32_t total_sectors) {
    fat_bpb_t *bpb = (fat_bpb_t *)boot_sector;
    fat32_ext_bpb_t *ext = (fat32_ext_bpb_t *)(boot_sector + sizeof(fat_bpb_t));

    memset(boot_sector, 0, sizeof(boot_sector));
    bpb->bytes_per_sector = 512;
    bpb->sectors_per_cluster = 1;
    bpb->reserved_sectors = 32;
    bpb->fat_count = 1;
    bpb->fat_size_16 = 0;                /* FAT32: size is in the ext BPB */
    bpb->total_sectors_32 = total_sectors;
    ext->fat_size_32 = fat_size_32;
    ext->root_cluster = 2;
}

static fat_fs_t *mount_fs(fs_node_t *device) {
    fs_node_t *root = fat_mount(NULL, 0, device);
    if (!root) return NULL;
    return ((fat_node_t *)(uintptr_t)root->impl)->fs;
}

int main(void) {
    static fs_node_t device_node;
    fat_fs_t *fs;

    device_node.read = mock_device_read;

    printf("Running FAT Vulnerability Test (Integer Overflow)...\n");

    /* 8388609 * 512 = 0x100000200: wraps to 512 in 32 bits. */
    make_boot_sector(8388609, 10000000);
    fs = mount_fs(&device_node);
    if (!fs) {
        printf("FAIL: mount of the oversized-FAT volume failed\n");
        return 1;
    }
    if (fs->fat_table != NULL || fs->fat_table_size != 0) {
        printf("FAIL: oversized FAT was cached (fat_table_size=%u)\n", fs->fat_table_size);
        return 1;
    }

    /* Control: a 1000-sector FAT is cached whole. */
    make_boot_sector(1000, 100000);
    fs = mount_fs(&device_node);
    if (!fs) {
        printf("FAIL: mount of the ordinary volume failed\n");
        return 1;
    }
    if (fs->fat_table == NULL || fs->fat_table_size != 1000u * 512) {
        printf("FAIL: ordinary FAT not cached in full (fat_table_size=%u)\n",
               fs->fat_table_size);
        return 1;
    }

    printf("host_test_fat_vuln: PASS\n");
    return 0;
}
