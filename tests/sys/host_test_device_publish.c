#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <kern/device.h>
#include <kern/bus.h>
#include <vfs/vfs.h>
#include <sys/proc.h>
#include <sys/tty.h>

void *kmalloc(size_t size) { return calloc(1, size); }
void *kzalloc(size_t size) { return calloc(1, size); }
void kfree(void *ptr, size_t size) { (void)size; free(ptr); }
void kmem_get_stats(uint64_t *allocs, uint64_t *frees, uint64_t *bytes) {
    if (allocs) *allocs = 0;
    if (frees) *frees = 0;
    if (bytes) *bytes = 0;
}

void spinlock_init(spinlock_t *lock, const char *name) { memset(lock, 0, sizeof(*lock)); lock->name = name; }
void spinlock_acquire(spinlock_t *lock) { (void)lock; }
bool spinlock_try_acquire(spinlock_t *lock) { (void)lock; return true; }
void spinlock_release(spinlock_t *lock) { (void)lock; }
bool spinlock_is_held(spinlock_t *lock) { (void)lock; return false; }

process_t *current_process = NULL;
struct fs_node *console_get_node(void) { return NULL; }
void vfs_register_filesystem(struct filesystem *fs) { (void)fs; }
int tty_read(struct tty *tty, char *buf, int len) { (void)tty; (void)buf; (void)len; return 0; }
int tty_write(struct tty *tty, const char *buf, int len) { (void)tty; (void)buf; (void)len; return 0; }
int tty_ioctl(struct tty *tty, uint32_t cmd, unsigned long arg) { (void)tty; (void)cmd; (void)arg; return 0; }
int kprintf(const char *fmt, ...) { (void)fmt; return 0; }
void kobject_uevent(const char *action, const char *subsystem, const char *name) {
    (void)action; (void)subsystem; (void)name;
}

static struct bus_type *registered_buses;
int bus_register_type(struct bus_type *bus) {
    bus->next_registered = registered_buses;
    registered_buses = bus;
    return 0;
}
struct bus_type *bus_first(void) { return registered_buses; }
struct bus_type *bus_next(struct bus_type *bus) { return bus ? bus->next_registered : NULL; }
struct driver *bus_match_device(struct bus_type *bus, struct device *dev) { (void)bus; (void)dev; return NULL; }
int bus_id_match(const struct device_id *id, struct device *dev) { (void)id; (void)dev; return 0; }
int bus_compatible_match(const char *compat, struct device *dev) { (void)compat; (void)dev; return 0; }
size_t bus_dump_tree(char *buf, size_t size) { (void)buf; (void)size; return 0; }
int driver_attach(struct driver *drv, struct device *dev) { (void)drv; (void)dev; return 0; }
int driver_detach(struct device *dev) { (void)dev; return 0; }

#include "../../sys/fs/devfs.c"
#include "../../sys/kern/device.c"

int main(void) {
    struct bus_type bus;
    struct device *dev;
    fs_node_t *node;
    devfs_entry_t *storage;
    devfs_entry_t *disk;
    devfs_entry_t *by_id;
    devfs_entry_t *alias;

    memset(&bus, 0, sizeof(bus));
    spinlock_init(&bus.lock, "publish-bus");
    bus.name = "publish-bus";

    devfs_init();

    dev = device_create("disk0", NULL);
    assert(dev != NULL);
    strcpy(dev->serial, "disk-serial");

    node = kmalloc(sizeof(*node));
    assert(node != NULL);
    memset(node, 0, sizeof(*node));
    node->flags = FS_BLOCKDEVICE;
    node->mask = 0600;
    node->uid = 0;
    node->gid = 0;

    assert(device_publish(dev, node, "storage/disk0") == 0);
    assert(device_register(dev, &bus) == 0);

    storage = devfs_find_child(root_entry, "storage");
    assert(storage != NULL);
    disk = devfs_find_child(storage, "disk0");
    assert(disk != NULL);
    assert(disk->node == node);

    by_id = devfs_find_child(root_entry, "by-id");
    assert(by_id != NULL);
    alias = devfs_find_child(by_id, "disk-serial");
    assert(alias != NULL);

    assert(device_unregister(dev) == 0);
    assert(devfs_find_child(storage, "disk0") == NULL);
    assert(devfs_find_child(by_id, "disk-serial") == NULL);

    puts("host_test_device_publish: PASS");
    return 0;
}
