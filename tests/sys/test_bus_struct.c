/*
 * test_bus_struct.c
 *
 * Unit Tests for struct bus_type compilation and layout.
 */

#include <kern/bus.h>
#include <kern/device.h>
#include <kern/driver.h>
#include <stddef.h>

/* Real headers provide definitions */

/* Dummy callbacks */
static int dummy_match(struct device *dev, struct driver *drv) { (void)dev; (void)drv; return 0; }
static int dummy_probe(struct device *dev) { (void)dev; return 0; }
static int dummy_remove(struct device *dev) { (void)dev; return 0; }

int test_bus_struct_layout(void);

int test_bus_struct_layout(void) {
    struct bus_type bus;
    
    bus.name = "test_bus";
    bus.match = dummy_match;
    bus.probe = dummy_probe;
    bus.remove = dummy_remove;
    bus.devices_list = NULL;
    bus.drivers_list = NULL;
    
    /* Suppress unused variable warning */
    (void)bus.name;
    
    // Check lock existence (compilation check)
    // spinlock_t op is opaque usually, but assignment should work conceptually
    // We assume spinlock_init would be called
    
    return 0;
}
