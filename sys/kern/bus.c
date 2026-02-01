/*
 * sys/kern/bus.c
 *
 * Bus Subsystem Implementation
 */

#include <sys/types.h>
#include <sys/errno.h>
#include <sys/spinlock.h>
#include <stddef.h>

#include "bus.h"
#include "driver.h"
#include "device.h"

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
