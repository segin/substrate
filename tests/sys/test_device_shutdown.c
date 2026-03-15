/*
 * test_device_shutdown.c
 *
 * Unit tests for device_shutdown.
 */

#include <kern/device.h>
#include <kern/driver.h>
#include <string.h>

static char call_order[8];
static int call_index;

static void shutdown_cb(struct device *dev) {
    call_order[call_index++] = dev->name[0];
}

static void reset_calls(void) {
    memset(call_order, 0, sizeof(call_order));
    call_index = 0;
}

int test_device_shutdown_logic(void) {
    struct driver drv = {0};
    struct device *root;
    struct device *child1;
    struct device *child2;
    struct device *grandchild;

    drv.shutdown = shutdown_cb;

    root = device_create("root", NULL);
    child1 = device_create("child1", root);
    child2 = device_create("child2", root);
    grandchild = device_create("grandchild", child1);
    if (!root || !child1 || !child2 || !grandchild) return -1;

    root->driver = &drv;
    child1->driver = &drv;
    child2->driver = &drv;
    grandchild->driver = &drv;

    reset_calls();
    device_shutdown(root);
    if (call_index != 4) return -2;
    if (call_order[0] != 'c') return -3; /* child2 first: head insertion */
    if (call_order[1] != 'g') return -4;
    if (call_order[2] != 'c') return -5; /* child1 */
    if (call_order[3] != 'r') return -6;

    device_shutdown(NULL);

    return 0;
}
