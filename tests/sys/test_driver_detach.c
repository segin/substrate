/*
 * test_driver_detach.c
 *
 * Unit Tests for driver_detach.
 */

#include <kern/driver.h>
#include <kern/bus.h>
#include <kern/device.h>
#include <sys/lock.h>
#include <string.h>
#include <stddef.h>

/* Mocks and State */
static int detach_called = 0;
static struct device *detach_target = NULL;

static int test_match(struct device *dev, struct driver *drv) {
    (void)dev; (void)drv;
    return 1;
}

static int test_detach(struct device *dev) {
    detach_called++;
    detach_target = dev;
    return 0; /* Success */
}

static int test_detach_fail(struct device *dev) {
    (void)dev;
    return -1;
}

static void reset_bus(struct bus_type *bus) {
    memset(bus, 0, sizeof(struct bus_type));
    spinlock_init(&bus->lock, "test_bus");
    bus->match = test_match;
}

/* External Function */
int driver_detach(struct device *dev);
struct device *device_create(const char *name, struct device *parent);

int test_driver_detach_logic(void) {
    struct bus_type bus;
    struct driver drv;
    struct device *dev;
    int ret;
    
    reset_bus(&bus);
    memset(&drv, 0, sizeof(struct driver));
    
    /* Setup */
    drv.name = "test_drv";
    drv.detach = test_detach;
    drv.bus_type = &bus;
    
    dev = device_create("test_dev", NULL);
    if (!dev) return -1;
    dev->bus = &bus;
    
    /* Manually bind for test */
    dev->driver = &drv;
    
    /* Test 1: Successful detach */
    detach_called = 0;
    detach_target = NULL;
    
    ret = driver_detach(dev);
    if (ret != 0) return -2;
    if (dev->driver != NULL) return -3;
    if (detach_called != 1) return -4;
    if (detach_target != dev) return -5;
    
    /* Test 2: Not bound */
    ret = driver_detach(dev); /* Already unbound */
    if (ret == 0) return -6;
    
    /* Test 3: Detach Callback Failure */
    /* Rebind */
    dev->driver = &drv;
    drv.detach = test_detach_fail;
    
    ret = driver_detach(dev);
    if (ret == 0) return -7; /* Should fail */
    if (dev->driver != &drv) return -8; /* Should stay bound */
    
    return 0;
}
