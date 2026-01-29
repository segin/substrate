/*
 * test_device_refcount.c
 *
 * Unit Tests for device_get/device_put.
 */

#include <kern/device.h>
#include <stddef.h>



/* We need to override kfree for this test, but we are inside the kernel build.
   We can't easily override the real kfree if it's linked.
   However, we can check the behavior by observing side effects if possible, 
   or we just test the refcount logic logic itself.
   
   Wait, if we can't observe kfree, we can't verify "frees at zero".
   But `device_create` uses `kmalloc`.
   
   If we are running in `tests.o`, we are linked with `kern.o`.
   `kern.o` has `device.o` which calls `kfree`.
   `kfree` is in `vm_kmem.c` (based on headers) or `lib.c`.
   
   Let's assume for UNIT TESTING of logic, we can inspect `ref_count`.
   For the "free" part, it's harder to test without mocking kfree.
   
   Maybe I can trust the code inspection for the free call, and just test the refcount increments/decrements
   down to 0. Use a "fake" device allocated on stack? 
   No, `device_put` calls `kfree` which expects a valid heap pointer usually.
   
   If I allocate with `device_create`, it uses real `kmalloc`.
   If I call `device_put` until 0, it calls real `kfree`.
   If I access it after free, it's a use-after-free (undefined behavior, might crash or pass).
   
   I'll test refcount logic:
   1. create -> ref=1
   2. get -> ref=2
   3. put -> ref=1
   4. put -> ref=0 (and freed)
   
   Passes if no crash.
*/

struct device *device_create(const char *name, struct device *parent);
void device_get(struct device *dev);
void device_put(struct device *dev);

int test_device_refcounting(void) {
    struct device *dev = device_create("test_ref", NULL);
    if (!dev) return -1;
    
    if (dev->ref_count != 1) return -2;
    
    device_get(dev);
    if (dev->ref_count != 2) return -3;
    
    device_put(dev);
    if (dev->ref_count != 1) return -4;
    
    device_put(dev);
    /* Device should be freed now. Accessing dev->ref_count here would be UAF. */
    
    return 0;
}
