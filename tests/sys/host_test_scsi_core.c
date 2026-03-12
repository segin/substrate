#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>

#include "../../sys/drivers/storage/scsi/scsi.h"

static int execute_calls;
static int detach_calls;
static int create_bus_node_calls;
static uint8_t last_bus_id;

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
    req->status = SCSI_STATUS_BUSY;
    return -1;
}

#include "../../sys/drivers/storage/scsi/scsi.c"

static void reset_state(void) {
    execute_calls = 0;
    detach_calls = 0;
    create_bus_node_calls = 0;
    last_bus_id = 0xff;
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

int main(void) {
    test_register_link_sets_defaults_and_scans();
    test_unregister_link_detaches_registered_devices();
    puts("host_test_scsi_core: PASS");
    return 0;
}
