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

/*
 * device_unregister
 *
 * Removes a device from its bus and parent.
 * Orphans any children.
 */
int device_unregister(struct device *dev) {
    struct bus_type *bus;
    struct device *parent;
    struct device *curr, *prev;
    
    if (!dev) return -1;
    
    /* 1. Remove from Bus */
    bus = dev->bus;
    if (bus) {
        spinlock_acquire(&bus->lock);
        
        curr = bus->devices_list;
        prev = NULL;
        while (curr) {
            if (curr == dev) {
                if (prev) {
                    prev->bus_next = curr->bus_next;
                } else {
                    bus->devices_list = curr->bus_next;
                }
                break;
            }
            prev = curr;
            curr = curr->bus_next;
        }
        
        dev->bus = NULL;
        dev->bus_next = NULL;
        
        spinlock_release(&bus->lock);
    }
    
    /* 2. Remove from Parent */
    parent = dev->parent;
    if (parent) {
        /* Note: Parent list locking not implemented yet, assuming single-thread or external sync for hierarchy changes */
        /* TODO: Add hierarchy lock if needed */
        
        curr = parent->children;
        prev = NULL;
        while (curr) {
            if (curr == dev) {
                if (prev) {
                    prev->sibling = curr->sibling;
                } else {
                    parent->children = curr->sibling;
                }
                break;
            }
            prev = curr;
            curr = curr->sibling;
        }
        dev->parent = NULL;
        dev->sibling = NULL;
    }
    
    /* 3. Handle Children (Orphan them) */
    curr = dev->children;
    while (curr) {
         struct device *next = curr->sibling;
         curr->parent = NULL;
         curr->sibling = NULL; /* Detach from sibling list too as they are now top-level orphans? 
                                  Or keep them linked? 
                                  Usually orphans are just roots. sibling is used for parent's list.
                                  If parent is NULL, sibling/children defines structure.
                                  Let's zero sibling to be safe/clean roots. */
         curr = next;
    }
    dev->children = NULL;
    
    return 0;
}

/*
 * device_get
 *
 * Increments the reference count of the device.
 */
void device_get(struct device *dev) {
    if (dev) {

        __sync_fetch_and_add(&dev->ref_count, 1);
    }
}

/*
 * device_put
 *
 * Decrements the reference count.
 * If zero, frees the device.
 */
void device_put(struct device *dev) {
    if (dev) {

        if (__sync_sub_and_fetch(&dev->ref_count, 1) <= 0) {
            /* Ensure it's unregistered? 
               Usually safe to assume calling put on a registered device is bad if it hits zero,
               but we just free memory here. */
            kfree(dev, sizeof(struct device));
        }
    }
}

/*
 * device_find_child
 *
 * Finds a child device by name.
 */
struct device *device_find_child(struct device *parent, const char *name) {
    struct device *curr;
    
    if (!parent || !name) return NULL;
    
    curr = parent->children;
    while (curr) {
        if (curr->name[0] && strcmp(curr->name, name) == 0) {
            return curr;
        }
        curr = curr->sibling;
    }
    
    return NULL;
}

