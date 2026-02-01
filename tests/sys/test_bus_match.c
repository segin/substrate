/*
 * tests/sys/test_bus_match.c
 *
 * Unit tests for bus_match_device()
 */

#include <sys/types.h>
#include <sys/types.h>
#include <kern/console.h>
#include <kern/bus.h>
#include <kern/driver.h>
#include <kern/device.h>
#include "tests.h"

/* Mocks */
static int match_always(struct device *dev, struct driver *drv) {
    (void)dev; (void)drv;
    return 1;
}

static int match_never(struct device *dev, struct driver *drv) {
    (void)dev; (void)drv;
    return 0;
}

static int match_specific_device(struct device *dev, struct driver *drv) {
    (void)drv;
    /* Simulate matching device ID 0x1234 */
    return (dev->device_id == 0x1234);
}

int test_bus_match_logic(void) {
    struct bus_type bus = {0};
    struct device dev1 = {0};
    struct device dev2 = {0};
    struct driver drv_low = {0};
    struct driver drv_high = {0};
    struct driver drv_specific = {0};

    bus.name = "test_bus";
    
    dev1.name[0] = 'd'; dev1.name[1] = '1'; dev1.name[2] = 0;
    dev2.name[0] = 'd'; dev2.name[1] = '2'; dev2.name[2] = 0;
    dev2.device_id = 0x1234;

    drv_low.name = "low_prio";
    drv_low.priority = 10;
    drv_low.match_func = NULL;
    
    drv_high.name = "high_prio";
    drv_high.priority = 100;
    drv_high.match_func = NULL;

    drv_specific.name = "specific";
    drv_specific.priority = 50;
    drv_specific.match_func = match_specific_device;

    /* 1. Test Empty Bus */
    if (bus_match_device(&bus, &dev1) != NULL) {
        kprint("FAIL: Matched matching on empty bus\n");
        return -1;
    }

    /* 2. Test Single Driver (No Match) */
    bus.match = match_never;
    bus.drivers_list = &drv_low;
    drv_low.bus_next = NULL;
    
    if (bus_match_device(&bus, &dev1) != NULL) {
        kprint("FAIL: Matched matching when match indicates failure\n");
        return -1;
    }

    /* 3. Test Single Driver (Match) */
    bus.match = match_always;
    if (bus_match_device(&bus, &dev1) != &drv_low) {
        kprint("FAIL: Failed to match single compatible driver\n");
        return -1;
    }

    /* 4. Test Priority Ordering */
    /* Add High Prio to list (High -> Low) */
    drv_high.bus_next = &drv_low;
    bus.drivers_list = &drv_high;
    
    /* Both match (match_always set on bus) */
    if (bus_match_device(&bus, &dev1) != &drv_high) {
        kprint("FAIL: Priority check failed (Expected High)\n");
        return -1;
    }

    /* Reverse list order (Low -> High) */
    drv_low.bus_next = &drv_high;
    drv_high.bus_next = NULL;
    bus.drivers_list = &drv_low;
    
    if (bus_match_device(&bus, &dev1) != &drv_high) {
        kprint("FAIL: Priority check failed with reverse list (Expected High)\n");
        return -1;
    }

    /* 5. Test Driver Specific Match Override */
    /* Chain specific -> low. Bus match is always. Specific matches only dev2. */
    bus.drivers_list = &drv_specific;
    drv_specific.bus_next = &drv_low;
    
    /* dev1: specific fails (custom), low matches (bus default). low is priority 10. */
    if (bus_match_device(&bus, &dev1) != &drv_low) {
        kprint("FAIL: Driver override match failed (Expected Low for dev1)\n");
        return -1;
    }

    /* dev2: specific matches (custom), low matches (bus default). 
       Specific (50) > Low (10). */
    if (bus_match_device(&bus, &dev2) != &drv_specific) {
        kprint("FAIL: Driver override match failed (Expected Specific for dev2)\n");
        return -1;
    }

    return 0;
}

int test_bus_id_match_logic(void) {
    struct device dev = {0};
    struct device_id id_exact = {0};
    struct device_id id_wild = {0};
    struct device_id id_class = {0};

    dev.vendor_id = 0x8086;
    dev.device_id = 0x1000;
    dev.class = 0x01;    /* Storage */
    dev.subclass = 0x01; /* IDE */
    dev.progif = 0x80;   /* Master */

    /* 1. Exact Match */
    id_exact.vendor_id = 0x8086;
    id_exact.device_id = 0x1000;
    if (bus_id_match(&id_exact, &dev) != 1) {
        kprint("FAIL: bus_id_match failed on exact match\n");
        return -1;
    }

    id_exact.device_id = 0x1001;
    if (bus_id_match(&id_exact, &dev) != 0) {
        kprint("FAIL: bus_id_match matched incorrectly (wrong device id)\n");
        return -1;
    }

    /* 2. Wildcard Match */
    id_wild.vendor_id = DEVICE_ID_ANY;
    id_wild.device_id = 0x1000;
    if (bus_id_match(&id_wild, &dev) != 1) {
        kprint("FAIL: bus_id_match failed on vendor wildcard\n");
        return -1;
    }

    id_wild.vendor_id = 0x8086;
    id_wild.device_id = DEVICE_ID_ANY;
    if (bus_id_match(&id_wild, &dev) != 1) {
        kprint("FAIL: bus_id_match failed on device wildcard\n");
        return -1;
    }

    /* 3. Class Match */
    /* Match storage class (0x01xxxx) */
    id_class.vendor_id = DEVICE_ID_ANY;
    id_class.device_id = DEVICE_ID_ANY;
    id_class.class_id = 0x010000;
    id_class.class_mask = 0xFF0000;
    
    if (bus_id_match(&id_class, &dev) != 1) {
        kprint("FAIL: bus_id_match failed on class match\n");
        return -1;
    }

    /* Mismatch subclass */
    id_class.class_id = 0x010200;
    id_class.class_mask = 0xFFFF00;
    if (bus_id_match(&id_class, &dev) != 0) {
        kprint("FAIL: bus_id_match matched incorrectly (wrong subclass)\n");
        return -1;
    }

    return 0;
}
