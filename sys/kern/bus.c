/*
 * sys/kern/bus.c
 *
 * Bus Subsystem Implementation
 */

#include <sys/types.h>
#include <sys/errno.h>
#include <sys/lock.h>
#include <stddef.h>
#include <string.h>

#include <kern/bus.h>
#include <kern/driver.h>
#include <kern/device.h>

/*
 * bus_match_device - Find the best driver for a device
 * @bus: Bus to search
 * @dev: Device to match
 *
 * Iterates over all drivers registered on the bus and returns the one
 * that matches the device with the highest priority.
 *
 * Matching logic:
 * 1. If driver->match_func is set, it is called.
 * 2. If not, bus->match is called.
 * 3. Ties in priority are broken by registration order (first found wins).
 *
 * Return: Pointer to best driver, or NULL if no match found.
 */
struct driver *bus_match_device(struct bus_type *bus, struct device *dev)
{
    struct driver *drv;
    struct driver *best_drv = NULL;
    int matched;

    if (!bus || !dev) {
        return NULL;
    }

    spinlock_acquire(&bus->lock);

    drv = bus->drivers_list;
    while (drv) {
        /* Check Driver Override */
        if (dev->driver_override) {
            if (strcmp(drv->name, dev->driver_override) != 0) {
                drv = drv->bus_next;
                continue;
            }
        } else {
            /* Check Blacklist */
            if (driver_is_blacklisted(drv->name)) {
                drv = drv->bus_next;
                continue;
            }
        }

        matched = 0;

        /* Prefer driver-specific match override */
        if (drv->match_func) {
            matched = drv->match_func(dev, drv);
        } else if (bus->match) {
            matched = bus->match(dev, drv);
        }

        if (matched) {
            if (best_drv == NULL || drv->priority > best_drv->priority) {
                best_drv = drv;
            }
        }

        drv = drv->bus_next;
    }

    spinlock_release(&bus->lock);

    return best_drv;
}

/*
 * bus_id_match - Check if a device matches a generic ID entry
 * @id: ID entry to check against (can contain wildcards)
 * @dev: Device to check
 *
 * Checks vendor_id, device_id, and class against the provided ID entry.
 * Use DEVICE_ID_ANY (0xFFFFFFFF) as a wildcard for vendor/device.
 *
 * Return: 1 if match, 0 if not.
 */
int bus_id_match(const struct device_id *id, struct device *dev)
{
    if (!id || !dev) return 0;

    /* Check Vendor ID */
    if (id->vendor_id != DEVICE_ID_ANY) {
        if (id->vendor_id != dev->vendor_id)
            return 0;
    }

    /* Check Device ID */
    if (id->device_id != DEVICE_ID_ANY) {
        if (id->device_id != dev->device_id)
            return 0;
    }

    /* Check Class (with mask) */
    if (id->class_mask != 0) {
        /* Construct device class word: class(16) | subclass(8) | progif(8) */
        uint32_t dev_class_word = ((uint32_t)dev->class << 16) |
                                  ((uint32_t)dev->subclass << 8) |
                                   (uint32_t)dev->progif;
        
        if ((dev_class_word & id->class_mask) != (id->class_id & id->class_mask))
            return 0;
    }

    return 1;
}

/*
 * bus_compatible_match - Check if device compatible string matches
 * @compat: Driver-provided compatible string to search for
 * @dev: Device to check
 *
 * Checks if the provided @compat string exists in the device's
 * compatible string list. The device's compatible list is expected
 * to be a sequence of NUL-terminated strings, terminated by an
 * empty string (double NUL).
 *
 * Example dev->compatible: "ns16550a\0ns16550\0"
 *
 * Return: 1 if match, 0 if not.
 */
int bus_compatible_match(const char *compat, struct device *dev)
{
    const char *ptr;

    if (!compat || !dev || !dev->compatible) {
        return 0;
    }

    ptr = dev->compatible;
    while (*ptr) {
        if (strcmp(ptr, compat) == 0) {
            return 1;
        }
        ptr += strlen(ptr) + 1;
    }

    return 0;
}
