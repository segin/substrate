#include <kern/pci.h>
#include <kern/console.h>
#include <stdio.h>
#include <vm/vm_kmem.h>

static pci_device_t *pci_devices_head = NULL;
static pci_device_t *pci_devices_tail = NULL;

static int pci_scan_bus_internal(uint8_t bus, uint8_t visited[256]);

static void pci_devices_clear(void) {
    pci_device_t *dev = pci_devices_head;

    while (dev != NULL) {
        pci_device_t *next = dev->next;
        kfree(dev, sizeof(*dev));
        dev = next;
    }

    pci_devices_head = NULL;
    pci_devices_tail = NULL;
}

static pci_device_t *pci_record_device(uint8_t bus, uint8_t slot, uint8_t func) {
    pci_device_t *dev;
    uint16_t vendor_id = pci_read_config16(bus, slot, func, 0x00);
    uint16_t device_id = pci_read_config16(bus, slot, func, 0x02);
    uint16_t class_code = pci_read_config16(bus, slot, func, 0x0A);
    char buf[64];

    if (vendor_id == 0xFFFFU) {
        return NULL;
    }

    dev = kmalloc(sizeof(*dev));
    if (dev == NULL) {
        return NULL;
    }

    dev->bus = bus;
    dev->slot = slot;
    dev->func = func;
    dev->vendor_id = vendor_id;
    dev->device_id = device_id;
    dev->class_code = class_code;
    dev->next = NULL;

    if (pci_devices_tail != NULL) {
        pci_devices_tail->next = dev;
    } else {
        pci_devices_head = dev;
    }
    pci_devices_tail = dev;

    sprintf(buf, "PCI %02x:%02x.%u %04x:%04x [%04x]\n",
            bus, slot, func, vendor_id, device_id, class_code);
    kprint(buf);
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

    kprint("Scanning PCI Bus...\n");
    pci_scan();
}
