#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <kern/bus.h>
#include <kern/device.h>
#include <kern/driver.h>

static int probe_calls;
static int attach_calls;

void spinlock_init(spinlock_t *lock, const char *name) {
    memset(lock, 0, sizeof(*lock));
    lock->name = name;
}
void spinlock_acquire(spinlock_t *lock) { (void)lock; }
bool spinlock_try_acquire(spinlock_t *lock) { (void)lock; return true; }
void spinlock_release(spinlock_t *lock) { (void)lock; }
bool spinlock_is_held(spinlock_t *lock) { (void)lock; return false; }

void device_get(struct device *dev) { (void)dev; }
void device_put(struct device *dev) { (void)dev; }
void kprint(const char *msg) { (void)msg; }
void kobject_uevent(const char *action, const char *subsystem, const char *name) {
    (void)action;
    (void)subsystem;
    (void)name;
}

static int test_bus_match(struct device *dev, struct driver *drv) {
    (void)dev;
    (void)drv;
    return 1;
}

static int test_probe(struct device *dev) {
    assert(dev != NULL);
    probe_calls++;
    return 0;
}

static int test_attach(struct device *dev) {
    assert(dev != NULL);
    attach_calls++;
    return 0;
}

#include "../../sys/kern/driver.c"

int main(void) {
    struct bus_type bus;
    struct driver drv;
    struct device dev;

    memset(&bus, 0, sizeof(bus));
    memset(&drv, 0, sizeof(drv));
    memset(&dev, 0, sizeof(dev));

    spinlock_init(&bus.lock, "test-bus");
    bus.name = "test";
    bus.match = test_bus_match;
    bus.devices_list = &dev;

    strcpy(dev.name, "dev0");
    dev.bus = &bus;
    dev.ref_count = 1;

    drv.name = "drv0";
    drv.probe = test_probe;
    drv.attach = test_attach;

    assert(driver_register(&drv, &bus) == 0);
    assert(probe_calls == 1);
    assert(attach_calls == 1);
    assert(dev.driver == &drv);

    puts("host_test_driver_register_attach: PASS");
    return 0;
}
