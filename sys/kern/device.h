/*
 * sys/kern/device.h
 *
 * Core Data Structure: Device
 * Represents a hardware device in the system topology.
 */

#ifndef _KERN_DEVICE_H
#define _KERN_DEVICE_H

#include <stdint.h>
#include <sys/lock.h>

#define DEVICE_FLAG_DEFERRED_PROBE 0x00000001U

typedef enum {
    PM_STATE_D0 = 0,
    PM_STATE_D1 = 1,
    PM_STATE_D2 = 2,
    PM_STATE_D3 = 3,
} pm_state_t;

/* Forward declarations */
struct driver;
struct bus_type;
struct resource;
struct fs_node;

/*
 * struct device
 *
 * Fields:
 * - vendor_id: Hardware Vendor ID (pci, usb, etc)
 * - device_id: Hardware Device ID
 * - class: Device Class (e.g. storage, display)
 * - subclass: Device Subclass
 * - progif: Programming Interface
 * - serial: Device Serial Number
 * - guid: Global Unique Identifier
 * - parent: Parent device in the tree (upstream bridge/bus)
 * - children: Head of the list of child devices
 * - sibling: Next device in the parent's children list
 * - resources: List/Array of allocated resources (IO, MEM, IRQ)
 * - power_state: Current power state (D0-D3)
 * - ref_count: Reference count for object lifecycle
 * - driver: Bound driver (if any)
 * - bus: Bus type this device is attached to
 * - flags: Device status flags
 */
struct device {
    /* Identification */
    char     name[32];
    uint32_t vendor_id;
    uint32_t device_id;
    uint16_t class;
    uint16_t subclass;
    uint8_t  progif;
    char     serial[32];
    uint8_t  guid[16];
    const char *compatible; /* NUL-separated compatible strings */
    const char *driver_override; /* Force binding to specific driver */

    /* Hierarchy */
    struct device *parent;
    struct device *children; /* Head of child list */
    struct device *sibling;  /* Next sibling */
    struct device *bus_next; /* Next device on the same bus */
    struct device *deferred_next; /* Next device on deferred probe queue */

    /* Resources */
    struct resource *resources;

    /* State */
    int      power_state;
    int      ref_count;

    /* Binding */
    struct driver   *driver;
    struct bus_type *bus;

    /* Status */
    uint32_t flags;

    /* Optional devfs publication owned by the device model. */
    struct fs_node *devnode;
    char devfs_path[128];
    char devfs_alias[128];

    /* Minimal runtime PM state. */
    uint32_t runtime_idle_timeout;
    uint32_t runtime_last_busy;
    uint32_t runtime_usage_count;
    uint8_t runtime_pm_enabled;
    uint8_t runtime_suspended;

    /* Locking */
    spinlock_t lock;
};

/* Core Device API */
struct device *device_create(const char *name, struct device *parent);
int device_register(struct device *dev, struct bus_type *bus);
int device_unregister(struct device *dev);
void device_get(struct device *dev);
void device_put(struct device *dev);
struct device *device_find_child(struct device *parent, const char *name);
int device_probe(struct device *dev);
void device_defer_probe(struct device *dev);
void device_retry_deferred(void);
int device_suspend(struct device *dev, pm_state_t state);
int device_resume(struct device *dev);
void device_shutdown(struct device *dev);
int device_reset(struct device *dev);
int device_publish(struct device *dev, struct fs_node *node, const char *path);
void device_unpublish(struct device *dev);
int device_suspend_all(pm_state_t state);
int device_resume_all(void);
void device_runtime_enable(struct device *dev, uint32_t idle_timeout);
int device_runtime_get(struct device *dev);
int device_runtime_put(struct device *dev, uint32_t now_ticks);
int device_runtime_poll(uint32_t now_ticks);

#endif /* _KERN_DEVICE_H */
