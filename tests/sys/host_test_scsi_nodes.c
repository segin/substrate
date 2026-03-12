#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/proc.h>
#include <sys/tty.h>
#include <vfs/vfs.h>
#include <drivers/storage/scsi/scsi.h>

void *kmalloc(size_t size) { return calloc(1, size); }
void *kzalloc(size_t size) { return calloc(1, size); }
void kfree(void *ptr, size_t size) { (void)size; free(ptr); }
void kmem_get_stats(uint64_t *allocs, uint64_t *frees, uint64_t *bytes) {
    if (allocs) *allocs = 0;
    if (frees) *frees = 0;
    if (bytes) *bytes = 0;
}

process_t *current_process = NULL;
struct fs_node *console_get_node(void) { return NULL; }
void vfs_register_filesystem(struct filesystem *fs) { (void)fs; }
int tty_read(struct tty *tty, char *buf, int len) { (void)tty; (void)buf; (void)len; return 0; }
int tty_write(struct tty *tty, const char *buf, int len) { (void)tty; (void)buf; (void)len; return 0; }
int tty_ioctl(struct tty *tty, uint32_t cmd, unsigned long arg) { (void)tty; (void)cmd; (void)arg; return 0; }
int kprintf(const char *fmt, ...) { (void)fmt; return 0; }
void kprint(const char *str) { (void)str; }

int copyin(const void *src, void *dst, size_t size) {
    memcpy(dst, src, size);
    return 0;
}

int copyout(const void *src, void *dst, size_t size) {
    memcpy(dst, src, size);
    return 0;
}

int scsi_execute_sync(scsi_device_t *dev, uint8_t *cdb, uint8_t cdb_len,
                      void *data, uint32_t data_len, uint16_t flags,
                      uint32_t timeout_ms) {
    (void)dev; (void)cdb; (void)cdb_len; (void)data;
    (void)data_len; (void)flags; (void)timeout_ms;
    return 0;
}

static int scsi_dev_attach_calls;
static scsi_device_t *last_attached_dev;

int scsi_dev_attach(scsi_device_t *dev) {
    scsi_dev_attach_calls++;
    last_attached_dev = dev;
    return 0;
}

void scsi_dev_init(void) {}

int scsi_scan_bus(scsi_link_t *link, uint8_t bus) {
    (void)link;
    (void)bus;
    return 0;
}

scsi_device_t *scsi_device_lookup(uint8_t bus, uint8_t target, uint16_t lun) {
    (void)bus;
    (void)target;
    (void)lun;
    return NULL;
}

#include "../../sys/fs/devfs.c"
#include "../../sys/drivers/storage/scsi/scsi_ctl.c"

static void test_generic_node_goes_under_storage_scsi(void) {
    scsi_device_t dev;
    devfs_entry_t *storage;
    devfs_entry_t *scsi;
    devfs_entry_t *node;

    memset(&dev, 0, sizeof(dev));
    dev.bus = 2;
    dev.target = 3;
    dev.lun = 4;

    devfs_init();
    sg_list = NULL;
    sg_count = 0;

    assert(scsi_create_generic_node(&dev) == 0);

    storage = devfs_find_child(root_entry, "storage");
    assert(storage != NULL);
    scsi = devfs_find_child(storage, "scsi");
    assert(scsi != NULL);
    node = devfs_find_child(scsi, "2:3:4");
    assert(node != NULL);
    assert(node->node != NULL);
    assert((node->node->flags & 0x7) == FS_CHARDEVICE);
}

static void test_bus_node_goes_under_storage_scsi(void) {
    scsi_link_t link;
    devfs_entry_t *storage;
    devfs_entry_t *scsi;
    devfs_entry_t *node;

    memset(&link, 0, sizeof(link));
    strcpy(link.name, "mockbus");

    devfs_init();
    bus_count = 0;
    memset(bus_nodes, 0, sizeof(bus_nodes));

    assert(scsi_create_bus_node(&link, 5) == 0);

    storage = devfs_find_child(root_entry, "storage");
    assert(storage != NULL);
    scsi = devfs_find_child(storage, "scsi");
    assert(scsi != NULL);
    node = devfs_find_child(scsi, "5");
    assert(node != NULL);
    assert(node->node != NULL);
    assert((node->node->flags & 0x7) == FS_CHARDEVICE);
}

static void test_auto_attach_creates_generic_node_and_calls_block_attach(void) {
    scsi_device_t dev;
    devfs_entry_t *storage;
    devfs_entry_t *scsi;
    devfs_entry_t *node;

    memset(&dev, 0, sizeof(dev));
    dev.bus = 1;
    dev.target = 0;
    dev.lun = 0;

    devfs_init();
    sg_list = NULL;
    sg_count = 0;
    scsi_dev_attach_calls = 0;
    last_attached_dev = NULL;

    scsi_auto_attach(&dev);

    storage = devfs_find_child(root_entry, "storage");
    assert(storage != NULL);
    scsi = devfs_find_child(storage, "scsi");
    assert(scsi != NULL);
    node = devfs_find_child(scsi, "1:0:0");
    assert(node != NULL);
    assert(scsi_dev_attach_calls == 1);
    assert(last_attached_dev == &dev);
}

int main(void) {
    test_generic_node_goes_under_storage_scsi();
    test_bus_node_goes_under_storage_scsi();
    test_auto_attach_creates_generic_node_and_calls_block_attach();
    puts("host_test_scsi_nodes: PASS");
    return 0;
}
