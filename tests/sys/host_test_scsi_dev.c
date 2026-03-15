#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <drivers/storage/scsi/scsi.h>
#include <drivers/storage/blkdev.h>

static int register_calls;
static int unregister_calls;
static blkdev_t *last_registered;
static blkdev_t *last_unregistered;
static char last_unregistered_name[32];
static uint8_t last_cdb[16];
static uint8_t last_cdb_len;
static uint32_t last_data_len;
static uint16_t last_flags;
static int read_capacity_calls;
static int execute_failures_before_success;
static int sync_cache_calls;

void kprint(const char *str) { (void)str; }

void blkdev_register_disk(blkdev_t *dev) {
    register_calls++;
    last_registered = dev;
}

void blkdev_unregister(blkdev_t *dev) {
    unregister_calls++;
    last_unregistered = dev;
    strncpy(last_unregistered_name, dev->name, sizeof(last_unregistered_name) - 1);
    last_unregistered_name[sizeof(last_unregistered_name) - 1] = '\0';
}

int scsi_execute_sync(scsi_device_t *dev, uint8_t *cdb, uint8_t cdb_len,
                      void *data, uint32_t data_len, uint16_t flags,
                      uint32_t timeout_ms) {
    (void)dev;
    (void)timeout_ms;
    memset(last_cdb, 0, sizeof(last_cdb));
    memcpy(last_cdb, cdb, cdb_len > sizeof(last_cdb) ? sizeof(last_cdb) : cdb_len);
    last_cdb_len = cdb_len;
    last_data_len = data_len;
    last_flags = flags;
    if (cdb[0] != SCSI_CMD_READ_CAPACITY_10 &&
        cdb[0] != SCSI_CMD_READ_CAPACITY_16 &&
        execute_failures_before_success-- > 0) {
        return -1;
    }
    if (cdb[0] == SCSI_CMD_READ_CAPACITY_10 && data != NULL && data_len >= 8) {
        uint8_t *buf = data;

        read_capacity_calls++;
        memset(buf, 0, data_len);
        buf[3] = 0x7F;
        buf[6] = 0x02;
        return 0;
    }
    if (cdb[0] == SCSI_CMD_READ_CAPACITY_16 && data != NULL && data_len >= 12) {
        uint8_t *buf = data;

        read_capacity_calls++;
        memset(buf, 0, data_len);
        buf[7] = 0xFF;
        buf[10] = 0x02;
        return 0;
    }
    return 0;
}

void scsi_cdb_read_10(uint8_t *cdb, uint32_t lba, uint16_t count) {
    memset(cdb, 0, 10);
    cdb[0] = SCSI_CMD_READ_10;
    (void)lba; (void)count;
}

void scsi_cdb_write_10(uint8_t *cdb, uint32_t lba, uint16_t count) {
    memset(cdb, 0, 10);
    cdb[0] = SCSI_CMD_WRITE_10;
    (void)lba; (void)count;
}

void scsi_cdb_read_16(uint8_t *cdb, uint64_t lba, uint32_t count) {
    memset(cdb, 0, 16);
    cdb[0] = SCSI_CMD_READ_16;
    (void)lba; (void)count;
}

void scsi_cdb_write_16(uint8_t *cdb, uint64_t lba, uint32_t count) {
    memset(cdb, 0, 16);
    cdb[0] = SCSI_CMD_WRITE_16;
    (void)lba; (void)count;
}

int scsi_read_capacity(scsi_device_t *dev, uint64_t *sectors, uint32_t *sector_size) {
    uint8_t cdb[10] = {0};
    uint8_t buf[8];

    cdb[0] = SCSI_CMD_READ_CAPACITY_10;
    if (scsi_execute_sync(dev, cdb, 10, buf, sizeof(buf), SCSI_REQ_READ, 10000) < 0) {
        return -1;
    }

    *sectors = (uint64_t)(((uint32_t)buf[0] << 24) |
                          ((uint32_t)buf[1] << 16) |
                          ((uint32_t)buf[2] << 8) |
                          buf[3]) + 1ULL;
    *sector_size = ((uint32_t)buf[4] << 24) |
                   ((uint32_t)buf[5] << 16) |
                   ((uint32_t)buf[6] << 8) |
                   buf[7];
    return 0;
}

