/*
 * test_resource_struct.c
 *
 * Unit Tests for struct resource and helper functions.
 */

#include <kern/resource.h>
#include <kern/device.h>
#include <stddef.h>

/* Real headers provide definitions */

int test_resource_helpers(void);

int test_resource_helpers(void) {
    struct resource parent = { .start = 0x1000, .end = 0x2000 };
    struct resource child = { .start = 0x1100, .end = 0x1200 };
    struct resource overlap = { .start = 0x1F00, .end = 0x2100 };
    struct resource outside = { .start = 0x3000, .end = 0x4000 };
    
    /* Test resource_contains */
    if (!resource_contains(&parent, &child)) return -1;
    if (resource_contains(&child, &parent)) return -2;
    if (resource_contains(&parent, &overlap)) return -3; /* Partial != Contains */
    
    /* Test resource_overlaps */
    if (!resource_overlaps(&parent, &child)) return -4;
    if (!resource_overlaps(&parent, &overlap)) return -5;
    if (resource_overlaps(&parent, &outside)) return -6;
    
    /* Test size */
    if (resource_size(&parent) != 0x1001) return -7;
    
    return 0;
}
