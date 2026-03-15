/*
 * test_device_pm.c
 *
 * Unit tests for device_suspend/device_resume.
 */

#include <kern/device.h>
#include <kern/driver.h>
#include <kern/bus.h>
#include <sys/errno.h>
#include <string.h>

static char call_order[8];
static int call_index;

static int suspend_cb(struct device *dev, int state) {
    (void)state;
    call_order[call_index++] = dev->name[0];
    return 0;
}

static int resume_cb(struct device *dev) {
    call_order[call_index++] = dev->name[0];
    return 0;
}

static void reset_calls(void) {
    memset(call_order, 0, sizeof(call_order));
    call_index = 0;
}

int test_device_pm_logic(void) {
    struct driver drv = {0};
    struct device *root;
    struct device *child1;
    struct device *child2;
    struct device *grandchild;
    int ret;

    drv.suspend = suspend_cb;
    drv.resume = resume_cb;

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
    ret = device_suspend(root, PM_STATE_D3);
    if (ret != 0) return -2;
    if (call_index != 4) return -3;
    if (call_order[0] != 'c') return -4; /* child2 first: head insertion */
    if (call_order[1] != 'g') return -5;
    if (call_order[2] != 'c') return -6; /* child1 */
    if (call_order[3] != 'r') return -7;
    if (root->power_state != PM_STATE_D3) return -8;
    if (child1->power_state != PM_STATE_D3) return -9;
    if (child2->power_state != PM_STATE_D3) return -10;
    if (grandchild->power_state != PM_STATE_D3) return -11;

    reset_calls();
    ret = device_resume(root);
    if (ret != 0) return -12;
    if (call_index != 4) return -13;
    if (call_order[0] != 'r') return -14;
    if (call_order[1] != 'c') return -15; /* child2 first */
    if (call_order[2] != 'c') return -16; /* child1 */
    if (call_order[3] != 'g') return -17;
    if (root->power_state != PM_STATE_D0) return -18;
    if (child1->power_state != PM_STATE_D0) return -19;
    if (child2->power_state != PM_STATE_D0) return -20;
    if (grandchild->power_state != PM_STATE_D0) return -21;

    if (device_suspend(NULL, PM_STATE_D3) != -EINVAL) return -22;
    if (device_resume(NULL) != -EINVAL) return -23;

    {
        static struct bus_type pm_bus = { .name = "pm-test-bus" };
        struct device *root2 = device_create("pmroot", NULL);
        if (!root2) return -24;
        if (bus_register_type(&pm_bus) != 0) return -25;
        root2->driver = &drv;
        if (device_register(root2, &pm_bus) != 0) return -26;
        reset_calls();
        if (device_suspend_all(PM_STATE_D3) != 0) return -27;
        if (root2->power_state != PM_STATE_D3) return -28;
        if (device_resume_all() != 0) return -29;
        if (root2->power_state != PM_STATE_D0) return -30;
    }

    {
        struct device *rpm = device_create("runtime", NULL);
        if (!rpm) return -31;
        rpm->driver = &drv;
        device_runtime_enable(rpm, 10);
        if (device_runtime_get(rpm) != 0) return -32;
        if (rpm->runtime_usage_count != 1) return -33;
        if (device_runtime_put(rpm, 100) != 0) return -34;
        if (rpm->runtime_usage_count != 0) return -35;
        if (device_runtime_poll(109) != 0) return -36;
        if (rpm->runtime_suspended) return -37;
        if (device_runtime_poll(110) != 0) return -38;
        if (!rpm->runtime_suspended) return -39;
        if (device_runtime_get(rpm) != 0) return -40;
        if (rpm->runtime_suspended) return -41;
    }

    return 0;
}
