#include <stdint.h>
#include <string.h>

#include <drivers/storage/blkdev.h>
#include <kern/console.h>

void test_floppy_qemu(void) {
    blkdev_t *dev;
    uint8_t backup[2048];
    uint8_t write_buf[2048];
    uint8_t read_buf[2048];
    uint64_t lba = 16;
    uint32_t count = 4;

    kprint("=== Floppy QEMU Integration ===\n");

    dev = blkdev_get("fd0");
    if (dev == NULL) {
        kprint("FAIL: floppy_qemu missing fd0\n");
        return;
    }
    if (dev->sector_size != 512 || dev->total_sectors == 0) {
        kprint("FAIL: floppy_qemu invalid geometry\n");
        return;
    }
    if (lba >= dev->total_sectors) {
        kprint("FAIL: floppy_qemu test LBA out of range\n");
        return;
    }

    memset(write_buf, 0x3C, sizeof(write_buf));
    memset(read_buf, 0x00, sizeof(read_buf));

    if (dev->read(dev, lba, count, backup) != 0) {
        kprint("FAIL: floppy_qemu backup read failed\n");
        return;
    }
    if (dev->write(dev, lba, count, write_buf) != 0) {
        kprint("FAIL: floppy_qemu write failed\n");
        return;
    }
    if (dev->read(dev, lba, count, read_buf) != 0) {
        (void)dev->write(dev, lba, count, backup);
        kprint("FAIL: floppy_qemu readback failed\n");
        return;
    }
    if (memcmp(write_buf, read_buf, sizeof(read_buf)) != 0) {
        (void)dev->write(dev, lba, count, backup);
        kprint("FAIL: floppy_qemu verify mismatch\n");
        return;
    }
    if (dev->write(dev, lba, count, backup) != 0) {
        kprint("FAIL: floppy_qemu restore failed\n");
        return;
    }

    kprint("PASS: floppy_qemu round-trip\n");
}
