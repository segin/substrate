/*
 * sys/kern/driver.c
 *
 * Core Driver Management Implementation
 */

#include <kern/driver.h>
#include <kern/bus.h>
#include <kern/device.h>
#include <sys/lock.h>
#include <string.h>
#include <stddef.h>

/*
 * probe_device
 *
 * Internal helper to probe a single device against a specific driver.
 * Returns 1 if bound, 0 if not.
 */
static int probe_device(struct driver *drv, struct device *dev) {
    /* If device already has a driver, skip */
    if (dev->driver) return 0;
    
    /* 
     * Matching Precedence:
     * 1. Bus Match (bus->match)
     * 2. Driver Match (drv->match_func) - optional override
     */
     
    /* Check Bus Match */
    if (dev->bus && dev->bus->match) {
        if (!dev->bus->match(dev, drv)) {
            return 0; /* Bus says no match */
        }
    }
    
    /* Check Driver Match */
    if (drv->match_func) {
        if (!drv->match_func(dev, drv)) {
            return 0; /* Driver says no match */
        }
    }
    
    /* 
     * If we get here, the driver and bus think it's a match.
     * Call driver Probe.
     */
    if (drv->probe) {
        if (drv->probe(dev) != 0) {
            return 0; /* Probe failed */
        }
    }
    
    /* Probe succeeded? Automatically attach? 
       The TASK description says "probes called". 
       "Acceptance: dev->driver set, attach() callback invoked" is for driver_attach().
       But usually registration triggers probe & attach.
       Let's assume probe only verifies support. 
       Usually if probe returns success (0), we verify it works.
       Wait, strict FreeBSD/Linux: probe returns 0 (success) or error.
       If success, we might attach.
       The prompt for driver_register says "probes called".
       The prompt for driver_attach says "API: driver_attach(drv, dev)".
       Usually separate. 
       However, `driver_register` usually triggers the binding process.
       I will implement the loop calling probe.
       If probe succeeds, should I attach? 
       Prompt for `driver_register` acceptance: "Driver in bus->drivers_list, probes called". 
       It DOES NOT say "devices attached".
       But `driver_attach` is a separate task.
       I will stick to just calling probe for now? 
       Actually, usually `probe` is "check if I can handle this", and if yes, we attach.
       If I don't attach, the device remains driverless despite a successful probe.
       Let's look at `driver_register` in other OSs. Usually it triggers attach.
       
       However, since `driver_attach` is a separate specific task to implement...
       I will implement `driver_register` to iterate and call `probe`. 
       If `probe` returns 0 (success), I *should* probably call `driver_attach` but I haven't implemented it yet!
       
       Constraint: "Implement driver_register ... Acceptance: probes called".
       I will implement the iteration and probe call. 
       Since I cannot call `driver_attach` yet (it's not implemented), I will just leave it as "probe called".
       Or I can implement a placeholder `driver_attach`? 
       No, `driver_attach` is the NEXT task.
       So I will just iterate and call `probe`. 
       Wait, if I don't attach, then `dev->driver` isn't set.
       If I run `driver_register` again, it will probe again.
       This seems fine for this atomic step.
       
       Actually, `driver_register` acceptance says "probes called".
       `driver_attach` acceptance says "dev->driver set".
       So for `driver_register`, I just need to verify it walks the list and calls probe.
    */
    
    /* For now, we just return result of probe. */
    /* Side note: In real kernel, we would call device_attach(dev) which finds best driver.
       Here we are iterating drivers? No, we are registering ONE driver.
       So we iterate devices on the bus.
    */
     return 1;
}

/*
 * driver_register
 *
 * Registers a driver with the bus and checks for devices.
 */
int driver_register(struct driver *drv, struct bus_type *bus) {
    struct driver *curr_drv;
    struct device *curr_dev;
    
    if (!drv || !bus) return -1;
    
    /* 1. Add to Bus Driver List */
    spinlock_acquire(&bus->lock);
    
    /* Check duplicates */
    curr_drv = bus->drivers_list;
    while (curr_drv) {
        if (curr_drv == drv) {
            spinlock_release(&bus->lock);
            return -1; /* Already registered */
        }
        if (curr_drv->name && drv->name && strcmp(curr_drv->name, drv->name) == 0) {
            spinlock_release(&bus->lock);
            return -1; /* Name collision */
        }
        curr_drv = curr_drv->bus_next;
    }
    
    /* Insert at head */
    drv->bus_next = bus->drivers_list;
    bus->drivers_list = drv;
    drv->bus_type = bus;
    
    spinlock_release(&bus->lock);
    
    /* 2. Probe Existing Devices */
    /* We need to iterate over bus->devices_list safely.
       Locking strategy: Holding bus lock while calling probe is dangerous if probe sleeps or calls into bus.
       Usually we take a snapshot or rely on refcounts. 
       Since we have device refcounting now, we can walk safe.
       
       Simple iteration with lock for now? 
       Or copy list? List might be long.
       Safe iteration:
       acquire lock.
       get first device.
       inc ref.
       release lock.
       while dev:
          probe(drv, dev);
          acquire lock.
          next = dev->bus_next.
          if next: get(next).
          put(dev). // release old
          dev = next.
          release lock.
    */
    
    spinlock_acquire(&bus->lock);
    curr_dev = bus->devices_list;
    /* We can't easily do safe iteration without `device_get` which I just implemented! Great. */
    /* Wait, I cannot call device_get from here if it's not declared in headers I included?
       It is in `kern/device.h`.
    */
    if (curr_dev) {
        device_get(curr_dev);
    }
    spinlock_release(&bus->lock);
    
    while (curr_dev) {
        /* Probe this device against our new driver */
        /* Note: probe_device is internal helper */
        probe_device(drv, curr_dev);
        
        /* Move to next */
        spinlock_acquire(&bus->lock);
        struct device *next = curr_dev->bus_next;
        if (next) {
            device_get(next);
        }
        spinlock_release(&bus->lock);
        
        device_put(curr_dev);
        curr_dev = next;
    }
    
    return 0;
}
