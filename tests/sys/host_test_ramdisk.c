#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <assert.h>

// Mock kernel functions and types
#include <drivers/storage/blkdev.h>

int blkdev_register_count = 0;
void blkdev_register(blkdev_t *dev) {
    (void)dev;
    blkdev_register_count++;
}

void kprint(const char *str) {
    (void)str;
}

int kprintf(const char *fmt, ...) {
    (void)fmt;
    return 0;
}

// Include the driver source
#include "../../sys/drivers/storage/ramdisk.c"

// Tests
void test_ramdisk_create() {
    printf("Testing ramdisk_create...\n");
    ramdisk_count = 0; // Reset static variable in ramdisk.c
    blkdev_register_count = 0;

    char buffer[1024];
    int id = ramdisk_create(buffer, sizeof(buffer));
    assert(id == 0);
    assert(ramdisk_count == 1);
    assert(blkdev_register_count == 1);
    assert(ramdisks[0].priv == buffer);
    assert(ramdisks[0].total_sectors == 2);
    assert(strcmp(ramdisks[0].name, "ram0") == 0);

    // Test invalid params
    assert(ramdisk_create(NULL, 1024) == -1);
    assert(ramdisk_create(buffer, 0) == -1);

    // Test limit
    for (int i = 1; i < MAX_RAMDISKS; i++) {
        assert(ramdisk_create(buffer, 512) == i);
    }
    assert(ramdisk_create(buffer, 512) == -1);

    printf("test_ramdisk_create: PASS\n");
}

void test_ramdisk_init() {
    printf("Testing ramdisk_init...\n");
    ramdisk_count = 0;
    blkdev_register_count = 0;

    char buffer[512];
    ramdisk_init(buffer, sizeof(buffer));
    assert(ramdisk_count == 1);
    assert(blkdev_register_count == 1);

    printf("test_ramdisk_init: PASS\n");
}

void test_ramdisk_read_write() {
    printf("Testing ramdisk read/write...\n");
    ramdisk_count = 0;

    char disk_mem[2048];
    memset(disk_mem, 0, sizeof(disk_mem));
    int id = ramdisk_create(disk_mem, sizeof(disk_mem));
    blkdev_t *dev = &ramdisks[id];

    char write_buf[512];
    for(int i=0; i<512; i++) write_buf[i] = (char)(i & 0xFF);

    // Write sector 1
    int res = dev->write(dev, 1, 1, write_buf);
    assert(res == 0);
    assert(memcmp(disk_mem + 512, write_buf, 512) == 0);

    // Read sector 1
    char read_buf[512];
    memset(read_buf, 0, 512);
    res = dev->read(dev, 1, 1, read_buf);
    assert(res == 0);
    assert(memcmp(read_buf, write_buf, 512) == 0);

    // Multi-sector read/write
    char write_buf_large[1024];
    for(int i=0; i<1024; i++) write_buf_large[i] = (char)((i + 10) & 0xFF);
    res = dev->write(dev, 2, 2, write_buf_large);
    assert(res == 0);
    assert(memcmp(disk_mem + 1024, write_buf_large, 1024) == 0);

    char read_buf_large[1024];
    res = dev->read(dev, 2, 2, read_buf_large);
    assert(res == 0);
    assert(memcmp(read_buf_large, write_buf_large, 1024) == 0);

    printf("test_ramdisk_read_write: PASS\n");
}

void test_ramdisk_bounds() {
    printf("Testing ramdisk bounds checking...\n");
    ramdisk_count = 0;

    char disk_mem[1024]; // 2 sectors
    int id = ramdisk_create(disk_mem, sizeof(disk_mem));
    blkdev_t *dev = &ramdisks[id];

    char buf[512];

    // Valid
    assert(dev->read(dev, 0, 1, buf) == 0);
    assert(dev->read(dev, 1, 1, buf) == 0);
    assert(dev->read(dev, 0, 2, buf) == 0);

    // Invalid
    assert(dev->read(dev, 2, 1, buf) == -1);
    assert(dev->read(dev, 0, 3, buf) == -1);
    assert(dev->read(dev, 1, 2, buf) == -1);

    // Giant values
    assert(dev->read(dev, 0xFFFFFFFFFFFFFFFFULL, 1, buf) == -1);
    assert(dev->read(dev, 0, 0xFFFFFFFF, buf) == -1);

    // Same for write
    assert(dev->write(dev, 2, 1, buf) == -1);
    assert(dev->write(dev, 0, 3, buf) == -1);

    printf("test_ramdisk_bounds: PASS\n");
}

int main() {
    printf("Running Ramdisk Host Tests...\n");
    test_ramdisk_create();
    test_ramdisk_init();
    test_ramdisk_read_write();
    test_ramdisk_bounds();
    printf("All ramdisk tests passed!\n");
    return 0;
}
