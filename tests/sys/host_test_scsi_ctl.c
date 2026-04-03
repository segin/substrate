#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <assert.h>

/* Mock kernel environment */
/* HOST_TEST must be defined on command line or before includes */

/* Mock errno values if not available */
#ifndef EINVAL
#define EINVAL 22
#endif
#ifndef EFAULT
#define EFAULT 14
#endif
#ifndef ENOMEM
#define ENOMEM 12
#endif

/* Mock VFS types */

#include "../../sys/vfs/vfs.h"
#include "../../sys/drivers/storage/scsi/scsi.h"
#include <sys/proc.h>

/* Forward declarations for scsi_ctl.c mocks */
void kprint(const char *str) { printf("[K] %s", str); }
void devfs_register_device(fs_node_t *node) { (void)node; }
void scsi_dev_init(void) {}
int scsi_dev_attach(scsi_device_t *dev) { (void)dev; return 0; }
int scsi_scan_bus(scsi_link_t *link, uint8_t bus) { (void)link; (void)bus; return 0; }
scsi_device_t *scsi_device_lookup(uint8_t bus, uint8_t target, uint16_t lun) {
    (void)bus; (void)target; (void)lun;
    return NULL;
}

/* Mock memory allocator */
void *kmalloc(size_t size) { return malloc(size); }
void kfree(void *ptr, size_t size) { (void)size; free(ptr); }
void *kzalloc(size_t size) { return calloc(1, size); }

/* Mock copyin/copyout */
static int copy_fail = 0;
int copyin(const void *src, void *dst, size_t size) {
    if (copy_fail) return -1;
    memcpy(dst, src, size);
    return 0;
}

int copyout(const void *src, void *dst, size_t size) {
    if (copy_fail) return -1;
    memcpy(dst, src, size);
    return 0;
}

/* Mock scsi_execute_sync */
static int last_execute_flags = 0;
static uint32_t last_execute_data_len = 0;
static void *last_execute_data = NULL;
static int mock_execute_ret = 0;
static uint8_t mock_execute_status = SCSI_STATUS_GOOD;
static uint32_t mock_execute_data_xfer = 0;
static uint8_t mock_execute_sense_len = 0;
static uint8_t mock_execute_sense[18];
static scsi_request_t mock_request;
static process_t mock_process;
process_t *current_process = &mock_process;

scsi_request_t *scsi_request_alloc(void) {
    memset(&mock_request, 0, sizeof(mock_request));
    mock_request.state = SCSI_REQ_STATE_PENDING;
    return &mock_request;
}

void scsi_request_free(scsi_request_t *req) {
    assert(req == &mock_request);
}

void scsi_request_init(scsi_request_t *req, scsi_device_t *dev) {
    memset(req, 0, sizeof(*req));
    req->device = dev;
    req->state = SCSI_REQ_STATE_PENDING;
}

int scsi_execute(scsi_request_t *req) {
    last_execute_data = req->data;
    last_execute_data_len = req->data_len;
    last_execute_flags = req->flags;
    req->status = mock_execute_status;
    req->data_xfer = mock_execute_data_xfer ? mock_execute_data_xfer : req->data_len;
    req->sense_len = mock_execute_sense_len;
    if (mock_execute_sense_len != 0) {
        memcpy(req->sense, mock_execute_sense, mock_execute_sense_len);
    }
    return mock_execute_ret;
}

/* Include the source file */
#include "../../sys/drivers/storage/scsi/scsi_ctl.c"

/* Test cases */
void test_get_info() {
    printf("Testing SCSI_IOCTL_GET_INFO...\n");
    scsi_device_t dev;
    memset(&dev, 0, sizeof(dev));
    dev.bus = 1;
    dev.target = 2;
    dev.lun = 3;
    dev.type = 0x05;
    strcpy(dev.vendor, "VENDOR");
    strcpy(dev.product, "PRODUCT");
    strcpy(dev.revision, "1.0");
    dev.capacity = 1000;
    dev.sector_size = 512;

    scsi_generic_node_t sg;
    memset(&sg, 0, sizeof(sg));
    sg.dev = &dev;

    fs_node_t node;
    memset(&node, 0, sizeof(node));
    node.impl = (uintptr_t)&sg;

    scsi_ioctl_info_t info;
    memset(&info, 0, sizeof(info));

    int ret = sg_ioctl(&node, SCSI_IOCTL_GET_INFO, &info);
    assert(ret == 0);
    assert(info.bus == 1);
    assert(info.target == 2);
    assert(info.lun == 3);
    assert(info.type == 0x05);
    assert(strcmp(info.vendor, "VENDOR") == 0);
    assert(strcmp(info.product, "PRODUCT") == 0);
    assert(strcmp(info.revision, "1.0") == 0);
    assert(info.capacity == 1000);
    assert(info.sector_size == 512);

    /* Test copyout failure */
    copy_fail = 1;
    ret = sg_ioctl(&node, SCSI_IOCTL_GET_INFO, &info);
    assert(ret == -EFAULT);
    copy_fail = 0;

    printf("SCSI_IOCTL_GET_INFO pass\n");
}

