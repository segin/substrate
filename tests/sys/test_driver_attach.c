/*
 * test_driver_attach.c
 *
 * Unit Tests for driver_attach.
 */

#include <kern/driver.h>
#include <kern/bus.h>
#include <kern/device.h>
#include <sys/lock.h>
#include <string.h>
#include <stddef.h>

/* Mocks and State */
static int attach_called = 0;
static struct device *attach_target = NULL;

static int test_match(struct device *dev, struct driver *drv) {
    (void)dev; (void)drv;
    return 1;
}

static int test_attach(struct device *dev) {
    attach_called++;
    attach_target = dev;
    return 0; /* Success */
}

static int test_attach_fail(struct device *dev) {
    (void)dev;
    return -1;
}

static void reset_bus(struct bus_type *bus) {
    memset(bus, 0, sizeof(struct bus_type));
    spinlock_init(&bus->lock, "test_bus");
    bus->match = test_match;
}

/* External Function */
int driver_attach(struct driver *drv, struct device *dev);
struct device *device_create(const char *name, struct device *parent);

int test_driver_attach_logic(void) {
    struct bus_type bus;
    struct driver drv;
    struct device *dev;
    int ret;
    
    reset_bus(&bus);
    memset(&drv, 0, sizeof(struct driver));
    
    /* Setup */
    drv.name = "test_drv";
    drv.attach = test_attach;
    drv.bus_type = &bus;
    
    dev = device_create("test_dev", NULL);
    if (!dev) return -1;
    dev->bus = &bus;
    
    /* Test 1: Successful attach */
    attach_called = 0;
    attach_target = NULL;
    
    ret = driver_attach(&drv, dev);
    if (ret != 0) return -2;
    if (dev->driver != &drv) return -3;
    if (attach_called != 1) return -4;
    if (attach_target != dev) return -5;
    
    /* Test 2: Already bound */
    ret = driver_attach(&drv, dev);
    if (ret == 0) return -6; /* Should fail */
    
    /* Test 3: Attach Callback Failure */
    /* Unbind first (manually for unit test) */
    dev->driver = NULL;
    drv.attach = test_attach_fail;
    
    ret = driver_attach(&drv, dev);
    if (ret == 0) return -7; /* Should fail */
    if (dev->driver != NULL) return -8; /* Should not be bound */
    
    /* Test 4: Bus Mismatch */
    struct bus_type other_bus;
    reset_bus(&other_bus);
    
    dev->bus = &other_bus; /* Pretend device is on other bus */
    /* drv.bus_type is still &bus */
    
    ret = driver_attach(&drv, dev);
    if (ret == 0) return -9; /* Should mismatch */
    
    return 0;
}
