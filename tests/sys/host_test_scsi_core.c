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
static int mock_execute_failures_before_success;
static int mock_request_sense_count;
static uint8_t mock_sense_key_code;
static uint8_t mock_sense_asc;
static uint8_t mock_sense_ascq;
static int callback_calls;
static int callback_last_error;
static int callback_last_state;
static int auto_attach_calls;
static int discovery_report_luns_fail;
static uint32_t discovery_present_luns_mask;
static uint8_t discovery_device_type;
static uint8_t discovery_version;
static char discovery_vendor[9];
static char discovery_product[17];
static char discovery_revision[5];

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
void scsi_auto_attach(scsi_device_t *dev) {
    (void)dev;
    auto_attach_calls++;
}
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

static void record_callback(scsi_request_t *req) {
    callback_calls++;
    callback_last_error = req->error;
    callback_last_state = req->state;
}

static int mock_execute(scsi_link_t *link, scsi_request_t *req) {
    (void)link;
    execute_calls++;
    last_cdb_len = req->cdb_len;
    memcpy(last_cdb, req->cdb, sizeof(last_cdb));
    last_flags = req->flags;
    last_data_len = req->data_len;
    req->status = (uint8_t)mock_status_code;

    if (req->cdb[0] == SCSI_CMD_REQUEST_SENSE) {
        if (req->data != NULL && req->data_len >= 18) {
            uint8_t *sense = req->data;

            memset(sense, 0, req->data_len);
            sense[0] = 0x70;
            sense[2] = mock_sense_key_code;
            sense[12] = mock_sense_asc;
            sense[13] = mock_sense_ascq;
            req->data_xfer = req->data_len;
        }
        mock_request_sense_count++;
        req->status = SCSI_STATUS_GOOD;
        return 0;
    }

    if (mock_status_code == SCSI_STATUS_GOOD ||
        (mock_status_code != SCSI_STATUS_GOOD &&
         mock_execute_failures_before_success-- <= 0)) {
        req->status = SCSI_STATUS_GOOD;
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

static int discovery_execute(scsi_link_t *link, scsi_request_t *req) {
    (void)link;

    if (req == NULL || req->device == NULL) {
        return -1;
    }

    if ((discovery_present_luns_mask & (1U << req->device->lun)) == 0) {
        return -1;
    }

    req->status = SCSI_STATUS_GOOD;

    switch (req->cdb[0]) {
    case SCSI_CMD_TEST_UNIT_READY:
        return 0;
    case SCSI_CMD_INQUIRY:
        if (req->data == NULL || req->data_len < sizeof(struct scsi_inquiry_data)) {
            return -1;
        }
        {
            struct scsi_inquiry_data *inq = req->data;

            memset(inq, 0, sizeof(*inq));
            inq->device_type = discovery_device_type & 0x1F;
            inq->version = discovery_version;
            inq->response_format = 2;
            inq->additional_len = 31;
            memcpy(inq->vendor, discovery_vendor, 8);
            memcpy(inq->product, discovery_product, 16);
            memcpy(inq->revision, discovery_revision, 4);
            req->data_xfer = sizeof(*inq);
        }
        return 0;
    case SCSI_CMD_READ_CAPACITY_10:
        if (req->data == NULL || req->data_len < 8) {
            return -1;
        }
        memset(req->data, 0, req->data_len);
        ((uint8_t *)req->data)[3] = 0xFF;
        ((uint8_t *)req->data)[6] = 0x02;
        req->data_xfer = 8;
        return 0;
    case SCSI_CMD_READ_CAPACITY_16:
        if (req->data == NULL || req->data_len < 12) {
            return -1;
        }
        memset(req->data, 0, req->data_len);
        ((uint8_t *)req->data)[7] = 0xFF;
        ((uint8_t *)req->data)[10] = 0x02;
        req->data_xfer = 12;
        return 0;
    case SCSI_CMD_REPORT_LUNS:
        if (discovery_report_luns_fail || req->data == NULL ||
            req->data_len < sizeof(struct scsi_report_luns_data)) {
            return -1;
        }
        {
            struct scsi_report_luns_data *luns = req->data;
            uint32_t count = 0;

            memset(luns, 0, sizeof(*luns));
            for (uint16_t lun = 0; lun < 32; lun++) {
                if ((discovery_present_luns_mask & (1U << lun)) == 0) {
                    continue;
                }
                luns->luns[count++] = (uint64_t)lun;
            }
            scsi_put_be32((uint8_t *)&luns->length, count * 8U);
            req->data_xfer = sizeof(*luns);
        }
        return 0;
    default:
        return 0;
    }
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
    mock_status_code = SCSI_STATUS_GOOD;
    mock_execute_failures_before_success = 0;
    mock_request_sense_count = 0;
    mock_sense_key_code = SCSI_SENSE_NO_SENSE;
    mock_sense_asc = 0;
    mock_sense_ascq = 0;
    callback_calls = 0;
    callback_last_error = 0;
    callback_last_state = 0;
    auto_attach_calls = 0;
    discovery_report_luns_fail = 0;
    discovery_present_luns_mask = 1U;
    discovery_device_type = SCSI_TYPE_DISK;
    discovery_version = 5;
    memset(discovery_vendor, ' ', 8);
    memset(discovery_product, ' ', 16);
    memset(discovery_revision, ' ', 4);
    memcpy(discovery_vendor, "DISCOVR", 7);
    memcpy(discovery_product, "MOCK DISK", 9);
    memcpy(discovery_revision, "1.0", 3);
    discovery_vendor[8] = '\0';
    discovery_product[16] = '\0';
    discovery_revision[4] = '\0';
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
    assert(execute_calls >= SCSI_MAX_TARGETS);
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
    dev->target = 7;
    dev->lun = 0;
    dev->link = &link;
    strcpy(dev->vendor, "MOCK");
    strcpy(dev->product, "DISK");
    assert(scsi_device_register(dev) == 0);
    assert(scsi_device_lookup(0, 7, 0) == dev);

    scsi_unregister_link(&link);

    assert(detach_calls == 1);
    assert(scsi_device_lookup(0, 7, 0) == NULL);
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

static void test_execute_retries_unit_attention(void) {
    scsi_device_t dev;
    uint8_t cdb[6];
    scsi_request_t *req;

    reset_state();
    memset(&dev, 0, sizeof(dev));
    dev.link = &(scsi_link_t){
        .execute = mock_execute,
    };

    mock_status_code = SCSI_STATUS_CHECK_CONDITION;
    mock_execute_failures_before_success = 1;
    mock_sense_key_code = SCSI_SENSE_UNIT_ATTENTION;
    mock_sense_asc = 0x29;
    mock_sense_ascq = 0x00;

    req = scsi_request_alloc();
    assert(req != NULL);
    scsi_request_init(req, &dev);
    scsi_cdb_test_unit_ready(cdb);
    memcpy(req->cdb, cdb, sizeof(cdb));
    req->cdb_len = sizeof(cdb);

    assert(scsi_execute(req) == 0);
    assert(req->state == SCSI_REQ_STATE_COMPLETE);
    assert(req->retries == 1);
    assert(mock_request_sense_count == 1);
    assert(execute_calls == 3);
    scsi_request_free(req);
}

static void test_execute_stops_on_medium_error(void) {
    scsi_device_t dev;
    uint8_t cdb[6];
    scsi_request_t *req;

    reset_state();
    memset(&dev, 0, sizeof(dev));
    dev.link = &(scsi_link_t){
        .execute = mock_execute,
    };

    mock_status_code = SCSI_STATUS_CHECK_CONDITION;
    mock_execute_failures_before_success = 8;
    mock_sense_key_code = SCSI_SENSE_MEDIUM_ERROR;
    mock_sense_asc = 0x11;
    mock_sense_ascq = 0x00;

    req = scsi_request_alloc();
    assert(req != NULL);
    scsi_request_init(req, &dev);
    scsi_cdb_test_unit_ready(cdb);
    memcpy(req->cdb, cdb, sizeof(cdb));
    req->cdb_len = sizeof(cdb);

    assert(scsi_execute(req) < 0);
    assert(req->state == SCSI_REQ_STATE_ERROR);
    assert(req->retries == 0);
    assert(mock_request_sense_count == 1);
    assert(execute_calls == 2);
    scsi_request_free(req);
}

static void test_execute_retries_busy(void) {
    scsi_device_t dev;
    uint8_t cdb[6];
    scsi_request_t *req;

    reset_state();
    memset(&dev, 0, sizeof(dev));
    dev.link = &(scsi_link_t){
        .execute = mock_execute,
    };

    mock_status_code = SCSI_STATUS_BUSY;
    mock_execute_failures_before_success = 1;

    req = scsi_request_alloc();
    assert(req != NULL);
    scsi_request_init(req, &dev);
    scsi_cdb_test_unit_ready(cdb);
    memcpy(req->cdb, cdb, sizeof(cdb));
    req->cdb_len = sizeof(cdb);

    assert(scsi_execute(req) == 0);
    assert(req->state == SCSI_REQ_STATE_COMPLETE);
    assert(req->retries == 1);
    assert(execute_calls == 2);
    scsi_request_free(req);
}

static void test_execute_retries_not_ready(void) {
    scsi_device_t dev;
    uint8_t cdb[6];
    scsi_request_t *req;

    reset_state();
    memset(&dev, 0, sizeof(dev));
    dev.link = &(scsi_link_t){
        .execute = mock_execute,
    };

    mock_status_code = SCSI_STATUS_CHECK_CONDITION;
    mock_execute_failures_before_success = 1;
    mock_sense_key_code = SCSI_SENSE_NOT_READY;
    mock_sense_asc = 0x04;
    mock_sense_ascq = 0x01;

    req = scsi_request_alloc();
    assert(req != NULL);
    scsi_request_init(req, &dev);
    scsi_cdb_test_unit_ready(cdb);
    memcpy(req->cdb, cdb, sizeof(cdb));
    req->cdb_len = sizeof(cdb);

    assert(scsi_execute(req) == 0);
    assert(req->state == SCSI_REQ_STATE_COMPLETE);
    assert(req->retries == 1);
    assert(mock_request_sense_count == 1);
    assert(execute_calls == 3);
    scsi_request_free(req);
}

static void test_abort_request_preserves_tail(void) {
    scsi_device_t dev;
    scsi_request_t *req1;
    scsi_request_t *req2;

    reset_state();
    memset(&dev, 0, sizeof(dev));
    dev.max_queue_depth = 2;

    req1 = scsi_request_alloc();
    req2 = scsi_request_alloc();
    assert(req1 != NULL && req2 != NULL);
    scsi_request_init(req1, &dev);
    scsi_request_init(req2, &dev);
    req1->state = SCSI_REQ_STATE_PENDING;
    req2->state = SCSI_REQ_STATE_PENDING;
    req2->callback = record_callback;
    req1->next = req2;
    req2->next = NULL;
    dev.queue_head = req1;
    dev.queue_tail = req2;
    dev.queue_depth = 2;

    assert(scsi_abort_request(req2) == 0);
    assert(dev.queue_head == req1);
    assert(dev.queue_tail == req1);
    assert(dev.queue_depth == 1);
    assert(callback_calls == 1);
    assert(callback_last_state == SCSI_REQ_STATE_ERROR);

    scsi_request_free(req1);
    scsi_request_free(req2);
}

static void test_complete_request_runs_next_queued_request(void) {
    scsi_device_t dev;
    scsi_request_t *done_req;
    scsi_request_t *queued_req;

    reset_state();
    memset(&dev, 0, sizeof(dev));
    dev.link = &(scsi_link_t){
        .execute = mock_execute,
    };
    dev.max_queue_depth = 1;

    done_req = scsi_request_alloc();
    queued_req = scsi_request_alloc();
    assert(done_req != NULL && queued_req != NULL);
    scsi_request_init(done_req, &dev);
    scsi_request_init(queued_req, &dev);
    done_req->callback = record_callback;
    queued_req->state = SCSI_REQ_STATE_PENDING;
    queued_req->cdb[0] = SCSI_CMD_TEST_UNIT_READY;
    queued_req->cdb_len = 6;
    dev.queue_head = queued_req;
    dev.queue_tail = queued_req;
    dev.queue_depth = 1;

    scsi_complete_request(done_req, 0);
    assert(callback_calls == 1);
    assert(callback_last_error == 0);
    assert(dev.queue_depth == 0);
    assert(dev.queue_head == NULL);
    assert(dev.queue_tail == NULL);
    assert(execute_calls == 1);
    assert(queued_req->state == SCSI_REQ_STATE_COMPLETE);

    scsi_request_free(done_req);
    scsi_request_free(queued_req);
}

static void test_queue_request_respects_max_queue_depth(void) {
    scsi_device_t dev;
    scsi_request_t *queued_req;
    scsi_request_t *rejected_req;

    reset_state();
    memset(&dev, 0, sizeof(dev));
    dev.link = &(scsi_link_t){
        .execute = mock_execute,
    };
    dev.max_queue_depth = 1;

    queued_req = scsi_request_alloc();
    rejected_req = scsi_request_alloc();
    assert(queued_req != NULL && rejected_req != NULL);
    scsi_request_init(queued_req, &dev);
    scsi_request_init(rejected_req, &dev);
    queued_req->state = SCSI_REQ_STATE_PENDING;
    rejected_req->callback = record_callback;
    dev.queue_head = queued_req;
    dev.queue_tail = queued_req;
    dev.queue_depth = 1;

    assert(scsi_queue_request(rejected_req) < 0);
    assert(dev.queue_head == queued_req);
    assert(dev.queue_tail == queued_req);
    assert(dev.queue_depth == 1);
    assert(callback_calls == 1);
    assert(callback_last_error == -1);
    assert(callback_last_state == SCSI_REQ_STATE_ERROR);
    assert(rejected_req->state == SCSI_REQ_STATE_ERROR);
    assert(rejected_req->next == NULL);

    scsi_request_free(queued_req);
    scsi_request_free(rejected_req);
}

static void test_scan_bus_falls_back_to_sequential_luns(void) {
    scsi_link_t link;
    scsi_device_t *lun0;
    scsi_device_t *lun2;

    reset_state();
    memset(&link, 0, sizeof(link));
    strcpy(link.name, "disc0");
    link.execute = discovery_execute;
    link.bus_id = 1;
    link.max_targets = 1;
    link.max_luns = 4;

    discovery_report_luns_fail = 1;
    discovery_present_luns_mask = (1U << 0) | (1U << 2);

    assert(scsi_scan_bus(&link, 1) == 2);

    lun0 = scsi_device_lookup(1, 0, 0);
    lun2 = scsi_device_lookup(1, 0, 2);
    assert(lun0 != NULL);
    assert(lun2 != NULL);
    assert(strcmp(lun0->vendor, "DISCOVR") == 0);
    assert(strcmp(lun0->product, "MOCK DISK") == 0);
    assert(strcmp(lun0->revision, "1.0") == 0);
    assert(lun0->capacity == 256ULL);
    assert(lun0->sector_size == 512);
    assert(lun0->device_num == 0);
    assert(lun2->device_num == 1);
    assert(auto_attach_calls == 2);
}

int main(void) {
    test_register_link_sets_defaults_and_scans();
    test_unregister_link_detaches_registered_devices();
    test_standard_wrappers_and_16byte_helpers();
    test_execute_retries_unit_attention();
    test_execute_stops_on_medium_error();
    test_execute_retries_busy();
    test_execute_retries_not_ready();
    test_abort_request_preserves_tail();
    test_complete_request_runs_next_queued_request();
    test_queue_request_respects_max_queue_depth();
    test_scan_bus_falls_back_to_sequential_luns();
    puts("host_test_scsi_core: PASS");
    return 0;
}
