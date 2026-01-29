/*
 * sys/kern/driver.h
 *
 * Core Data Structure: Driver
 * Represents a device driver capable of managing hardware devices.
 */

#ifndef _KERN_DRIVER_H
#define _KERN_DRIVER_H

#include <stdint.h>

/* Forward declarations */
struct device;
struct bus_type;
struct device_id;

/*
 * struct driver
 *
 * Fields:
 * - name: Human-readable name of the driver
 * - bus_type: Pointer to the bus implementation this driver supports
 * - id_table: Pointer to table of supported device IDs (bus-specific)
 * - probe: Called to verify device existence/support (returns 0 on success)
 * - attach: Called to initialize the device and bind driver
 * - detach: Called to unbind driver and shutdown device instance
 * - suspend: Called to save state and power down (state: D1-D3)
 * - resume: Called to restore state and power up (D0)
 * - shutdown: Called during system shutdown/reboot
 * - reset: Called to reset the device hardware
 * - match_func: Optional custom matching logic (overrides bus default)
 * - priority: Driver priority (for conflict resolution)
 * - flags: Driver status/capability flags
 */
struct driver {
    const char      *name;
    struct bus_type *bus_type;
    const void      *id_table; /* Bus-specific ID table (e.g. pci_device_id) */

    /* Lifecycle Callbacks */
    int  (*probe)(struct device *dev);
    int  (*attach)(struct device *dev);
    int  (*detach)(struct device *dev);
    int  (*suspend)(struct device *dev, int state);
    int  (*resume)(struct device *dev);
    void (*shutdown)(struct device *dev);
    int  (*reset)(struct device *dev);
    
    /* Matching */
    int  (*match_func)(struct device *dev, struct driver *drv);

    /* Scheduling/Policy */
    int      priority;
    uint32_t flags;
    
    /* List Linkage */
    struct driver *bus_next;
};

#endif /* _KERN_DRIVER_H */