int scsi_synchronize_cache(scsi_device_t *dev) {
    uint8_t cdb[10] = {0};

    cdb[0] = SCSI_CMD_SYNCHRONIZE_CACHE;
    sync_cache_calls++;
    return scsi_execute_sync(dev, cdb, 10, NULL, 0, 0, 30000);
}

#include "../../sys/drivers/storage/scsi/scsi_dev.c"

static void reset_state(void) {
    register_calls = 0;
    unregister_calls = 0;
    last_registered = NULL;
    last_unregistered = NULL;
    last_unregistered_name[0] = '\0';
    memset(last_cdb, 0, sizeof(last_cdb));
    last_cdb_len = 0;
    last_data_len = 0;
    last_flags = 0;
    read_capacity_calls = 0;
    execute_failures_before_success = 0;
    sync_cache_calls = 0;
    scsi_dev_init();
}

static void test_attach_disk_and_lookup(void) {
    scsi_device_t dev;
    scsi_blk_dev_t *sbd;

    reset_state();
    memset(&dev, 0, sizeof(dev));
    dev.type = SCSI_TYPE_DISK;
    dev.capacity = 4096;
    dev.sector_size = 512;
    strcpy(dev.vendor, "ACME");
    strcpy(dev.product, "DISK");

    assert(scsi_dev_attach(&dev) == 0);
    assert(register_calls == 1);
    assert(last_registered != NULL);
    assert(strcmp(last_registered->name, "scsi0") == 0);
    assert(last_registered->sector_size == 512);
    assert(last_registered->total_sectors == 4096);

    sbd = scsi_dev_lookup("scsi0");
    assert(sbd != NULL);
    assert(sbd->scsi_dev == &dev);
}

static void test_attach_cdrom_uses_2048_sector_size(void) {
    scsi_device_t dev;

    reset_state();
    memset(&dev, 0, sizeof(dev));
    dev.type = SCSI_TYPE_ROM;
    dev.capacity = 123;
    dev.sector_size = 512;

    assert(scsi_dev_attach(&dev) == 0);
    assert(last_registered != NULL);
    assert(last_registered->sector_size == 2048);
}

static void test_attach_reads_capacity_when_missing(void) {
    scsi_device_t dev;

    reset_state();
    memset(&dev, 0, sizeof(dev));
    dev.type = SCSI_TYPE_DISK;

    assert(scsi_dev_attach(&dev) == 0);
    assert(read_capacity_calls == 1);
    assert(dev.capacity == 128ULL);
    assert(dev.sector_size == 512);
    assert(last_registered != NULL);
    assert(last_registered->total_sectors == 128ULL);
    assert(last_registered->sector_size == 512);
}

static void test_attach_assigns_sequential_names(void) {
    scsi_device_t dev0;
    scsi_device_t dev1;

    reset_state();
    memset(&dev0, 0, sizeof(dev0));
    memset(&dev1, 0, sizeof(dev1));
    dev0.type = SCSI_TYPE_DISK;
    dev1.type = SCSI_TYPE_DISK;
    dev0.capacity = 1;
    dev1.capacity = 1;
    dev0.sector_size = 512;
    dev1.sector_size = 512;

    assert(scsi_dev_attach(&dev0) == 0);
    assert(scsi_dev_lookup("scsi0") != NULL);
    assert(scsi_dev_attach(&dev1) == 0);
    assert(scsi_dev_lookup("scsi1") != NULL);
}

static void test_attach_rejects_non_block_types(void) {
    scsi_device_t dev;

    reset_state();
    memset(&dev, 0, sizeof(dev));
    dev.type = SCSI_TYPE_TAPE;

    assert(scsi_dev_attach(&dev) < 0);
    assert(register_calls == 0);
}

