/*
 * test_device_probe.c
 *
 * Unit tests for device_probe.
 */

#include <kern/device.h>
#include <kern/driver.h>
#include <kern/bus.h>
#include <sys/errno.h>
#include <sys/lock.h>
#include <string.h>

static int probe_calls;
static int attach_calls;
static int probe_result;
static struct device *probed_dev;
static struct device *attached_dev;

static int test_bus_match(struct device *dev, struct driver *drv) {
    (void)dev;
    (void)drv;
    return 1;
}

static int test_probe_cb(struct device *dev) {
    probe_calls++;
    probed_dev = dev;
    return probe_result;
}

static int test_attach_cb(struct device *dev) {
    attach_calls++;
    attached_dev = dev;
    return 0;
}

static void reset_state(struct bus_type *bus, struct driver *drv,
                        struct device *dev) {
    memset(bus, 0, sizeof(*bus));
    memset(drv, 0, sizeof(*drv));
    memset(dev, 0, sizeof(*dev));

    spinlock_init(&bus->lock, "test_bus");
    spinlock_init(&dev->lock, "test_dev");

    bus->name = "test";
    bus->match = test_bus_match;
    bus->drivers_list = drv;

    drv->name = "probe_drv";
    drv->bus_type = bus;
    drv->probe = test_probe_cb;
    drv->attach = test_attach_cb;

    dev->bus = bus;

    probe_calls = 0;
    attach_calls = 0;
    probe_result = 0;
    probed_dev = NULL;
    attached_dev = NULL;
}

int test_device_probe_logic(void) {
    struct bus_type bus;
    struct driver drv;
    struct device dev;
    int ret;

    reset_state(&bus, &drv, &dev);

    ret = device_probe(&dev);
    if (ret != 0) return -1;
    if (probe_calls != 1) return -2;
    if (attach_calls != 1) return -3;
    if (probed_dev != &dev) return -4;
    if (attached_dev != &dev) return -5;
    if (dev.driver != &drv) return -6;

    reset_state(&bus, &drv, &dev);
    probe_result = -EDEFER;
    ret = device_probe(&dev);
    if (ret != -EDEFER) return -7;
    if (probe_calls != 1) return -8;
    if (attach_calls != 0) return -9;
    if (dev.driver != NULL) return -10;

    reset_state(&bus, &drv, &dev);
    bus.drivers_list = NULL;
    ret = device_probe(&dev);
    if (ret != -ENODEV) return -11;

    reset_state(&bus, &drv, &dev);
    dev.driver = &drv;
    ret = device_probe(&dev);
    if (ret != -EBUSY) return -12;

    ret = device_probe(NULL);
    if (ret != -ENODEV) return -13;

    memset(&dev, 0, sizeof(dev));
    spinlock_init(&dev.lock, "test_dev2");
    ret = device_probe(&dev);
    if (ret != -ENODEV) return -14;

    return 0;
}
