/*
 * sys/kern/device.h
 *
 * Core Data Structure: Device
 * Represents a hardware device in the system topology.
 */

#ifndef _KERN_DEVICE_H
#define _KERN_DEVICE_H

#include <stdint.h>

/* Forward declarations */
struct driver;
struct bus_type;
struct resource;

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
    uint32_t vendor_id;
    uint32_t device_id;
    uint16_t class;
    uint16_t subclass;
    uint8_t  progif;
    char     serial[32];
    uint8_t  guid[16];

    /* Hierarchy */
    struct device *parent;
    struct device *children; /* Head of child list */
    struct device *sibling;  /* Next sibling */

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
};

#endif /* _KERN_DEVICE_H */
