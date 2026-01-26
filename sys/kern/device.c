/*
 * sys/kern/device.c
 *
 * Core Device Management Implementation
 */

#include <kern/device.h>
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
