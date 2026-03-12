/*
 * test_device_reset.c
 *
 * Unit tests for device_reset.
 */

#include <kern/device.h>
#include <kern/driver.h>
#include <sys/errno.h>
#include <string.h>

static char call_order[8];
static int call_index;

static int reset_cb(struct device *dev) {
    call_order[call_index++] = dev->name[0];
    return 0;
}

static void reset_calls(void) {
    memset(call_order, 0, sizeof(call_order));
    call_index = 0;
}

int test_device_reset_logic(void) {
    struct driver drv = {0};
    struct device *root;
    struct device *child1;
    struct device *child2;
    struct device *grandchild;
    int ret;

    drv.reset = reset_cb;

    root = device_create("root", NULL);
    child1 = device_create("child1", root);
    child2 = device_create("child2", root);
    grandchild = device_create("grandchild", child1);
    if (!root || !child1 || !child2 || !grandchild) return -1;

    root->driver = &drv;
    child1->driver = &drv;
    child2->driver = &drv;
    grandchild->driver = &drv;

    root->power_state = PM_STATE_D3;
    child1->power_state = PM_STATE_D3;
    child2->power_state = PM_STATE_D3;
    grandchild->power_state = PM_STATE_D3;

    reset_calls();
    ret = device_reset(root);
    if (ret != 0) return -2;
    if (call_index != 4) return -3;
    if (call_order[0] != 'c') return -4; /* child2 first: head insertion */
    if (call_order[1] != 'g') return -5;
    if (call_order[2] != 'c') return -6; /* child1 */
    if (call_order[3] != 'r') return -7;
    if (root->power_state != PM_STATE_D0) return -8;
    if (child1->power_state != PM_STATE_D0) return -9;
    if (child2->power_state != PM_STATE_D0) return -10;
    if (grandchild->power_state != PM_STATE_D0) return -11;

    if (device_reset(NULL) != -EINVAL) return -12;

    return 0;
}
