#include <kern/pci.h>
#include <kern/device.h>
#include <kern/driver.h>
#include <kern/console.h>
#include <stdio.h>
#include <string.h>
#include <sys/errno.h>
#include <vm/vm_kmem.h>

static pci_device_t *pci_devices_head = NULL;
static pci_device_t *pci_devices_tail = NULL;
struct bus_type pci_bus_type = {
    .name = "pci",
};
static int pci_bus_initialized = 0;

static int pci_scan_bus_internal(uint8_t bus, uint8_t visited[256]);

static int pci_id_table_is_end(const device_id_t *id) {
    return id->vendor_id == 0 &&
           id->device_id == 0 &&
           id->class_id == 0 &&
           id->class_mask == 0 &&
           id->driver_data == 0;
}

static int pci_bus_match(struct device *dev, struct driver *drv) {
    const device_id_t *id;

    if (drv == NULL || drv->id_table == NULL) {
        return 0;
    }

    for (id = (const device_id_t *)drv->id_table; !pci_id_table_is_end(id); id++) {
        if (bus_id_match(id, dev)) {
            return 1;
        }
    }

    return 0;
}

static void pci_bus_ensure_init(void) {
    if (pci_bus_initialized) {
        return;
    }

    memset(&pci_bus_type.lock, 0, sizeof(pci_bus_type.lock));
    spinlock_init(&pci_bus_type.lock, "pci_bus");
    pci_bus_type.name = "pci";
    pci_bus_type.match = pci_bus_match;
    pci_bus_type.probe = NULL;
    pci_bus_type.remove = NULL;
    pci_bus_type.devices_list = NULL;
    pci_bus_type.drivers_list = NULL;
    pci_bus_initialized = 1;
}

static void pci_devices_clear(void) {
    pci_device_t *dev = pci_devices_head;

    while (dev != NULL) {
        pci_device_t *next = dev->next;
        if (dev->kdev != NULL) {
            device_unregister(dev->kdev);
            device_put(dev->kdev);
        }
        kfree(dev, sizeof(*dev));
        dev = next;
    }

    pci_devices_head = NULL;
    pci_devices_tail = NULL;
}

static pci_device_t *pci_record_device(uint8_t bus, uint8_t slot, uint8_t func) {
    return pci_device_create(bus, slot, func);
}

pci_device_t *pci_device_create(uint8_t bus, uint8_t slot, uint8_t func) {
    pci_device_t *dev;
    struct device *kdev;
    uint16_t vendor_id = pci_read_config16(bus, slot, func, 0x00);
    uint16_t device_id = pci_read_config16(bus, slot, func, 0x02);
    uint16_t class_code = pci_read_config16(bus, slot, func, 0x0A);
    uint8_t progif = pci_read_config8(bus, slot, func, 0x09);
    uint8_t subclass = pci_read_config8(bus, slot, func, 0x0A);
    uint8_t class_id = pci_read_config8(bus, slot, func, 0x0B);
    char name[32];
    int probe_ret;

    if (vendor_id == 0xFFFFU) {
        return NULL;
    }

    pci_bus_ensure_init();

    dev = kmalloc(sizeof(*dev));
    if (dev == NULL) {
        return NULL;
    }
    memset(dev, 0, sizeof(*dev));

    snprintf(name, sizeof(name), "pci%02x:%02x.%u", bus, slot, func);
    kdev = device_create(name, NULL);
    if (kdev == NULL) {
        kfree(dev, sizeof(*dev));
        return NULL;
    }

    dev->bus = bus;
    dev->slot = slot;
    dev->func = func;
    dev->vendor_id = vendor_id;
    dev->device_id = device_id;
    dev->class_code = class_code;
    dev->kdev = kdev;
    dev->next = NULL;

    kdev->vendor_id = vendor_id;
    kdev->device_id = device_id;
    kdev->class = class_id;
    kdev->subclass = subclass;
    kdev->progif = progif;

    if (device_register(kdev, &pci_bus_type) != 0) {
        device_put(kdev);
        kfree(dev, sizeof(*dev));
        return NULL;
    }

    if (pci_devices_tail != NULL) {
        pci_devices_tail->next = dev;
    } else {
        pci_devices_head = dev;
    }
    pci_devices_tail = dev;

    probe_ret = device_probe(kdev);
    if (probe_ret == -EDEFER) {
        device_defer_probe(kdev);
    }

    return dev;
}

static int pci_scan_bridge_internal(pci_device_t *bridge, uint8_t visited[256]) {
    uint8_t secondary;
    uint8_t subordinate;
    int found = 0;

    if (bridge == NULL || bridge->class_code != 0x0604U) {
        return 0;
    }

    secondary = pci_read_config8(bridge->bus, bridge->slot, bridge->func, 0x19);
    subordinate = pci_read_config8(bridge->bus, bridge->slot, bridge->func, 0x1A);
    if (secondary == 0 || subordinate < secondary) {
        return 0;
    }

    for (uint16_t bus = secondary; bus <= subordinate; bus++) {
        found += pci_scan_bus_internal((uint8_t)bus, visited);
    }

    return found;
}

static int pci_scan_bus_internal(uint8_t bus, uint8_t visited[256]) {
    int found = 0;

    if (visited[bus]) {
        return 0;
    }
    visited[bus] = 1;

    for (uint8_t slot = 0; slot < 32; slot++) {
        uint16_t vendor_id = pci_read_config16(bus, slot, 0, 0x00);
        uint8_t header_type;
        uint8_t functions;

        if (vendor_id == 0xFFFFU) {
            continue;
        }

        header_type = pci_read_config8(bus, slot, 0, 0x0E);
        functions = (header_type & 0x80U) ? 8U : 1U;

        for (uint8_t func = 0; func < functions; func++) {
            pci_device_t *dev = pci_record_device(bus, slot, func);
            if (dev != NULL) {
                found++;
                found += pci_scan_bridge_internal(dev, visited);
            }
        }
    }

    return found;
}

int pci_scan_bus(uint8_t bus) {
    uint8_t visited[256] = {0};

    if (!pci_present()) {
        return 0;
    }

    return pci_scan_bus_internal(bus, visited);
}

int pci_scan_bridge(pci_device_t *bridge) {
    uint8_t visited[256] = {0};

    if (!pci_present()) {
        return 0;
    }

    if (bridge != NULL) {
        visited[bridge->bus] = 1;
    }
    return pci_scan_bridge_internal(bridge, visited);
}

void pci_scan(void) {
    if (!pci_present()) {
        return;
    }

    pci_bus_ensure_init();
    pci_devices_clear();
    {
        uint8_t visited[256] = {0};
        for (uint16_t bus = 0; bus < 256; bus++) {
            pci_scan_bus_internal((uint8_t)bus, visited);
        }
    }
}

pci_device_t *pci_find_device(uint16_t vendor_id, uint16_t device_id, pci_device_t *from) {
    pci_device_t *curr = from ? from->next : pci_devices_head;

    while (curr != NULL) {
        if (curr->vendor_id == vendor_id &&
            (device_id == 0xFFFFU || curr->device_id == device_id)) {
            return curr;
        }
        curr = curr->next;
    }

    return NULL;
}

void pci_init(void) {
    if (!pci_present()) {
        kprint("PCI: configuration mechanism #1 unavailable, skipping scan.\n");
        return;
    }

    pci_bus_ensure_init();
    kprint("Scanning PCI Bus...\n");
    pci_scan();
}
