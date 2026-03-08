#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifndef HOST_TEST
#define HOST_TEST
#endif

#include <sys/types.h>
#include <vfs/vfs.h>

void kprint(const char *msg) {
    printf("[KERNEL] %s", msg);
}

void *kmalloc(size_t size) {
    return calloc(1, size);
}

void kfree(void *ptr, size_t size) {
    (void)size;
    free(ptr);
}

#include <sys/lock.h>
void mutex_init(mutex_t *m, const char *name) { (void)m; (void)name; }
void mutex_lock(mutex_t *m) { (void)m; }
void mutex_unlock(mutex_t *m) { (void)m; }

void vfs_register_filesystem(filesystem_t *fs) {
    (void)fs;
}

int64_t get_time(void) {
    return 0;
}

#include <vm/uma.h>
uma_zone_t *uma_zcreate(const char *name, size_t size, uma_ctor ctor, uma_dtor dtor, uma_init init, uma_fini fini, int align, uint32_t flags) {
    (void)ctor; (void)dtor; (void)init; (void)fini; (void)align; (void)flags;
    uma_zone_t *zone = (uma_zone_t *)calloc(1, sizeof(uma_zone_t));
    if (zone) {
        zone->uz_name = name;
        zone->uz_size = size;
        zone->uz_flags = flags;
    }
    return zone;
}
void *uma_zalloc(uma_zone_t *zone, int flags) {
    (void)flags;
    if (!zone) return NULL;
    return calloc(1, zone->uz_size);
}
void uma_zfree(uma_zone_t *zone, void *item) {
    (void)zone;
    free(item);
}
void uma_zone_set_max(uma_zone_t *zone, int max) { (void)zone; (void)max; }

#define vasprintf kernel_vasprintf
#include <fs/ext2/ext2.c>

#define MOCK_DISK_SIZE (1024 * 1024)
static uint8_t mock_disk[MOCK_DISK_SIZE];

static size_t mock_read(fs_node_t *node, off_t offset, size_t size, uint8_t *buffer) {
    (void)node;
    if ((size_t)offset + size > sizeof(mock_disk)) return 0;
    memcpy(buffer, mock_disk + offset, size);
    return size;
}

static size_t mock_write(fs_node_t *node, off_t offset, size_t size, const uint8_t *buffer) {
    (void)node;
    if ((size_t)offset + size > sizeof(mock_disk)) return 0;
    memcpy(mock_disk + offset, buffer, size);
    return size;
}

static void test_ext2_read_blocks(void) {
    for (int i = 0; i < MOCK_DISK_SIZE; i++) {
        mock_disk[i] = (uint8_t)(i % 256);
    }

    ext2_fs_t fs;
    memset(&fs, 0, sizeof(fs));

    fs_node_t dev_node;
    memset(&dev_node, 0, sizeof(dev_node));
    dev_node.read = mock_read;
    dev_node.write = mock_write;

    fs.device = &dev_node;
    fs.block_size = 1024;

    uint8_t buffer[4096];
    assert(ext2_read_blocks(NULL, 0, 1, buffer) == 0);

    fs.device = NULL;
    assert(ext2_read_blocks(&fs, 0, 1, buffer) == 0);

    fs.device = &dev_node;
    dev_node.read = NULL;
    assert(ext2_read_blocks(&fs, 0, 1, buffer) == 0);

    dev_node.read = mock_read;
    assert(ext2_read_blocks(&fs, 10, 0, buffer) == 0);

    memset(buffer, 0, sizeof(buffer));
    assert(ext2_read_blocks(&fs, 5, 3, buffer) == 3072);
    for (int i = 0; i < 3072; i++) {
        assert(buffer[i] == (uint8_t)((5120 + i) % 256));
    }
}

int main(void) {
    test_ext2_read_blocks();
    puts("PASS: test_ext2_read_blocks");
    return 0;
}
