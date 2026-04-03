#include <stdint.h>
#include <string.h>

#include <drivers/storage/blkdev.h>
#include <kern/console.h>
#include <kern/sched.h>
#include <kern/time.h>
#include <sys/floppy.h>

static int floppy_buffer_is_byte(const uint8_t *buf, size_t len, uint8_t value) {
    size_t i;

    for (i = 0; i < len; i++) {
        if (buf[i] != value) {
            return 0;
        }
    }
    return 1;
}

static blkdev_t *floppy_lookup_fd0(void) {
    blkdev_t *dev = blkdev_get("fd0");

    if (dev == NULL) {
        kprint("FAIL: floppy missing fd0\n");
        return NULL;
    }
    if (dev->sector_size != 512 || dev->total_sectors == 0) {
        kprint("FAIL: floppy invalid geometry\n");
        return NULL;
    }
    return dev;
}

void test_floppy_qemu(void) {
    blkdev_t *dev;
    uint8_t backup[2048];
    uint8_t write_buf[2048];
    uint8_t read_buf[2048];
    uint64_t lba = 16;
    uint32_t count = 4;

    kprint("=== Floppy QEMU Integration ===\n");

    dev = floppy_lookup_fd0();
    if (dev == NULL) {
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

void test_floppy_format_qemu(void) {
    blkdev_t *dev;
    struct floppy_format_track fmt;
    uint8_t sector0[512];
    uint8_t sector_last[512];
    uint32_t sectors_per_track = 18;

    kprint("=== Floppy Format Integration ===\n");

    dev = floppy_lookup_fd0();
    if (dev == NULL) {
        return;
    }
    if (dev->total_sectors < sectors_per_track) {
        kprint("FAIL: floppy_format insufficient sectors\n");
        return;
    }
    if (dev->ioctl == NULL) {
        kprint("FAIL: floppy_format missing ioctl\n");
        return;
    }

    memset(&fmt, 0, sizeof(fmt));
    fmt.cylinder = 0;
    fmt.head = 0;
    fmt.fill = 0xE5;

    if (dev->ioctl(dev, FLOPPY_IOCTL_FORMAT_TRACK, &fmt) != 0) {
        kprint("FAIL: floppy_format ioctl failed\n");
        return;
    }
    if (dev->read(dev, 0, 1, sector0) != 0) {
        kprint("FAIL: floppy_format read sector0 failed\n");
        return;
    }
    if (dev->read(dev, sectors_per_track - 1, 1, sector_last) != 0) {
        kprint("FAIL: floppy_format read last sector failed\n");
        return;
    }
    if (!floppy_buffer_is_byte(sector0, sizeof(sector0), 0xE5)) {
        kprint("FAIL: floppy_format sector0 fill mismatch\n");
        return;
    }
    if (!floppy_buffer_is_byte(sector_last, sizeof(sector_last), 0xE5)) {
        kprint("FAIL: floppy_format last sector fill mismatch\n");
        return;
    }

    kprint("PASS: floppy_format format/read-back\n");
}

void test_floppy_change_qemu(void) {
    blkdev_t *dev;
    uint8_t sector[512];
    int64_t deadline_ms;

    kprint("=== Floppy Disk Change Integration ===\n");

    dev = floppy_lookup_fd0();
    if (dev == NULL) {
        return;
    }

    if (dev->read(dev, 0, 1, sector) != 0) {
        kprint("FAIL: floppy_change initial read failed\n");
        return;
    }
    if (!floppy_buffer_is_byte(sector, sizeof(sector), 0x11)) {
        kprint("FAIL: floppy_change initial pattern mismatch\n");
        return;
    }

    deadline_ms = get_uptime_ms() + 8000;
    while (get_uptime_ms() < deadline_ms) {
        if (dev->read(dev, 0, 1, sector) == 0 &&
            floppy_buffer_is_byte(sector, sizeof(sector), 0xA5)) {
            kprint("PASS: floppy_change media change detected\n");
            return;
        }
        sched_yield();
    }

    kprint("FAIL: floppy_change timed out waiting for new media\n");
}
