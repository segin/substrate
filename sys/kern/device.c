/*
 * sys/kern/device.c
 *
 * Core Device Management Implementation
 */

#include <kern/device.h>
#include <kern/bus.h>
#include <kern/driver.h>
#include <sys/errno.h>
#include <sys/kobject.h>
#include <sys/lock.h>
#include <string.h>
#include <stdio.h>
#include <vm/vm_kmem.h> 
#include <sys/types.h>
#include <vfs/vfs.h>

static spinlock_t deferred_probe_lock = SPINLOCK_INIT("deferred_probe");
static struct device *deferred_probe_head;

static int device_has_guid(const struct device *dev) {
    size_t i;

    if (dev == NULL) {
        return 0;
    }
    for (i = 0; i < sizeof(dev->guid); i++) {
        if (dev->guid[i] != 0) {
            return 1;
        }
    }
    return 0;
}

static void device_build_alias(const struct device *dev, char *buf, size_t size) {
    size_t off;
    size_t i;

    if (buf == NULL || size == 0) {
        return;
    }
    buf[0] = '\0';

    if (dev == NULL) {
        return;
    }
    if (dev->serial[0] != '\0') {
        (void)snprintf(buf, size, "by-id/%s", dev->serial);
        return;
    }
    if (!device_has_guid(dev)) {
        return;
    }

    off = (size_t)snprintf(buf, size, "by-id/guid-");
    for (i = 0; i < sizeof(dev->guid) && off + 2 < size; i++) {
        off += (size_t)snprintf(buf + off, size - off, "%02x", dev->guid[i]);
    }
}

static void device_publish_current(struct device *dev) {
    char target[160];

    if (dev == NULL || dev->devnode == NULL || dev->devfs_path[0] == '\0') {
        return;
    }

    strncpy(dev->devnode->name, dev->devfs_path, sizeof(dev->devnode->name) - 1);
    dev->devnode->name[sizeof(dev->devnode->name) - 1] = '\0';
    devfs_register_device(dev->devnode);

    device_build_alias(dev, dev->devfs_alias, sizeof(dev->devfs_alias));
    if (dev->devfs_alias[0] != '\0') {
        (void)snprintf(target, sizeof(target), "/dev/%s", dev->devfs_path);
        (void)devfs_register_alias(dev->devfs_alias, target);
    }
}

static void device_remove_from_deferred_queue(struct device *dev) {
    struct device *curr;
    struct device *prev = NULL;

    spinlock_acquire(&deferred_probe_lock);
    curr = deferred_probe_head;
    while (curr) {
        if (curr == dev) {
            if (prev) {
                prev->deferred_next = curr->deferred_next;
            } else {
                deferred_probe_head = curr->deferred_next;
            }
            dev->deferred_next = NULL;
            dev->flags &= ~DEVICE_FLAG_DEFERRED_PROBE;
            spinlock_release(&deferred_probe_lock);
            device_put(dev);
            return;
        }
        prev = curr;
        curr = curr->deferred_next;
    }
    spinlock_release(&deferred_probe_lock);
}


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
    spinlock_init(&dev->lock, "device_lock");

    /* Link to parent */
    if (parent) {
        dev->parent = parent;
        /* Insert at head of parent's children list */
        spinlock_acquire(&parent->lock);
        dev->sibling = parent->children;
        parent->children = dev;
        spinlock_release(&parent->lock);
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
    kobject_uevent("add", bus->name, dev->name);
    device_publish_current(dev);
    
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

    device_remove_from_deferred_queue(dev);
    device_unpublish(dev);
    
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
        kobject_uevent("remove", bus->name, dev->name);
    }
    
    /* 2. Remove from Parent */
    parent = dev->parent;
    if (parent) {
        spinlock_acquire(&parent->lock);
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
        spinlock_release(&parent->lock);

        dev->parent = NULL;
        dev->sibling = NULL;
    }
    
    /* 3. Handle Children (Orphan them) */
    spinlock_acquire(&dev->lock);
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
    spinlock_release(&dev->lock);
    
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

