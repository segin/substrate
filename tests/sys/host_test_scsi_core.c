#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>

#include "../../sys/drivers/storage/scsi/scsi.h"

static int execute_calls;
static int detach_calls;
static int create_bus_node_calls;
static uint8_t last_bus_id;
static uint8_t last_cdb[16];
static uint8_t last_cdb_len;
static uint32_t last_flags;
static uint32_t last_data_len;
static int mock_status_code;

void kprint(const char *str) {
    (void)str;
}

int kprintf(const char *fmt, ...) {
    (void)fmt;
    return 0;
}

int64_t get_uptime_ms(void) {
    static int64_t ticks;
    return ++ticks;
}

void scsi_dev_init(void) {}
void scsi_ctl_init(void) {}
void scsi_auto_attach(scsi_device_t *dev) { (void)dev; }
int scsi_create_bus_node(scsi_link_t *link, uint8_t bus_id) {
    (void)link;
    create_bus_node_calls++;
    last_bus_id = bus_id;
    return 0;
}

int scsi_dev_detach(scsi_device_t *scsi_dev) {
    (void)scsi_dev;
    detach_calls++;
    return 0;
}

static int mock_execute(scsi_link_t *link, scsi_request_t *req) {
    (void)link;
    execute_calls++;
    last_cdb_len = req->cdb_len;
    memcpy(last_cdb, req->cdb, sizeof(last_cdb));
    last_flags = req->flags;
    last_data_len = req->data_len;
    req->status = (uint8_t)mock_status_code;

    if (mock_status_code == SCSI_STATUS_GOOD) {
        if (req->data && req->data_len != 0) {
            memset(req->data, 0, req->data_len);

            if (req->cdb[0] == SCSI_CMD_READ_CAPACITY_10 && req->data_len >= 8) {
                uint8_t *buf = req->data;

                buf[0] = 0xFF;
                buf[1] = 0xFF;
                buf[2] = 0xFF;
                buf[3] = 0xFF;
                buf[4] = 0x00;
                buf[5] = 0x00;
                buf[6] = 0x02;
                buf[7] = 0x00;
            } else if (req->cdb[0] == SCSI_CMD_READ_CAPACITY_16 &&
                       req->data_len >= 12) {
                uint8_t *buf = req->data;

                buf[0] = 0x00;
                buf[1] = 0x00;
                buf[2] = 0x00;
                buf[3] = 0x00;
                buf[4] = 0xFF;
                buf[5] = 0xFF;
                buf[6] = 0xFF;
                buf[7] = 0xFF;
                buf[8] = 0x00;
                buf[9] = 0x00;
                buf[10] = 0x02;
                buf[11] = 0x00;
            }
            req->data_xfer = req->data_len;
        }
        return 0;
    }

    return -1;
}

#include "../../sys/drivers/storage/scsi/scsi.c"

static void reset_state(void) {
    execute_calls = 0;
    detach_calls = 0;
    create_bus_node_calls = 0;
    last_bus_id = 0xff;
    memset(last_cdb, 0, sizeof(last_cdb));
    last_cdb_len = 0;
    last_flags = 0;
    last_data_len = 0;
    mock_status_code = SCSI_STATUS_BUSY;
    scsi_init();
}

static void test_register_link_sets_defaults_and_scans(void) {
    scsi_link_t link;

    reset_state();
    memset(&link, 0, sizeof(link));
    strcpy(link.name, "mock0");
    link.execute = mock_execute;
    link.bus_id = 3;

    assert(scsi_register_link(&link) == 0);
    assert(link.max_targets == SCSI_MAX_TARGETS);
    assert(link.max_luns == 8);
    assert(link.adapter_queue_depth == 1);
    assert(create_bus_node_calls == 1);
    assert(last_bus_id == 3);
    assert(execute_calls == SCSI_MAX_TARGETS);
}

