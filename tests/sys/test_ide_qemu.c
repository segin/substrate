#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <kern/console.h>
#include <drivers/storage/blkdev.h>
#include <drivers/storage/ide/ide.h>

static uint64_t ide_test_lba(blkdev_t *dev, uint32_t sectors_needed) {
    if (dev == NULL || dev->total_sectors <= (uint64_t)sectors_needed + 8) {
        return 8;
    }

    return dev->total_sectors - sectors_needed - 8;
}

static int ide_restore_pio(uint64_t lba, const void *backup, uint8_t count) {
    if (ide_write_sectors(ATA_PRIMARY_IO, 0, (uint32_t)lba, count, backup) < 0) {
        kprint("FAIL: ide_qemu_pio restore failed\n");
        return -1;
    }
    return 0;
}

static int ide_restore_dma(uint64_t lba, const void *backup, uint16_t count) {
    if (ide_dma_write(0, 0, lba, count, backup) < 0) {
        kprint("FAIL: ide_qemu_dma restore failed\n");
        return -1;
    }
    return 0;
}

void test_ide_qemu_pio(void) {
    blkdev_t *dev;
    uint64_t lba;
    uint8_t backup[512];
    uint8_t write_buf[512];
    uint8_t read_buf[512];

    kprint("=== IDE QEMU PIO Integration ===\n");

    dev = blkdev_get("ide0");
    if (dev == NULL) {
        kprint("FAIL: ide_qemu_pio missing ide0\n");
        return;
    }
    if (dev->sector_size != 512) {
        kprintf("FAIL: ide_qemu_pio sector size %u\n", dev->sector_size);
        return;
    }

    lba = ide_test_lba(dev, 1);
    memset(write_buf, 0xA5, sizeof(write_buf));
    memset(read_buf, 0, sizeof(read_buf));

    if (ide_read_sectors(ATA_PRIMARY_IO, 0, (uint32_t)lba, 1, backup) < 0) {
        kprint("FAIL: ide_qemu_pio backup read failed\n");
        return;
    }
    if (ide_write_sectors(ATA_PRIMARY_IO, 0, (uint32_t)lba, 1, write_buf) < 0) {
        kprint("FAIL: ide_qemu_pio write failed\n");
        return;
    }
    if (ide_read_sectors(ATA_PRIMARY_IO, 0, (uint32_t)lba, 1, read_buf) < 0) {
        (void)ide_restore_pio(lba, backup, 1);
        kprint("FAIL: ide_qemu_pio readback failed\n");
        return;
    }

    if (memcmp(read_buf, write_buf, sizeof(read_buf)) != 0) {
        (void)ide_restore_pio(lba, backup, 1);
        kprint("FAIL: ide_qemu_pio verify mismatch\n");
        return;
    }

    if (ide_restore_pio(lba, backup, 1) < 0) {
        return;
    }

    kprint("PASS: ide_qemu_pio round-trip\n");
}

void test_ide_qemu_dma(void) {
    blkdev_t *dev;
    uint64_t lba;
    uint8_t backup[512];
    uint8_t write_buf[512];
    uint8_t read_buf[512];

    kprint("=== IDE QEMU DMA Integration ===\n");

    dev = blkdev_get("ide0");
    if (dev == NULL) {
        kprint("FAIL: ide_qemu_dma missing ide0\n");
        return;
    }

    lba = ide_test_lba(dev, 1);
    memset(write_buf, 0x5A, sizeof(write_buf));
    memset(read_buf, 0, sizeof(read_buf));

    if (ide_dma_read(0, 0, lba, 1, backup) < 0) {
        kprint("FAIL: ide_qemu_dma backup read failed\n");
        return;
    }
    if (ide_dma_write(0, 0, lba, 1, write_buf) < 0) {
        kprint("FAIL: ide_qemu_dma write failed\n");
        return;
    }
    if (ide_dma_read(0, 0, lba, 1, read_buf) < 0) {
        (void)ide_restore_dma(lba, backup, 1);
        kprint("FAIL: ide_qemu_dma readback failed\n");
        return;
    }

    if (memcmp(read_buf, write_buf, sizeof(read_buf)) != 0) {
        (void)ide_restore_dma(lba, backup, 1);
        kprint("FAIL: ide_qemu_dma verify mismatch\n");
        return;
    }

    if (ide_restore_dma(lba, backup, 1) < 0) {
        return;
    }

    kprint("PASS: ide_qemu_dma round-trip\n");
}

void test_ide_qemu_atapi(void) {
    blkdev_t *dev;
    uint32_t last_lba;
    uint32_t sector_size;
    uint8_t toc[32];
    uint8_t sector[2048];
    uint16_t toc_len;

    kprint("=== IDE QEMU ATAPI Integration ===\n");

    dev = blkdev_get("ide2");
    if (dev == NULL) {
        kprint("FAIL: ide_qemu_atapi missing ide2\n");
        return;
    }

    if (ide_atapi_read_capacity(1, 0, &last_lba, &sector_size) < 0) {
        kprint("FAIL: ide_qemu_atapi read capacity failed\n");
        return;
    }
    if (sector_size != 2048) {
        kprintf("FAIL: ide_qemu_atapi sector size %u\n", sector_size);
        return;
    }

    memset(toc, 0, sizeof(toc));
    if (ide_atapi_read_toc(1, 0, 1, toc, sizeof(toc)) < 0) {
        kprint("FAIL: ide_qemu_atapi read toc failed\n");
        return;
    }

    toc_len = ((uint16_t)toc[0] << 8) | toc[1];
    if (toc_len < 10 || toc[2] == 0 || toc[3] < toc[2]) {
        kprint("FAIL: ide_qemu_atapi toc invalid\n");
        return;
    }

    memset(sector, 0, sizeof(sector));
    if (ide_atapi_read_sectors(1, 0, 16, 1, sector) < 0) {
        kprint("FAIL: ide_qemu_atapi sector read failed\n");
        return;
    }

    if (memcmp(sector + 1, "CD001", 5) != 0) {
        kprint("FAIL: ide_qemu_atapi missing ISO9660 signature\n");
        return;
    }

    kprint("PASS: ide_qemu_atapi capacity/toc/read\n");
}

void test_ide_qemu_extra_channels(void) {
    blkdev_t *tertiary;
    blkdev_t *quaternary;

    kprint("=== IDE QEMU Extra Channel Integration ===\n");

    tertiary = blkdev_get("ide4");
    quaternary = blkdev_get("ide6");

    if (tertiary == NULL || quaternary == NULL) {
        kprint("FAIL: ide_qemu_extra missing ide4/ide6\n");
        return;
    }

    if (tertiary->total_sectors == 0 || quaternary->total_sectors == 0) {
        kprint("FAIL: ide_qemu_extra zero-sized device\n");
        return;
    }

    kprint("PASS: ide_qemu_extra tertiary/quaternary detected\n");
}
