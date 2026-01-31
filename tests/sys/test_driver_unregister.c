/*
 * test_driver_unregister.c
 *
 * Unit Tests for driver_unregister.
 */

#include <kern/driver.h>
#include <kern/bus.h>
#include <kern/device.h>
#include <sys/lock.h>
#include <string.h>

/* Mocks */
static int detach_called = 0;
static struct device *detach_target = NULL;

static int test_detach(struct device *dev) {
    detach_called++;
    detach_target = dev;
    return 0;
}

static void reset_bus(struct bus_type *bus) {
    memset(bus, 0, sizeof(struct bus_type));
    spinlock_init(&bus->lock, "test_bus");
}

/* External */
int driver_register(struct driver *drv, struct bus_type *bus);
int driver_unregister(struct driver *drv);
struct device *device_create(const char *name, struct device *parent);
int device_register(struct device *dev, struct bus_type *bus);

int test_driver_unregister_logic(void) {
    struct bus_type bus;
    struct driver drv1;
    struct device *dev1;
    int ret;
    
    reset_bus(&bus);
    memset(&drv1, 0, sizeof(struct driver));
    drv1.name = "drv1";
    drv1.detach = test_detach;
    
    dev1 = device_create("dev1", NULL);
    device_register(dev1, &bus);
    
    /* Manually bind for now since driver_register doesn't attach yet */
    /* But driver_register needs to be called to link the driver to bus */
    driver_register(&drv1, &bus);
    dev1->driver = &drv1; 
    
    /* Reset stats */
    detach_called = 0;
    
    /* Test Unregister */
    ret = driver_unregister(&drv1);
    if (ret != 0) return -1;
    
    /* Verify Detach Called */
    if (detach_called != 1) return -2;
    if (detach_target != dev1) return -3;
    
    /* Verify Unbind */
    if (dev1->driver != NULL) return -4;
    
    /* Verify List Removal */
    if (bus.drivers_list != NULL) return -5;
    if (drv1.bus_type != NULL) return -6;
    
    return 0;
}
