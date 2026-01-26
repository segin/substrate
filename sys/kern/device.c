/*
 * sys/kern/device.c
 *
 * Core Device Management Implementation
 */

#include <kern/device.h>
#include <kern/bus.h>
#include <sys/lock.h>
#include <string.h>
#include <vm/vm_kmem.h> 
#include <sys/types.h>


/*
 * device_create
 *
 * Allocates and initializes a new device structure.
 * Links it to the parent if provided.
 */
struct device *device_create(const char *name, struct device *parent) {
    struct device *dev;

    dev = (struct device *)kmalloc(sizeof(struct device));
    if (!dev) {
        return NULL;
    }

    memset(dev, 0, sizeof(struct device));

    /* Initialize identity */
    if (name) {
        strncpy(dev->name, name, sizeof(dev->name) - 1);
        dev->name[sizeof(dev->name) - 1] = '\0';
    }

    /* Initialize state */
    dev->ref_count = 1;
    dev->power_state = 0; /* D0 */

    /* Link to parent */
    if (parent) {
        dev->parent = parent;
        /* Insert at head of parent's children list */
        dev->sibling = parent->children;
        parent->children = dev;
    }

    return dev;
}

/*
 * device_register
 *
 * Registers a device with a bus.
 * Checks for duplicates and manages the bus devices list.
 */
int device_register(struct device *dev, struct bus_type *bus) {
    struct device *curr;
    
    if (!dev || !bus) return -1;
    
    /* Check if already registered */
    if (dev->bus) return -1;
    
    spinlock_acquire(&bus->lock);
    
    /* Check for duplicates in the list */
    curr = bus->devices_list;
    while (curr) {
        if (curr == dev) {
            spinlock_release(&bus->lock);
            return -1; /* Duplicate pointer */
        }
        /* Optional: Check name collision if names are unique per bus */
        if (curr->name[0] && dev->name[0] && strcmp(curr->name, dev->name) == 0) {
             spinlock_release(&bus->lock);
             return -1; /* Duplicate name */
        }
        curr = curr->bus_next;
    }
    
    /* Insert at head */
    dev->bus_next = bus->devices_list;
    bus->devices_list = dev;
    dev->bus = bus;
    
    spinlock_release(&bus->lock);
    
    return 0;
}
