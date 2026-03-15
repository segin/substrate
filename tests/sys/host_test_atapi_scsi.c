#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define _IO_H
static uint8_t mock_status_port_value = 0x50;
static uint8_t inb(uint16_t port) {
    (void)port;
    return mock_status_port_value;
}

#include <drivers/storage/scsi/scsi.h>
#include <drivers/storage/ide/ide.h>

static int register_link_calls;
static scsi_link_t *registered_link;
static int identify_calls;
static int packet_calls;
static uint8_t last_packet[12];
static uint8_t last_packet_len;
static uint8_t last_packet_channel;
static uint8_t last_packet_drive;
static uint32_t last_packet_data_len;
static int last_packet_write;
static int ctrl_write_count;
static struct {
    uint8_t channel;
    uint8_t value;
} ctrl_writes[8];
static int present_map[2][2];

void kprint(const char *str) { (void)str; }

static void fill_model(uint16_t *buf, const char *model) {
    char padded[40];
    size_t len = strlen(model);
    memset(padded, ' ', sizeof(padded));
    if (len > sizeof(padded)) len = sizeof(padded);
    memcpy(padded, model, len);
    for (int i = 0; i < 20; i++) {
        buf[27 + i] = ((uint16_t)(uint8_t)padded[i * 2] << 8) |
                      (uint8_t)padded[i * 2 + 1];
    }
}

int ide_identify_atapi(uint16_t bus, uint8_t drive, void *buffer) {
    uint16_t *buf = buffer;
    int channel = (bus == ATA_PRIMARY_IO) ? 0 : 1;

    identify_calls++;
    if (channel < 0 || channel > 1 || drive > 1) return -1;
    if (!present_map[channel][drive]) return -1;

    memset(buf, 0, 512);
    buf[0] = (uint16_t)(SCSI_TYPE_ROM << 8);
    fill_model(buf, channel == 0 ? (drive == 0 ? "PRIMARY MASTER CD" : "PRIMARY SLAVE CD")
                                 : (drive == 0 ? "SECONDARY MASTER CD" : "SECONDARY SLAVE CD"));
    return 0;
}

int ide_atapi_packet(uint8_t channel, uint8_t drive,
                     const uint8_t *packet, uint8_t packet_len,
                     void *data, uint32_t data_len, int write) {
    (void)data;
    packet_calls++;
    last_packet_channel = channel;
    last_packet_drive = drive;
    last_packet_len = packet_len;
    last_packet_data_len = data_len;
    last_packet_write = write;
    memset(last_packet, 0, sizeof(last_packet));
    memcpy(last_packet, packet, packet_len > sizeof(last_packet) ? sizeof(last_packet) : packet_len);
    return 0;
}

void ide_write_ctrl(uint8_t channel, uint8_t value) {
    if (ctrl_write_count < (int)(sizeof(ctrl_writes) / sizeof(ctrl_writes[0]))) {
        ctrl_writes[ctrl_write_count].channel = channel;
        ctrl_writes[ctrl_write_count].value = value;
    }
    ctrl_write_count++;
}

int scsi_register_link(scsi_link_t *link) {
    register_link_calls++;
    registered_link = link;
    return 0;
}

#include "../../sys/drivers/storage/scsi/atapi_scsi.c"

static void reset_state(void) {
    register_link_calls = 0;
    registered_link = NULL;
    identify_calls = 0;
    packet_calls = 0;
    last_packet_len = 0;
    last_packet_channel = 0xff;
    last_packet_drive = 0xff;
    last_packet_data_len = 0;
    last_packet_write = -1;
    ctrl_write_count = 0;
    memset(ctrl_writes, 0, sizeof(ctrl_writes));
    memset(last_packet, 0, sizeof(last_packet));
    memset(present_map, 0, sizeof(present_map));
    memset(&atapi_link, 0, sizeof(atapi_link));
    atapi_initialized = 0;
    mock_status_port_value = 0x50;
}

