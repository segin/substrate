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
    (void)data;
    (void)timeout_ms;
    memset(last_cdb, 0, sizeof(last_cdb));
    memcpy(last_cdb, cdb, cdb_len > sizeof(last_cdb) ? sizeof(last_cdb) : cdb_len);
    last_cdb_len = cdb_len;
    last_data_len = data_len;
    last_flags = flags;
    return 0;
}

void scsi_cdb_read_10(uint8_t *cdb, uint32_t lba, uint16_t count) {
    (void)cdb; (void)lba; (void)count;
}

void scsi_cdb_write_10(uint8_t *cdb, uint32_t lba, uint16_t count) {
    (void)cdb; (void)lba; (void)count;
}

void scsi_cdb_read_16(uint8_t *cdb, uint64_t lba, uint32_t count) {
    (void)cdb; (void)lba; (void)count;
}

void scsi_cdb_write_16(uint8_t *cdb, uint64_t lba, uint32_t count) {
    (void)cdb; (void)lba; (void)count;
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

int main(void) {
    test_attach_disk_and_lookup();
    test_attach_cdrom_uses_2048_sector_size();
    test_attach_rejects_non_block_types();
    test_detach_unregisters_and_removes_lookup();
    test_detach_unknown_device_fails();
    test_cdrom_helpers_emit_expected_cdbs();
    puts("host_test_scsi_dev: PASS");
    return 0;
}
