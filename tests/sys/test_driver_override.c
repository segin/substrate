/*
 * tests/sys/test_driver_override.c
 *
 * Unit tests for driver blacklist and override
 */

#include <sys/types.h>
#include <kern/console.h>
#include <kern/bus.h>
#include <kern/driver.h>
#include <kern/device.h>
#include "tests.h"

/* Mock bus/driver logic needed? 
   We test bus_match_device logic with blacklist/override.
   So we need to setup a bus and drivers.
*/

static int match_always(struct device *dev, struct driver *drv) {
    (void)dev; (void)drv;
    return 1;
}

int test_driver_override_logic(void) {
    struct bus_type bus = {0};
    struct device dev = {0};
    struct driver drv_target = {0};
    struct driver drv_other = {0};

    bus.name = "test_bus";
    bus.match = match_always;
    bus.drivers_list = &drv_target;
    
    drv_target.name = "target_driver";
    drv_target.priority = 10;
    drv_target.bus_next = &drv_other;
    
    drv_other.name = "other_driver";
    drv_other.priority = 20; /* Higher priority */
    drv_other.bus_next = NULL;

    /* 1. Baseline: Higher priority wins */
    if (bus_match_device(&bus, &dev) != &drv_other) {
        kprint("FAIL: Baseline priority check failed (Expected other_driver)\n");
        return -1;
    }

    /* 2. Test Override */
    driver_override(&dev, "target_driver");
    /* Now target_driver should win despite lower priority, because other_driver name mismatches override */
    if (bus_match_device(&bus, &dev) != &drv_target) {
        kprint("FAIL: Override passed but wrong driver selected (Expected target_driver)\n");
        return -1;
    }

    /* Reset override */
    driver_override(&dev, NULL);
    if (bus_match_device(&bus, &dev) != &drv_other) {
        kprint("FAIL: Override reset failed\n");
        return -1;
    }

    /* 3. Test Blacklist */
    /* Blacklist the high priority driver */
    driver_blacklist_add("other_driver");
    
    if (bus_match_device(&bus, &dev) != &drv_target) {
        kprint("FAIL: Blacklist failed to skip other_driver\n");
        return -1;
    }

    return 0;
}