static void test_init_registers_discovered_devices(void) {
    reset_state();
    present_map[0][0] = 1;
    present_map[1][1] = 1;

    atapi_scsi_init();

    assert(atapi_initialized == 1);
    assert(register_link_calls == 1);
    assert(registered_link == &atapi_link.link);
    assert(strcmp(atapi_link.link.name, "atapi0") == 0);
    assert(atapi_link.link.bus_id == 0);
    assert(atapi_link.link.max_targets == 4);
    assert(atapi_link.link.max_luns == 1);
    assert(atapi_link.link.adapter_queue_depth == 1);
    assert(atapi_link.target_count == 2);
    assert(atapi_link.targets[0].present == 1);
    assert(atapi_link.targets[0].channel == 0);
    assert(atapi_link.targets[0].drive == 0);
    assert(atapi_link.targets[1].present == 0);
    assert(atapi_link.targets[3].present == 1);
    assert(atapi_link.targets[3].channel == 1);
    assert(atapi_link.targets[3].drive == 1);
}

static void test_execute_translates_to_packet_command(void) {
    scsi_device_t dev;
    scsi_request_t req;
    uint8_t data[2048];

    reset_state();
    present_map[1][1] = 1;
    atapi_scsi_init();

    memset(&dev, 0, sizeof(dev));
    dev.target = 3;
    dev.lun = 0;

    memset(&req, 0, sizeof(req));
    req.device = &dev;
    req.cdb_len = 16;
    req.flags = SCSI_REQ_READ;
    req.data = data;
    req.data_len = sizeof(data);
    for (int i = 0; i < 16; i++) req.cdb[i] = (uint8_t)(0x80 + i);

    assert(atapi_link.link.execute(&atapi_link.link, &req) == 0);
    assert(packet_calls == 1);
    assert(last_packet_channel == 1);
    assert(last_packet_drive == 1);
    assert(last_packet_len == 12);
    assert(last_packet_data_len == sizeof(data));
    assert(last_packet_write == 0);
    for (int i = 0; i < 12; i++) assert(last_packet[i] == (uint8_t)(0x80 + i));
    assert(req.status == SCSI_STATUS_GOOD);
    assert(req.data_xfer == sizeof(data));
}

static void test_reset_paths_follow_target_channel(void) {
    scsi_device_t dev;

    reset_state();
    present_map[1][1] = 1;
    atapi_scsi_init();

    memset(&dev, 0, sizeof(dev));
    dev.target = 3;
    dev.lun = 0;

    assert(atapi_link.link.reset_device(&atapi_link.link, &dev) == 0);
    assert(ctrl_write_count >= 2);
    assert(ctrl_writes[0].channel == 1 && ctrl_writes[0].value == ATA_CTRL_SRST);
    assert(ctrl_writes[1].channel == 1 && ctrl_writes[1].value == 0x00);

    ctrl_write_count = 0;
    memset(ctrl_writes, 0, sizeof(ctrl_writes));
    assert(atapi_link.link.reset_bus(&atapi_link.link) == 0);
    assert(ctrl_write_count >= 4);
    assert(ctrl_writes[0].channel == 0 && ctrl_writes[0].value == ATA_CTRL_SRST);
    assert(ctrl_writes[1].channel == 0 && ctrl_writes[1].value == 0x00);
    assert(ctrl_writes[2].channel == 1 && ctrl_writes[2].value == ATA_CTRL_SRST);
    assert(ctrl_writes[3].channel == 1 && ctrl_writes[3].value == 0x00);
}

static void test_invalid_target_rejected(void) {
    scsi_device_t dev;
    scsi_request_t req;

    reset_state();
    present_map[0][0] = 1;
    atapi_scsi_init();

    memset(&dev, 0, sizeof(dev));
    dev.target = 2;
    dev.lun = 0;

    memset(&req, 0, sizeof(req));
    req.device = &dev;
    req.cdb_len = 6;

    assert(atapi_link.link.execute(&atapi_link.link, &req) < 0);
    assert(req.status == SCSI_STATUS_CHECK_CONDITION);
    assert(packet_calls == 0);
}

int main(void) {
    test_init_registers_discovered_devices();
    test_execute_translates_to_packet_command();
    test_reset_paths_follow_target_channel();
    test_invalid_target_rejected();
    puts("host_test_atapi_scsi: PASS");
    return 0;
}