static void test_detach_unregisters_and_removes_lookup(void) {
    scsi_device_t dev;

    reset_state();
    memset(&dev, 0, sizeof(dev));
    dev.type = SCSI_TYPE_DISK;
    dev.capacity = 64;
    dev.sector_size = 512;

    assert(scsi_dev_attach(&dev) == 0);
    assert(scsi_dev_lookup("scsi0") != NULL);

    assert(scsi_dev_detach(&dev) == 0);
    assert(sync_cache_calls == 1);
    assert(last_cdb[0] == SCSI_CMD_SYNCHRONIZE_CACHE);
    assert(unregister_calls == 1);
    assert(last_unregistered != NULL);
    assert(strcmp(last_unregistered_name, "scsi0") == 0);
    assert(scsi_dev_lookup("scsi0") == NULL);
}

static void test_detach_unknown_device_fails(void) {
    scsi_device_t dev;

    reset_state();
    memset(&dev, 0, sizeof(dev));
    dev.type = SCSI_TYPE_DISK;

    assert(scsi_dev_detach(&dev) < 0);
    assert(unregister_calls == 0);
}

static void test_cdrom_helpers_emit_expected_cdbs(void) {
    scsi_device_t dev;
    uint8_t toc[32];

    reset_state();
    memset(&dev, 0, sizeof(dev));
    dev.type = SCSI_TYPE_ROM;

    assert(scsi_read_toc(&dev, toc, sizeof(toc)) == 0);
    assert(last_cdb_len == 10);
    assert(last_cdb[0] == SCSI_CMD_READ_TOC);
    assert(last_cdb[1] == 0x02);
    assert(last_cdb[7] == 0x00);
    assert(last_cdb[8] == sizeof(toc));
    assert(last_data_len == sizeof(toc));
    assert((last_flags & SCSI_REQ_READ) != 0);

    assert(scsi_lock_door(&dev, 1) == 0);
    assert(last_cdb_len == 6);
    assert(last_cdb[0] == SCSI_CMD_PREVENT_ALLOW);
    assert(last_cdb[4] == 0x01);
    assert(last_data_len == 0);
}

static void test_cdrom_media_change_refreshes_capacity_and_retries(void) {
    scsi_device_t dev;
    uint8_t toc[32];

    reset_state();
    memset(&dev, 0, sizeof(dev));
    dev.type = SCSI_TYPE_ROM;
    dev.removable = 1;

    execute_failures_before_success = 1;
    assert(scsi_read_toc(&dev, toc, sizeof(toc)) == 0);
    assert(read_capacity_calls == 1);
    assert(last_cdb[0] == SCSI_CMD_READ_TOC);
}

static void test_worm_write_is_rejected(void) {
    scsi_device_t dev;
    scsi_blk_dev_t *sbd;
    uint8_t buf[2048];

    reset_state();
    memset(&dev, 0, sizeof(dev));
    dev.type = SCSI_TYPE_WORM;
    dev.capacity = 32;
    dev.sector_size = 2048;

    assert(scsi_dev_attach(&dev) == 0);
    sbd = scsi_dev_lookup("scsi0");
    assert(sbd != NULL);
    assert(sbd->blkdev.write(&sbd->blkdev, 0, 1, buf) < 0);
}

int main(void) {
    test_attach_disk_and_lookup();
    test_attach_cdrom_uses_2048_sector_size();
    test_attach_reads_capacity_when_missing();
    test_attach_assigns_sequential_names();
    test_attach_rejects_non_block_types();
    test_detach_unregisters_and_removes_lookup();
    test_detach_unknown_device_fails();
    test_cdrom_helpers_emit_expected_cdbs();
    test_cdrom_media_change_refreshes_capacity_and_retries();
    test_worm_write_is_rejected();
    puts("host_test_scsi_dev: PASS");
    return 0;
}
