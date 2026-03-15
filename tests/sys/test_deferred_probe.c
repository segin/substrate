/*
 * test_deferred_probe.c
 *
 * Unit tests for deferred probe queue handling.
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

static int test_bus_match(struct device *dev, struct driver *drv) {
    (void)dev;
    (void)drv;
    return 1;
}

static int test_probe_cb(struct device *dev) {
    (void)dev;
    probe_calls++;
    return probe_result;
}

static int test_attach_cb(struct device *dev) {
    (void)dev;
    attach_calls++;
    return 0;
}

static void reset_state(struct bus_type *bus, struct driver *drv,
                        struct device *dev) {
    memset(bus, 0, sizeof(*bus));
    memset(drv, 0, sizeof(*drv));
    memset(dev, 0, sizeof(*dev));

    spinlock_init(&bus->lock, "test_bus");
    spinlock_init(&dev->lock, "test_dev");

    bus->match = test_bus_match;
    bus->drivers_list = drv;

    drv->name = "defer_drv";
    drv->bus_type = bus;
    drv->probe = test_probe_cb;
    drv->attach = test_attach_cb;

    dev->bus = bus;

    probe_calls = 0;
    attach_calls = 0;
    probe_result = 0;
}

int test_deferred_probe_logic(void) {
    struct bus_type bus;
    struct driver drv;
    struct device dev;

    reset_state(&bus, &drv, &dev);

    device_defer_probe(&dev);
    if ((dev.flags & DEVICE_FLAG_DEFERRED_PROBE) == 0) return -1;

    device_retry_deferred();
    if (probe_calls != 1) return -2;
    if (attach_calls != 1) return -3;
    if (dev.driver != &drv) return -4;
    if (dev.flags & DEVICE_FLAG_DEFERRED_PROBE) return -5;

    reset_state(&bus, &drv, &dev);
    probe_result = -EDEFER;
    device_defer_probe(&dev);
    device_retry_deferred();
    if (probe_calls != 1) return -6;
    if (attach_calls != 0) return -7;
    if ((dev.flags & DEVICE_FLAG_DEFERRED_PROBE) == 0) return -8;
    if (dev.driver != NULL) return -9;

    probe_result = 0;
    device_retry_deferred();
    if (probe_calls != 2) return -10;
    if (attach_calls != 1) return -11;
    if (dev.driver != &drv) return -12;
    if (dev.flags & DEVICE_FLAG_DEFERRED_PROBE) return -13;

    reset_state(&bus, &drv, &dev);
    device_defer_probe(&dev);
    device_defer_probe(&dev);
    device_retry_deferred();
    if (probe_calls != 1) return -14;
    if (attach_calls != 1) return -15;

    return 0;
}
