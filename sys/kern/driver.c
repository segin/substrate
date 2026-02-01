/*
 * sys/kern/driver.c
 *
 * Core Driver Management Implementation
 */

#include <sys/types.h>
#include <sys/errno.h>
#include <sys/lock.h>
#include <string.h>
#include <stddef.h>

#include "driver.h"
#include "bus.h"
#include "device.h"

#define MAX_BLACKLIST 32
static const char *driver_blacklist[MAX_BLACKLIST];
static int blacklist_count = 0;
static spinlock_t blacklist_lock;

void driver_blacklist_add(const char *name) {
    spinlock_acquire(&blacklist_lock);
    if (blacklist_count < MAX_BLACKLIST) {
        driver_blacklist[blacklist_count++] = name;
    }
    spinlock_release(&blacklist_lock);
}

int driver_is_blacklisted(const char *name) {
    int i;
    /* Lockless read safeish for basic check, or add lock */
    /* Assuming static strings or consistent memory */
    for (i = 0; i < MAX_BLACKLIST; i++) {
        if (driver_blacklist[i] && strcmp(driver_blacklist[i], name) == 0) {
            return 1;
        }
    }
    return 0;
}

void driver_override(struct device *dev, const char *name) {
    if (dev) {
        dev->driver_override = name;
    }
}

/*
 * probe_device
 *
 * Internal helper to probe a single device against a specific driver.
 * Returns 1 if bound, 0 if not.
 */
static int probe_device(struct driver *drv, struct device *dev) {
    int ret;

    if (dev->driver) 
        return 0;
    
    /* 1. Bus Match */
    if (dev->bus && dev->bus->match) {
        if (!dev->bus->match(dev, drv))
            return 0;
    }
    
    /* 2. Driver Match */
    if (drv->match_func) {
        if (!drv->match_func(dev, drv))
            return 0;
    }
    
    /* 3. Probe */
    if (drv->probe) {
        ret = drv->probe(dev);
        if (ret != 0)
            return 0;
    }

    return 1;
}

/*
 * driver_register
 *
 * Registers a driver with the bus and checks for devices.
 */
int driver_register(struct driver *drv, struct bus_type *bus) {
    struct driver *curr_drv;
    struct device *curr_dev;
    
    if (!drv || !bus) return -1;
    
    /* 1. Add to Bus Driver List */
    spinlock_acquire(&bus->lock);
    
    /* Check duplicates */
    curr_drv = bus->drivers_list;
    while (curr_drv) {
        if (curr_drv == drv) {
            spinlock_release(&bus->lock);
            return -1; /* Already registered */
        }
        if (curr_drv->name && drv->name && strcmp(curr_drv->name, drv->name) == 0) {
            spinlock_release(&bus->lock);
            return -1; /* Name collision */
        }
        curr_drv = curr_drv->bus_next;
    }
    
    /* Insert at head */
    drv->bus_next = bus->drivers_list;
    bus->drivers_list = drv;
    drv->bus_type = bus;
    
    spinlock_release(&bus->lock);
    
    /* 2. Probe Existing Devices */
    /* We need to iterate over bus->devices_list safely.
       Locking strategy: Holding bus lock while calling probe is dangerous if probe sleeps or calls into bus.
       Usually we take a snapshot or rely on refcounts. 
       Since we have device refcounting now, we can walk safe.
       
       Simple iteration with lock for now? 
       Or copy list? List might be long.
       Safe iteration:
       acquire lock.
       get first device.
       inc ref.
       release lock.
       while dev:
          probe(drv, dev);
          acquire lock.
          next = dev->bus_next.
          if next: get(next).
          put(dev). // release old
          dev = next.
          release lock.
    */
    
    spinlock_acquire(&bus->lock);
    curr_dev = bus->devices_list;
    /* We can't easily do safe iteration without `device_get` which I just implemented! Great. */
    /* Wait, I cannot call device_get from here if it's not declared in headers I included?
       It is in `kern/device.h`.
    */
    if (curr_dev) {
        device_get(curr_dev);
    }
    spinlock_release(&bus->lock);
    
    while (curr_dev) {
        /* Probe this device against our new driver */
        /* Note: probe_device is internal helper */
        probe_device(drv, curr_dev);
        
        /* Move to next */
        spinlock_acquire(&bus->lock);
        struct device *next = curr_dev->bus_next;
        if (next) {
            device_get(next);
        }
        spinlock_release(&bus->lock);
        
        device_put(curr_dev);
        curr_dev = next;
    }
    
    return 0;
}