void test_send_cmd() {
    printf("Testing SCSI_IOCTL_SEND_CMD...\n");
    scsi_device_t dev;
    memset(&dev, 0, sizeof(dev));

    scsi_generic_node_t sg;
    memset(&sg, 0, sizeof(sg));
    sg.dev = &dev;

    fs_node_t node;
    memset(&node, 0, sizeof(node));
    node.impl = (uintptr_t)&sg;

    scsi_ioctl_cmd_t cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.cdb_len = 6;
    cmd.cdb[0] = SCSI_CMD_INQUIRY;
    cmd.direction = 1; /* READ */
    cmd.data_len = 100;
    char data_buf[100];
    cmd.data = data_buf;
    current_process->euid = 0;
    mock_execute_ret = 0;
    mock_execute_status = SCSI_STATUS_GOOD;
    mock_execute_data_xfer = 100;
    mock_execute_sense_len = 0;

    int ret = sg_ioctl(&node, SCSI_IOCTL_SEND_CMD, &cmd);
    assert(ret == 0);
    assert(last_execute_data_len == 100);
    assert(last_execute_data != data_buf); /* Must use kernel buffer! */
    assert(last_execute_flags & SCSI_REQ_READ);
    assert(cmd.status == SCSI_STATUS_GOOD);
    assert(cmd.error == 0);
    assert(cmd.data_xfer == 100);
    assert(cmd.sense_len == 0);

    /* Test WRITE */
    cmd.direction = 2; /* WRITE */
    mock_execute_data_xfer = 100;
    ret = sg_ioctl(&node, SCSI_IOCTL_SEND_CMD, &cmd);
    assert(ret == 0);
    assert(last_execute_flags & SCSI_REQ_WRITE);

    /* Test CHECK CONDITION propagation */
    cmd.direction = 1;
    mock_execute_ret = -5;
    mock_execute_status = SCSI_STATUS_CHECK_CONDITION;
    mock_execute_data_xfer = 0;
    mock_execute_sense_len = 4;
    mock_execute_sense[0] = 0x70;
    mock_execute_sense[1] = 0x00;
    mock_execute_sense[2] = 0x05;
    mock_execute_sense[3] = 0x24;
    ret = sg_ioctl(&node, SCSI_IOCTL_SEND_CMD, &cmd);
    assert(ret == -5);
    assert(cmd.error == -5);
    assert(cmd.status == SCSI_STATUS_CHECK_CONDITION);
    assert(cmd.sense_len == 4);
    assert(cmd.sense[0] == 0x70);
    assert(cmd.sense[2] == 0x05);

    /* Test large data_len limit */
    mock_execute_ret = 0;
    mock_execute_status = SCSI_STATUS_GOOD;
    mock_execute_data_xfer = 100;
    mock_execute_sense_len = 0;
    cmd.data_len = 70000;
    ret = sg_ioctl(&node, SCSI_IOCTL_SEND_CMD, &cmd);
    assert(ret == -EINVAL);

    /* Test copyin failure */
    cmd.data_len = 100;
    copy_fail = 1;
    ret = sg_ioctl(&node, SCSI_IOCTL_SEND_CMD, &cmd);
    assert(ret == -EFAULT);
    copy_fail = 0;

    printf("SCSI_IOCTL_SEND_CMD pass\n");
}

void test_get_idlun() {
    printf("Testing SCSI_IOCTL_GET_IDLUN...\n");
    scsi_device_t dev;
    scsi_generic_node_t sg;
    fs_node_t node;
    uint32_t packed = 0;
    int ret;

    memset(&dev, 0, sizeof(dev));
    dev.bus = 1;
    dev.target = 2;
    dev.lun = 0x3456;

    memset(&sg, 0, sizeof(sg));
    sg.dev = &dev;

    memset(&node, 0, sizeof(node));
    node.impl = (uintptr_t)&sg;

    ret = sg_ioctl(&node, SCSI_IOCTL_GET_IDLUN, &packed);
    assert(ret == 0);
    assert(packed == 0x01023456U);

    copy_fail = 1;
    ret = sg_ioctl(&node, SCSI_IOCTL_GET_IDLUN, &packed);
    assert(ret == -EFAULT);
    copy_fail = 0;

    printf("SCSI_IOCTL_GET_IDLUN pass\n");
}

