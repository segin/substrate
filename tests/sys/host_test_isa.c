#include <assert.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <kern/device.h>
#include <kern/bus.h>

static uint8_t io_space[65536];
static uint8_t uart_scratch[4];
static struct bus_type *registered_buses;

void *kmalloc(size_t size) { return calloc(1, size); }
void kfree(void *ptr, size_t size) { (void)size; free(ptr); }

void spinlock_init(spinlock_t *lock, const char *name) {
    memset(lock, 0, sizeof(*lock));
    lock->name = name;
}
void spinlock_acquire(spinlock_t *lock) { (void)lock; }
bool spinlock_try_acquire(spinlock_t *lock) { (void)lock; return true; }
void spinlock_release(spinlock_t *lock) { (void)lock; }
bool spinlock_is_held(spinlock_t *lock) { (void)lock; return false; }

int bus_register_type(struct bus_type *bus) {
    bus->next_registered = registered_buses;
    registered_buses = bus;
    return 0;
}
struct bus_type *bus_first(void) { return registered_buses; }
struct bus_type *bus_next(struct bus_type *bus) { return bus ? bus->next_registered : NULL; }
size_t bus_dump_tree(char *buf, size_t size) { (void)buf; (void)size; return 0; }
struct driver *bus_match_device(struct bus_type *bus, struct device *dev) { (void)bus; (void)dev; return NULL; }
int bus_id_match(const struct device_id *id, struct device *dev) { (void)id; (void)dev; return 0; }
int bus_compatible_match(const char *compat, struct device *dev) { (void)compat; (void)dev; return 0; }

struct device *device_create(const char *name, struct device *parent) {
    struct device *dev = kmalloc(sizeof(*dev));
    if (!dev) return NULL;
    strncpy(dev->name, name, sizeof(dev->name) - 1);
    dev->parent = parent;
    spinlock_init(&dev->lock, "device");
    dev->ref_count = 1;
    return dev;
}
int device_register(struct device *dev, struct bus_type *bus) {
    dev->bus = bus;
    dev->bus_next = bus->devices_list;
    bus->devices_list = dev;
    return 0;
}
int device_unregister(struct device *dev) { (void)dev; return 0; }
void device_get(struct device *dev) { (void)dev; }
void device_put(struct device *dev) { free(dev); }
struct device *device_find_child(struct device *parent, const char *name) { (void)parent; (void)name; return NULL; }
int device_probe(struct device *dev) { (void)dev; return 0; }
void device_defer_probe(struct device *dev) { (void)dev; }
void device_retry_deferred(void) {}
int device_suspend(struct device *dev, pm_state_t state) { (void)dev; (void)state; return 0; }
int device_resume(struct device *dev) { (void)dev; return 0; }
void device_shutdown(struct device *dev) { (void)dev; }
int device_reset(struct device *dev) { (void)dev; return 0; }
int device_publish(struct device *dev, struct fs_node *node, const char *path) { (void)dev; (void)node; (void)path; return 0; }
void device_unpublish(struct device *dev) { (void)dev; }
int device_suspend_all(pm_state_t state) { (void)state; return 0; }
int device_resume_all(void) { return 0; }
void device_runtime_enable(struct device *dev, uint32_t idle_timeout) { (void)dev; (void)idle_timeout; }
int device_runtime_get(struct device *dev) { (void)dev; return 0; }
int device_runtime_put(struct device *dev, uint32_t now_ticks) { (void)dev; (void)now_ticks; return 0; }
int device_runtime_poll(uint32_t now_ticks) { (void)now_ticks; return 0; }

int kprintf(const char *fmt, ...) { (void)fmt; return 0; }

uint8_t isa_test_inb(uint16_t port) {
    if (port == 0x3FF) return uart_scratch[0];
    if (port == 0x2FF) return uart_scratch[1];
    if (port == 0x3EF) return uart_scratch[2];
    if (port == 0x2EF) return uart_scratch[3];
    return io_space[port];
}

void isa_test_outb(uint16_t port, uint8_t value) {
    if (port == 0x3FF) uart_scratch[0] = value;
    else if (port == 0x2FF) uart_scratch[1] = value;
    else if (port == 0x3EF) uart_scratch[2] = value;
    else if (port == 0x2EF) uart_scratch[3] = value;
    else io_space[port] = value;
}

#include "../../sys/kern/isa.c"

static void reset_fixture(void) {
    memset(io_space, 0xFF, sizeof(io_space));
    memset(uart_scratch, 0x00, sizeof(uart_scratch));
    registered_buses = NULL;
    isa_bus_type.devices_list = NULL;
    isa_bus_initialized = 0;
}

int main(void) {
    struct device *dev;
    char buf[256];

    reset_fixture();
    io_space[0x378] = 0x00;
    io_space[0x378 + 1] = 0x80;
    io_space[0x1F0] = 0x00;
    io_space[0x1F0 + 7] = 0x50;
    io_space[0x60] = 0x00;
    io_space[0x60 + 4] = 0x14;
    isa_init();

    assert(isa_port_alive(0x378));
    assert(!isa_port_alive(0x1234));

    isa_probe_legacy();
    dev = isa_first_device();
    assert(dev != NULL);
    assert(isa_find_device_by_name("serial0") != NULL);
    assert(isa_find_device_by_name("parallel0") != NULL);
    assert(isa_find_device_by_name("ide-primary") != NULL);
    assert(isa_find_device_by_name("ps2") != NULL);

    memset(buf, 0, sizeof(buf));
    assert(isa_dump_devices(buf, sizeof(buf)) > 0);
    assert(strstr(buf, "serial0") != NULL);
    assert(strstr(buf, "parallel0") != NULL);

    puts("host_test_isa: PASS");
    return 0;
}