int device_publish(struct device *dev, fs_node_t *node, const char *path) {
    if (dev == NULL || node == NULL || path == NULL || path[0] == '\0') {
        return -EINVAL;
    }

    dev->devnode = node;
    strncpy(dev->devfs_path, path, sizeof(dev->devfs_path) - 1);
    dev->devfs_path[sizeof(dev->devfs_path) - 1] = '\0';
    dev->devfs_alias[0] = '\0';

    if (dev->bus != NULL) {
        device_publish_current(dev);
    }
    return 0;
}

void device_unpublish(struct device *dev) {
    if (dev == NULL) {
        return;
    }
    if (dev->devfs_alias[0] != '\0') {
        devfs_unregister_alias(dev->devfs_alias);
        dev->devfs_alias[0] = '\0';
    }
    if (dev->devnode != NULL) {
        devfs_unregister_device(dev->devnode);
    }
}

/*
 * device_find_child
 *
 * Finds a child device by name.
 */
struct device *device_find_child(struct device *parent, const char *name) {
    struct device *curr;
    struct device *found = NULL;
    
    if (!parent || !name) return NULL;
    
    spinlock_acquire(&parent->lock);
    curr = parent->children;
    while (curr) {
        if (curr->name[0] && strcmp(curr->name, name) == 0) {
            found = curr;
            break;
        }
        curr = curr->sibling;
    }
    spinlock_release(&parent->lock);
    
    return found;
}

/*
 * device_probe
 *
 * Matches a device against registered drivers on its bus, runs the selected
 * driver's probe callback, and binds the device on success.
 */
int device_probe(struct device *dev) {
    struct driver *drv;
    int ret;

    if (!dev || !dev->bus) {
        return -ENODEV;
    }

    if (dev->driver) {
        return -EBUSY;
    }

    drv = bus_match_device(dev->bus, dev);
    if (!drv) {
        return -ENODEV;
    }

    if (drv->probe) {
        ret = drv->probe(dev);
        if (ret == -EDEFER || ret == EDEFER) {
            return -EDEFER;
        }
        if (ret != 0) {
            return ret;
        }
    }

    return driver_attach(drv, dev);
}

void device_defer_probe(struct device *dev) {
    struct device *curr;

    if (!dev) {
        return;
    }

    spinlock_acquire(&deferred_probe_lock);
    if (dev->flags & DEVICE_FLAG_DEFERRED_PROBE) {
        spinlock_release(&deferred_probe_lock);
        return;
    }

    device_get(dev);
    dev->flags |= DEVICE_FLAG_DEFERRED_PROBE;
    dev->deferred_next = NULL;

    if (!deferred_probe_head) {
        deferred_probe_head = dev;
    } else {
        curr = deferred_probe_head;
        while (curr->deferred_next) {
            curr = curr->deferred_next;
        }
        curr->deferred_next = dev;
    }
    spinlock_release(&deferred_probe_lock);
}

void device_retry_deferred(void) {
    struct device *dev;

    for (;;) {
        spinlock_acquire(&deferred_probe_lock);
        dev = deferred_probe_head;
        if (!dev) {
            spinlock_release(&deferred_probe_lock);
            return;
        }

        deferred_probe_head = dev->deferred_next;
        dev->deferred_next = NULL;
        dev->flags &= ~DEVICE_FLAG_DEFERRED_PROBE;
        spinlock_release(&deferred_probe_lock);

        if (device_probe(dev) == -EDEFER) {
            device_defer_probe(dev);
        }

        device_put(dev);
    }
}

int device_suspend(struct device *dev, pm_state_t state) {
    struct device *child;
    int ret;

    if (!dev) {
        return -EINVAL;
    }

    child = dev->children;
    while (child) {
        ret = device_suspend(child, state);
        if (ret != 0) {
            return ret;
        }
        child = child->sibling;
    }

    if (dev->driver && dev->driver->suspend) {
        ret = dev->driver->suspend(dev, state);
        if (ret != 0) {
            return ret;
        }
    }

    dev->power_state = state;
    return 0;
}