void test_send_cmd_permissions() {
    printf("Testing SCSI_IOCTL_SEND_CMD permissions...\n");
    scsi_device_t dev;
    scsi_generic_node_t sg;
    fs_node_t node;
    scsi_ioctl_cmd_t cmd;
    char data_buf[16];
    int ret;

    memset(&dev, 0, sizeof(dev));
    memset(&sg, 0, sizeof(sg));
    memset(&node, 0, sizeof(node));
    memset(&cmd, 0, sizeof(cmd));

    sg.dev = &dev;
    node.impl = (uintptr_t)&sg;
    cmd.cdb_len = 6;
    cmd.cdb[0] = SCSI_CMD_INQUIRY;
    cmd.direction = 1;
    cmd.data_len = sizeof(data_buf);
    cmd.data = data_buf;

    current_process->euid = 1000;
    ret = sg_ioctl(&node, SCSI_IOCTL_SEND_CMD, &cmd);
    assert(ret == -EPERM);

    current_process->euid = 0;
    ret = sg_ioctl(&node, SCSI_IOCTL_SEND_CMD, &cmd);
    assert(ret == 0);

    printf("SCSI_IOCTL_SEND_CMD permissions pass\n");
}

void test_send_cmd_cdb_overflow() {
    printf("Testing SCSI_IOCTL_SEND_CMD overflow check...\n");
    scsi_device_t dev;
    memset(&dev, 0, sizeof(dev));

    scsi_generic_node_t sg;
    memset(&sg, 0, sizeof(sg));
    sg.dev = &dev;

    fs_node_t node;
    memset(&node, 0, sizeof(node));
    node.impl = (uintptr_t)&sg;

    scsi_ioctl_cmd_t cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.direction = 1; /* READ */
    cmd.data_len = 100;
    char data_buf[100];
    cmd.data = data_buf;
    cmd.cdb_len = 20; /* Larger than 16 */

    int ret = sg_ioctl(&node, SCSI_IOCTL_SEND_CMD, &cmd);

    /* Before fix, this returns 0 (success) because mock execute returns 0.
       We assert that it should return -EINVAL. */
    if (ret == 0) {
        printf("FAILED: Expected -EINVAL for cdb_len > 16, got 0\n");
        exit(1);
    }
    assert(ret == -EINVAL);

    printf("SCSI_IOCTL_SEND_CMD overflow check pass\n");
}

void test_send_cmd_invalid_shape() {
    printf("Testing SCSI_IOCTL_SEND_CMD invalid shape checks...\n");
    scsi_device_t dev;
    scsi_generic_node_t sg;
    fs_node_t node;
    scsi_ioctl_cmd_t cmd;
    int ret;

    memset(&dev, 0, sizeof(dev));
    memset(&sg, 0, sizeof(sg));
    memset(&node, 0, sizeof(node));
    memset(&cmd, 0, sizeof(cmd));

    sg.dev = &dev;
    node.impl = (uintptr_t)&sg;
    current_process->euid = 0;

    cmd.cdb_len = 0;
    ret = sg_ioctl(&node, SCSI_IOCTL_SEND_CMD, &cmd);
    assert(ret == -EINVAL);

    cmd.cdb_len = 6;
    cmd.direction = 3;
    ret = sg_ioctl(&node, SCSI_IOCTL_SEND_CMD, &cmd);
    assert(ret == -EINVAL);

    cmd.direction = 1;
    cmd.data_len = 16;
    cmd.data = NULL;
    ret = sg_ioctl(&node, SCSI_IOCTL_SEND_CMD, &cmd);
    assert(ret == -EINVAL);

    printf("SCSI_IOCTL_SEND_CMD invalid shape checks pass\n");
}

void test_get_count() {
    printf("Testing SCSI_IOCTL_GET_COUNT...\n");
    scsi_bus_node_t bn;
    memset(&bn, 0, sizeof(bn));
    scsi_link_t link;
    memset(&link, 0, sizeof(link));
    link.max_targets = 16;
    link.max_luns = 8;
    bn.link = &link;
    bn.bus_id = 0;

    fs_node_t node;
    memset(&node, 0, sizeof(node));
    node.impl = (uintptr_t)&bn;

    int count = 0;
    int ret = bus_ioctl(&node, SCSI_IOCTL_GET_COUNT, &count);
    assert(ret == 0);
    /* Device lookup mock always returns NULL, so count should be 0 */
    assert(count == 0);

    /* Test copyout failure */
    copy_fail = 1;
    ret = bus_ioctl(&node, SCSI_IOCTL_GET_COUNT, &count);
    assert(ret == -EFAULT);
    copy_fail = 0;

    printf("SCSI_IOCTL_GET_COUNT pass\n");
}

int main() {
    printf("Running host-side scsi_ctl tests...\n");
    test_get_info();
    test_get_idlun();
    test_send_cmd();
    test_send_cmd_permissions();
    test_get_count();
    test_send_cmd_cdb_overflow();
    test_send_cmd_invalid_shape();
    printf("All scsi_ctl tests passed!\n");
    return 0;
}
