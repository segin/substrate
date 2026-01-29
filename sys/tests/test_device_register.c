/*
 * test_device_register.c
 *
 * Unit Tests for device_register.
 */

#include <kern/device.h>
#include <kern/bus.h>
#include <sys/lock.h>
#include <string.h>
#include <stddef.h>

/* Helper to reset bus state */
static void reset_bus(struct bus_type *bus) {
    memset(bus, 0, sizeof(struct bus_type));
    spinlock_init(&bus->lock, "test_bus_lock");
}

/* External functions being tested */
int device_register(struct device *dev, struct bus_type *bus);
struct device *device_create(const char *name, struct device *parent);

int test_device_registration_logic(void) {
    struct bus_type bus;
    struct device *dev1, *dev2, *dev3;
    int ret;

    reset_bus(&bus);

    /* Test 1: Successful registration */
    dev1 = device_create("dev1", NULL);
    if (!dev1) return -1;
    
    ret = device_register(dev1, &bus);
    if (ret != 0) return -2;
    if (dev1->bus != &bus) return -3;
    if (bus.devices_list != dev1) return -4;
    
    /* Test 2: Duplicate pointer registration */
    ret = device_register(dev1, &bus);
    if (ret == 0) return -5; /* Should fail */
    
    /* Test 3: Duplicate name registration */
    dev2 = device_create("dev1", NULL); /* Same name as dev1 */
    if (!dev2) return -6;
    
    ret = device_register(dev2, &bus);
    if (ret == 0) return -7; /* Should fail due to name collision */
    
    /* Test 4: Second distinct device */
    dev3 = device_create("dev3", NULL);
    if (!dev3) return -8;
    
    ret = device_register(dev3, &bus);
    if (ret != 0) return -9;
    if (bus.devices_list != dev3) return -10; /* Head insertion */
    if (dev3->bus_next != dev1) return -11;   /* Linked list intact */
    
    return 0;
}