int device_resume(struct device *dev) {
    struct device *child;
    int ret;

    if (!dev) {
        return -EINVAL;
    }

    if (dev->driver && dev->driver->resume) {
        ret = dev->driver->resume(dev);
        if (ret != 0) {
            return ret;
        }
    }

    dev->power_state = PM_STATE_D0;

    child = dev->children;
    while (child) {
        ret = device_resume(child);
        if (ret != 0) {
            return ret;
        }
        child = child->sibling;
    }

    return 0;
}

void device_shutdown(struct device *dev) {
    struct device *child;

    if (!dev) {
        return;
    }

    child = dev->children;
    while (child) {
        device_shutdown(child);
        child = child->sibling;
    }

    if (dev->driver && dev->driver->shutdown) {
        dev->driver->shutdown(dev);
    }
}

int device_reset(struct device *dev) {
    struct device *child;
    int ret;

    if (!dev) {
        return -EINVAL;
    }

    child = dev->children;
    while (child) {
        ret = device_reset(child);
        if (ret != 0) {
            return ret;
        }
        child = child->sibling;
    }

    if (dev->driver && dev->driver->reset) {
        ret = dev->driver->reset(dev);
        if (ret != 0) {
            return ret;
        }
    }

    dev->power_state = PM_STATE_D0;
    return 0;
}

int device_suspend_all(pm_state_t state) {
    struct bus_type *bus;

    for (bus = bus_first(); bus != NULL; bus = bus_next(bus)) {
        struct device *dev = bus->devices_list;
        while (dev != NULL) {
            if (dev->parent == NULL) {
                int ret = device_suspend(dev, state);
                if (ret != 0) {
                    return ret;
                }
            }
            dev = dev->bus_next;
        }
    }
    return 0;
}

int device_resume_all(void) {
    struct bus_type *bus;

    for (bus = bus_first(); bus != NULL; bus = bus_next(bus)) {
        struct device *dev = bus->devices_list;
        while (dev != NULL) {
            if (dev->parent == NULL) {
                int ret = device_resume(dev);
                if (ret != 0) {
                    return ret;
                }
            }
            dev = dev->bus_next;
        }
    }
    return 0;
}

void device_runtime_enable(struct device *dev, uint32_t idle_timeout) {
    if (dev == NULL) {
        return;
    }
    dev->runtime_pm_enabled = 1;
    dev->runtime_idle_timeout = idle_timeout;
    dev->runtime_last_busy = 0;
    dev->runtime_usage_count = 0;
    dev->runtime_suspended = 0;
}

int device_runtime_get(struct device *dev) {
    if (dev == NULL) {
        return -EINVAL;
    }
    if (!dev->runtime_pm_enabled) {
        return 0;
    }
    if (dev->runtime_suspended) {
        int ret = device_resume(dev);
        if (ret != 0) {
            return ret;
        }
        dev->runtime_suspended = 0;
    }
    dev->runtime_usage_count++;
    return 0;
}

int device_runtime_put(struct device *dev, uint32_t now_ticks) {
    if (dev == NULL) {
        return -EINVAL;
    }
    if (!dev->runtime_pm_enabled) {
        return 0;
    }
    if (dev->runtime_usage_count > 0) {
        dev->runtime_usage_count--;
    }
    if (dev->runtime_usage_count == 0) {
        dev->runtime_last_busy = now_ticks;
    }
    return 0;
}

int device_runtime_poll(uint32_t now_ticks) {
    struct bus_type *bus;

    for (bus = bus_first(); bus != NULL; bus = bus_next(bus)) {
        struct device *dev = bus->devices_list;
        while (dev != NULL) {
            if (dev->runtime_pm_enabled &&
                !dev->runtime_suspended &&
                dev->runtime_usage_count == 0 &&
                now_ticks >= dev->runtime_last_busy &&
                now_ticks - dev->runtime_last_busy >= dev->runtime_idle_timeout) {
                int ret = device_suspend(dev, PM_STATE_D3);
                if (ret != 0) {
                    return ret;
                }
                dev->runtime_suspended = 1;
            }
            dev = dev->bus_next;
        }
    }
    return 0;
}
