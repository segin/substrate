/*
 * test_driver_struct.c
 *
 * Unit Tests for struct driver compilation and callback signatures.
 */

#include <kern/driver.h>
#include <kern/device.h>
#include <stddef.h>

/* Mock types */
struct bus_type { int unused; };

/* Dummy callback implementations */
static int dummy_probe(struct device *dev) { (void)dev; return 0; }
static int dummy_attach(struct device *dev) { (void)dev; return 0; }
static int dummy_detach(struct device *dev) { (void)dev; return 0; }
static int dummy_suspend(struct device *dev, int state) { (void)dev; (void)state; return 0; }
static int dummy_resume(struct device *dev) { (void)dev; return 0; }
static void dummy_shutdown(struct device *dev) { (void)dev; }
static int dummy_reset(struct device *dev) { (void)dev; return 0; }
static int dummy_match(struct device *dev, struct driver *drv) { (void)dev; (void)drv; return 1; }

int test_driver_struct_signatures(void);

int test_driver_struct_signatures(void) {
    struct driver drv;
    
    /* Verify field assignment and type safety */
    drv.name = "test_driver";
    drv.bus_type = NULL;
    drv.id_table = NULL;
    
    /* Verify callback signatures match */
    drv.probe = dummy_probe;
    drv.attach = dummy_attach;
    drv.detach = dummy_detach;
    drv.suspend = dummy_suspend;
    drv.resume = dummy_resume;
    drv.shutdown = dummy_shutdown;
    drv.reset = dummy_reset;
    drv.match_func = dummy_match;
    
    drv.priority = 100;
    drv.flags = 0;
    
    if (drv.priority != 100) return -1;
    
    return 0;
}