static void test_unregister_link_detaches_registered_devices(void) {
    scsi_link_t link;
    scsi_device_t *dev;

    reset_state();
    memset(&link, 0, sizeof(link));
    strcpy(link.name, "mock1");
    link.execute = mock_execute;
    link.max_targets = 1;

    assert(scsi_register_link(&link) == 0);

    dev = scsi_device_alloc();
    assert(dev != NULL);
    dev->bus = 0;
    dev->target = 0;
    dev->lun = 0;
    dev->link = &link;
    strcpy(dev->vendor, "MOCK");
    strcpy(dev->product, "DISK");
    assert(scsi_device_register(dev) == 0);
    assert(scsi_device_lookup(0, 0, 0) == dev);

    scsi_unregister_link(&link);

    assert(detach_calls == 1);
    assert(scsi_device_lookup(0, 0, 0) == NULL);
}

static void test_standard_wrappers_and_16byte_helpers(void) {
    scsi_device_t dev;
    uint64_t sectors = 0;
    uint32_t sector_size = 0;
    uint8_t mode_buf[300];
    uint8_t sense_buf[18];

    reset_state();
    memset(&dev, 0, sizeof(dev));
    dev.link = &(scsi_link_t){
        .execute = mock_execute,
    };

    mock_status_code = SCSI_STATUS_GOOD;
    assert(scsi_read_capacity(&dev, &sectors, &sector_size) == 0);
    assert(execute_calls == 2);
    assert(last_cdb_len == 16);
    assert(last_cdb[0] == SCSI_CMD_READ_CAPACITY_16);
    assert(last_cdb[1] == 0x10);
    assert(sectors == 0x100000000ULL);
    assert(sector_size == 512);

    assert(scsi_request_sense(&dev, sense_buf, sizeof(sense_buf)) == 0);
    assert(last_cdb_len == 6);
    assert(last_cdb[0] == SCSI_CMD_REQUEST_SENSE);
    assert(last_cdb[4] == sizeof(sense_buf));

    memset(mode_buf, 0, sizeof(mode_buf));
    assert(scsi_mode_sense(&dev, 0x3F, mode_buf, 64) == 0);
    assert(last_cdb_len == 6);
    assert(last_cdb[0] == SCSI_CMD_MODE_SENSE_6);
    assert(last_cdb[2] == 0x3F);
    assert(last_cdb[4] == 64);

    assert(scsi_mode_sense(&dev, 0x08, mode_buf, sizeof(mode_buf)) == 0);
    assert(last_cdb_len == 10);
    assert(last_cdb[0] == SCSI_CMD_MODE_SENSE_10);
    assert(last_cdb[2] == 0x08);
    assert(last_cdb[7] == 0x01);
    assert(last_cdb[8] == 0x2C);

    assert(scsi_synchronize_cache(&dev) == 0);
    assert(last_cdb_len == 10);
    assert(last_cdb[0] == SCSI_CMD_SYNCHRONIZE_CACHE);

    {
        uint8_t cdb[16];

        scsi_cdb_read_16(cdb, 0x1122334455667788ULL, 0x01020304U);
        assert(cdb[0] == SCSI_CMD_READ_16);
        assert(cdb[2] == 0x11 && cdb[9] == 0x88);
        assert(cdb[10] == 0x01 && cdb[13] == 0x04);

        scsi_cdb_write_16(cdb, 0x8877665544332211ULL, 0x05060708U);
        assert(cdb[0] == SCSI_CMD_WRITE_16);
        assert(cdb[2] == 0x88 && cdb[9] == 0x11);
        assert(cdb[10] == 0x05 && cdb[13] == 0x08);
    }

    assert(strcmp(scsi_sense_string(SCSI_SENSE_COMPLETED, 0, 0), "Completed") == 0);
}

int main(void) {
    test_register_link_sets_defaults_and_scans();
    test_unregister_link_detaches_registered_devices();
    test_standard_wrappers_and_16byte_helpers();
    puts("host_test_scsi_core: PASS");
    return 0;
}
