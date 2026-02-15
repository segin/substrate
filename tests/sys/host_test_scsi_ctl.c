#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <assert.h>

/* Mock kernel environment */
#define HOST_TEST

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

int scsi_execute_sync(scsi_device_t *dev, uint8_t *cdb, uint8_t cdb_len,
                      void *data, uint32_t data_len, uint16_t flags,
                      uint32_t timeout_ms) {
    (void)dev; (void)cdb; (void)cdb_len; (void)timeout_ms;
    last_execute_data = data;
    last_execute_data_len = data_len;
    last_execute_flags = flags;
    return 0;
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
    cmd.direction = 1; /* READ */
    cmd.data_len = 100;
    char data_buf[100];
    cmd.data = data_buf;

    int ret = sg_ioctl(&node, SCSI_IOCTL_SEND_CMD, &cmd);
    assert(ret == 0);
    assert(last_execute_data_len == 100);
    assert(last_execute_data != data_buf); /* Must use kernel buffer! */
    assert(last_execute_flags & SCSI_REQ_READ);

    /* Test WRITE */
    cmd.direction = 2; /* WRITE */
    ret = sg_ioctl(&node, SCSI_IOCTL_SEND_CMD, &cmd);
    assert(ret == 0);
    assert(last_execute_flags & SCSI_REQ_WRITE);

    /* Test large data_len limit */
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

void test_get_count() {
    printf("Testing SCSI_IOCTL_GET_COUNT...\n");
    scsi_bus_node_t bn;
    memset(&bn, 0, sizeof(bn));
    scsi_link_t link;
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
    test_send_cmd();
    test_get_count();
    printf("All scsi_ctl tests passed!\n");
    return 0;
}