/*
 * driver_attach
 *
 * Binds a driver to a device.
 * Calls the driver's attach callback and sets the binding if successful.
 */
int driver_attach(struct driver *drv, struct device *dev) {
    int ret;

    if (!drv || !dev) return -1;

    /* Lock the bus to ensure exclusive access to the device state */
    if (dev->bus) {
        spinlock_acquire(&dev->bus->lock);
    }

    if (dev->driver) {
        /* Already bound */
        if (dev->bus) spinlock_release(&dev->bus->lock);
        return -1;
    }

    /* Verify bus compatibility if implied */
    if (dev->bus && drv->bus_type && dev->bus != drv->bus_type) {
        spinlock_release(&dev->bus->lock);
        return -1;
    }

    /* Call driver attach */
    if (drv->attach) {
        /* Drop lock during callback? 
           Usually attach might sleep or need other locks. 
           But we need to prevent other binders. 
           For now, holding lock prevents race on dev->driver. 
           CAUTION: attach must be atomic/fast or we need fine-grained locking.
           Given Substrate design so far, holding bus lock might be strict.
           Let's hold it to protect dev->driver. 
        */
        ret = drv->attach(dev);
        if (ret != 0) {
            if (dev->bus) spinlock_release(&dev->bus->lock);
            return ret;
        }
    }

    dev->driver = drv;

    if (dev->bus) {
        spinlock_release(&dev->bus->lock);
    }
    
    return 0;
}

/*
 * driver_detach
 *
 * Detaches a driver from a device.
 * Calls the driver's detach callback and clears the binding.
 */
int driver_detach(struct device *dev) {
    struct driver *drv;
    int ret;

    if (!dev) return -1;

    /* Lock the bus */
    if (dev->bus) {
        spinlock_acquire(&dev->bus->lock);
    }
    
    drv = dev->driver;
    if (!drv) {
        /* Not bound */
        if (dev->bus) spinlock_release(&dev->bus->lock);
        return -1;
    }

    /* Call driver detach */
    if (drv->detach) {
        /* See driver_attach regarding locking.
           We hold lock to prevent re-attach during detach.
        */
        ret = drv->detach(dev);
        if (ret != 0) {
            /* Detach failed/refused */
            if (dev->bus) spinlock_release(&dev->bus->lock);
            return ret;
        }
    }

    dev->driver = NULL;

    if (dev->bus) {
        spinlock_release(&dev->bus->lock);
    }

    return 0;
}

/*
 * driver_unregister
 *
 * Unregisters a driver and detaches it from all devices.
 */
int driver_unregister(struct driver *drv) {
    struct bus_type *bus;
    struct driver *curr_drv, *prev_drv;
    struct device *curr_dev;
    
    if (!drv || !drv->bus_type) return -1;
    
    bus = drv->bus_type;
    
    /* 1. Detach from devices */
    spinlock_acquire(&bus->lock);
    curr_dev = bus->devices_list;
    if (curr_dev) device_get(curr_dev);
    spinlock_release(&bus->lock);
    
    while (curr_dev) {
        struct device *next;
        
        /* Check if bound to this driver */
        if (curr_dev->driver == drv) {
            /* Detach */
            if (drv->detach) {
                drv->detach(curr_dev);
            }
            curr_dev->driver = NULL;
        }
        
        /* Move next */
        spinlock_acquire(&bus->lock);
        next = curr_dev->bus_next;
        if (next) device_get(next);
        spinlock_release(&bus->lock);
        
        device_put(curr_dev);
        curr_dev = next;
    }
    
    /* 2. Remove from Bus List */
    spinlock_acquire(&bus->lock);
    
    curr_drv = bus->drivers_list;
    prev_drv = NULL;
    
    while (curr_drv) {
        if (curr_drv == drv) {
            if (prev_drv) {
                prev_drv->bus_next = curr_drv->bus_next;
            } else {
                bus->drivers_list = curr_drv->bus_next;
            }
            break;
        }
        prev_drv = curr_drv;
        curr_drv = curr_drv->bus_next;
    }
    
    drv->bus_type = NULL;
    drv->bus_next = NULL;
    
    spinlock_release(&bus->lock);
    
    return 0;
}
