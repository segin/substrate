/*
 * test_device_unregister.c
 *
 * Unit Tests for device_unregister.
 */

#include <kern/device.h>
#include <kern/bus.h>
#include <sys/lock.h>
#include <string.h>
#include <stddef.h>

/* Helpers */
static void reset_bus(struct bus_type *bus) {
    memset(bus, 0, sizeof(struct bus_type));
    spinlock_init(&bus->lock, "test_bus_lock");
}

/* External functions */
int device_register(struct device *dev, struct bus_type *bus);
int device_unregister(struct device *dev);
struct device *device_create(const char *name, struct device *parent);

int test_device_unregister_logic(void) {
    struct bus_type bus;
    struct device *root, *child1, *child2, *grandchild;
    int ret;
    
    reset_bus(&bus);
    
    /* Setup Hierarchy */
    root = device_create("root", NULL);
    child1 = device_create("child1", root);
    child2 = device_create("child2", root);
    grandchild = device_create("grandchild", child1);
    
    /* Register root */
    device_register(root, &bus);
    
    /* Test 1: Unregister attached device from bus */
    ret = device_unregister(root);
    if (ret != 0) return -1;
    if (root->bus != NULL) return -2;
    if (bus.devices_list != NULL) return -3;
    
    /* Re-register for hierarchy test */
    device_register(root, &bus);
    
    /* Test 2: Unregister child1 (middle of siblings, has children, has parent) 
       Note: device_create uses head insertion.
       root->children -> child2 -> child1
    */
    ret = device_unregister(child1);
    if (ret != 0) return -4;
    
    /* Verify Parent Link Broken */
    if (child1->parent != NULL) return -5;
    /* Verify removed from parent list */
    /* root->children should be child2. child2->sibling was child1. child2->sibling should be NULL?
       Wait, list is child2 -> child1 -> NULL.
       Removing child1: child2->sibling = NULL.
    */
    if (root->children != child2) return -6;
    if (child2->sibling != NULL) return -7;
    
    /* Verify Children Orphaned */
    if (grandchild->parent != NULL) return -8;
    /* Verify local list cleared */
    if (child1->children != NULL) return -9;
    
    return 0;
}
