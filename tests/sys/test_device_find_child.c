/*
 * test_device_find_child.c
 *
 * Unit Tests for device_find_child.
 */

#include <kern/device.h>
#include <stddef.h>
#include <string.h>

/* External functions */
struct device *device_create(const char *name, struct device *parent);
struct device *device_find_child(struct device *parent, const char *name);

int test_find_child_logic(void) {
    struct device *root, *child1, *child2;
    struct device *found;
    
    /* Setup Hierarchy */
    root = device_create("root", NULL);
    if (!root) return -1;
    
    child1 = device_create("child1", root);
    if (!child1) return -2;
    
    child2 = device_create("child2", root);
    if (!child2) return -3;
    
    /* Test 1: Find existing child1 */
    found = device_find_child(root, "child1");
    if (found != child1) return -4;
    
    /* Test 2: Find existing child2 */
    found = device_find_child(root, "child2");
    if (found != child2) return -5;
    
    /* Test 3: Find non-existent child */
    found = device_find_child(root, "child3");
    if (found != NULL) return -6;
    
    /* Test 4: Find with NULL parent */
    found = device_find_child(NULL, "child1");
    if (found != NULL) return -7;
    
    /* Test 5: Find with NULL name */
    found = device_find_child(root, NULL);
    if (found != NULL) return -8;
    
    return 0;
}
