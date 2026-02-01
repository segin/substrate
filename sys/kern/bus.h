/*
 * sys/kern/bus.h
 *
 * Core Data Structure: Bus Type
 * Represents a type of bus (PCI, USB, etc.) and manages devices/drivers.
 */

#ifndef _KERN_BUS_H
#define _KERN_BUS_H

#include <sys/lock.h>

/* Forward declarations */
struct device;
struct driver;
struct device_id;

/*
 * struct bus_type
 *
 * Fields:
 * - name: Name of the bus type (e.g. "pci", "usb")
 * - match: Callback to check if a driver supports a device
 * - probe: Callback to initialize a device on this bus
 * - remove: Callback to remove a device
 * - devices_list: List of devices on this bus type
 * - drivers_list: List of drivers registered for this bus type
 * - lock: Spinlock protecting the lists
 */
struct bus_type {
    const char *name;

    int (*match)(struct device *dev, struct driver *drv);
    int (*probe)(struct device *dev);
    int (*remove)(struct device *dev);

    struct device *devices_list;
    struct driver *drivers_list;

    spinlock_t lock;
};

struct driver *bus_match_device(struct bus_type *bus, struct device *dev);
int bus_id_match(const struct device_id *id, struct device *dev);

#endif /* _KERN_BUS_H */
