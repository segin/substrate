/*
 * test_driver_register.c
 *
 * Unit Tests for driver_register.
 */

#include <kern/driver.h>
#include <kern/bus.h>
#include <kern/device.h>
#include <sys/lock.h>
#include <string.h>
#include <stddef.h>

/* Mocks and State */
static int probe_called = 0;
static struct device *probe_target = NULL;

static int test_match(struct device *dev, struct driver *drv) {
    (void)dev; (void)drv;
    return 1; /* Always match */
}

static int test_probe(struct device *dev) {
    probe_called++;
    probe_target = dev;
    return 0; /* Success */
}

/* Helpers */
static void reset_bus(struct bus_type *bus) {
    memset(bus, 0, sizeof(struct bus_type));
    spinlock_init(&bus->lock, "test_bus");
    bus->match = test_match;
}

/* External Function */
int driver_register(struct driver *drv, struct bus_type *bus);
struct device *device_create(const char *name, struct device *parent);
int device_register(struct device *dev, struct bus_type *bus);

int test_driver_registration_logic(void) {
    struct bus_type bus;
    struct driver drv1, drv2;
    struct device *dev1;
    int ret;
    
    reset_bus(&bus);
    memset(&drv1, 0, sizeof(struct driver));
    memset(&drv2, 0, sizeof(struct driver));
    
    drv1.name = "drv1";
    drv1.probe = (void*)test_probe; /* Cast to fix signature mismatch if any? no, signature matches standard */
    /* Wait, standard signature in driver.h: int (*probe)(struct device *dev); */
    
    /* Test 1: Register driver with no devices */
    ret = driver_register(&drv1, &bus);
    if (ret != 0) return -1;
    if (bus.drivers_list != &drv1) return -2;
    if (drv1.bus_type != &bus) return -3;
    
    /* Test 2: Duplicate registration */
    ret = driver_register(&drv1, &bus);
    if (ret == 0) return -4;
    
    /* Test 3: Name collision */
    drv2.name = "drv1";
    ret = driver_register(&drv2, &bus);
    if (ret == 0) return -5;
    
    /* Test 4: Probe trigger */
    /* Need a fresh bus or reset state */
    reset_bus(&bus); /* Clear drivers list */
    probe_called = 0;
    probe_target = NULL;
    
    /* Add a device */
    dev1 = device_create("dev1", NULL);
    if (!dev1) return -6;
    device_register(dev1, &bus);
    
    /* Register driver again */
    /* Re-init drv1 to clear links */
    memset(&drv1, 0, sizeof(struct driver));
    drv1.name = "drv1";
    drv1.probe = (int(*)(struct device*))test_probe;
    
    ret = driver_register(&drv1, &bus);
    if (ret != 0) return -7;
    
    /* Check probe called */
    if (probe_called != 1) return -8;
    if (probe_target != dev1) return -9;
    
    return 0;
}
