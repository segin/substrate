#include <kern/pci.h>
#include <kern/device.h>
#include <kern/driver.h>
#include <kern/console.h>
#include <stdio.h>
#include <string.h>
#include <sys/errno.h>
#include <vm/vm_kmem.h>

static pci_device_t *pci_devices_head;
static pci_device_t *pci_devices_tail;
struct bus_type pci_bus_type = {
    .name = "pci",
};
static int pci_bus_initialized;

static int pci_scan_bus_internal(uint8_t bus, uint8_t visited[256]);
static int pci_id_table_is_end(const device_id_t *id);
static uintptr_t pci_bar_base(pci_device_t *dev, int bar);

static void pci_release_bar_resources(pci_device_t *dev) {
    int bar;

    if (dev == NULL) {
        return;
    }

    for (bar = 0; bar < PCI_BAR_COUNT; bar++) {
        struct resource *res = dev->bar_resource[bar];
        if (res == NULL) {
            continue;
        }
        if (res->type == RES_IO) {
            release_region(res->start, (size_t)resource_size(res));
        } else if (res->type == RES_MEM) {
            release_mem_region(res->start, (size_t)resource_size(res));
        }
        dev->bar_resource[bar] = NULL;
    }
}

static void pci_unlink_device(pci_device_t *dev) {
    pci_device_t *curr;
    pci_device_t *prev = NULL;

    curr = pci_devices_head;
    while (curr != NULL) {
        if (curr == dev) {
            if (prev != NULL) {
                prev->next = curr->next;
            } else {
                pci_devices_head = curr->next;
            }
            if (pci_devices_tail == curr) {
                pci_devices_tail = prev;
            }
            curr->next = NULL;
            return;
        }
        prev = curr;
        curr = curr->next;
    }
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
    pci_bus_type.next_registered = NULL;
    (void)bus_register_type(&pci_bus_type);
    pci_bus_initialized = 1;
}

static int pci_id_table_is_end(const device_id_t *id) {
    return id->vendor_id == 0 &&
           id->device_id == 0 &&
           id->class_id == 0 &&
           id->class_mask == 0 &&
           id->driver_data == 0;
}

static void pci_devices_clear(void) {
    pci_device_t *dev = pci_devices_head;

    while (dev != NULL) {
        pci_device_t *next = dev->next;
        pci_release_bar_resources(dev);
        if (dev->kdev != NULL) {
            driver_detach(dev->kdev);
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
    dev->class_code = ((uint32_t)class_id << 8) | subclass;
    dev->kdev = kdev;

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

pci_device_t *pci_find_bdf(uint8_t bus, uint8_t slot, uint8_t func) {
    pci_device_t *curr = pci_devices_head;

    while (curr != NULL) {
        if (curr->bus == bus && curr->slot == slot && curr->func == func) {
            return curr;
        }
        curr = curr->next;
    }

    return NULL;
}

int pci_remove_device(pci_device_t *dev) {
    if (dev == NULL) {
        return -EINVAL;
    }

    pci_release_bar_resources(dev);
    if (dev->kdev != NULL) {
        driver_detach(dev->kdev);
        device_unregister(dev->kdev);
        device_put(dev->kdev);
        dev->kdev = NULL;
    }
    pci_unlink_device(dev);
    kfree(dev, sizeof(*dev));
    return 0;
}

int pci_find_capability(pci_device_t *dev, uint8_t cap_id) {
    uint16_t status;
    uint16_t ptr;
    int guard = 48;

    if (dev == NULL || cap_id == 0) {
        return 0;
    }

    status = pci_read_config16(dev->bus, dev->slot, dev->func, 0x06);
    if ((status & PCI_STATUS_CAP_LIST) == 0) {
        return 0;
    }

    ptr = (uint16_t)(pci_read_config8(dev->bus, dev->slot, dev->func, 0x34) & (uint8_t)~0x3U);
    while (ptr >= 0x40U && guard-- > 0) {
        uint16_t next;

        if (pci_read_config8(dev->bus, dev->slot, dev->func, ptr) == cap_id) {
            return ptr;
        }
        next = (uint16_t)(pci_read_config8(dev->bus, dev->slot, dev->func, (uint16_t)(ptr + 1U)) &
                          (uint8_t)~0x3U);
        if (next == 0 || next == ptr) {
            break;
        }
        ptr = next;
    }

    return 0;
}

int pci_find_ext_capability(pci_device_t *dev, uint16_t cap_id) {
    uint16_t ptr = 0x100U;
    int guard = 256;

    if (dev == NULL || cap_id == 0) {
        return 0;
    }

    while (ptr >= 0x100U && ptr + 4U <= PCI_EXT_CONFIG_SPACE_SIZE && guard-- > 0) {
        uint32_t hdr = pci_read_config32(dev->bus, dev->slot, dev->func, ptr);
        uint16_t id = (uint16_t)(hdr & 0xFFFFU);
        uint16_t next = (uint16_t)((hdr >> 20) & 0xFFFU);

        if (hdr == 0xFFFFFFFFU || hdr == 0U) {
            break;
        }
        if (id == cap_id) {
            return ptr;
        }
        if (next < 0x100U || next == ptr) {
            break;
        }
        ptr = next;
    }

    return 0;
}

int pci_bar_type(pci_device_t *dev, int bar) {
    uint32_t value;

    if (dev == NULL || bar < 0 || bar >= PCI_BAR_COUNT) {
        return PCI_BAR_NONE;
    }

    value = pci_read_config32(dev->bus, dev->slot, dev->func, (uint16_t)(0x10 + bar * 4));
    if (value == 0 || value == 0xFFFFFFFFU) {
        return PCI_BAR_NONE;
    }
    if (value & 0x1U) {
        return PCI_BAR_IO;
    }
    if ((value & 0x6U) == 0x4U) {
        return PCI_BAR_MEM64;
    }
    return PCI_BAR_MEM32;
}

static uintptr_t pci_bar_base(pci_device_t *dev, int bar) {
    uint32_t low;
    uint64_t base;
    int type = pci_bar_type(dev, bar);

    if (type == PCI_BAR_NONE) {
        return 0;
    }

    low = pci_read_config32(dev->bus, dev->slot, dev->func, (uint16_t)(0x10 + bar * 4));
    if (type == PCI_BAR_IO) {
        return (uintptr_t)(low & ~0x3U);
    }

    base = (uint64_t)(low & ~0xFU);
    if (type == PCI_BAR_MEM64 && bar + 1 < PCI_BAR_COUNT) {
        base |= (uint64_t)pci_read_config32(dev->bus, dev->slot, dev->func,
                                            (uint16_t)(0x10 + (bar + 1) * 4)) << 32;
    }
    return (uintptr_t)base;
}

size_t pci_bar_size(pci_device_t *dev, int bar) {
    uint16_t offset;
    uint32_t orig_low;
    uint32_t mask_low;
    int type = pci_bar_type(dev, bar);

    if (dev == NULL || bar < 0 || bar >= PCI_BAR_COUNT || type == PCI_BAR_NONE) {
        return 0;
    }

    offset = (uint16_t)(0x10 + bar * 4);
    orig_low = pci_read_config32(dev->bus, dev->slot, dev->func, offset);
    pci_write_config32(dev->bus, dev->slot, dev->func, offset, 0xFFFFFFFFU);
    mask_low = pci_read_config32(dev->bus, dev->slot, dev->func, offset);
    pci_write_config32(dev->bus, dev->slot, dev->func, offset, orig_low);

    if (type == PCI_BAR_IO) {
        return (size_t)(~(mask_low & ~0x3U) + 1U);
    }

    if (type == PCI_BAR_MEM64 && bar + 1 < PCI_BAR_COUNT) {
        uint32_t orig_high = pci_read_config32(dev->bus, dev->slot, dev->func, (uint16_t)(offset + 4));
        uint32_t mask_high;
        uint64_t mask;

        pci_write_config32(dev->bus, dev->slot, dev->func, (uint16_t)(offset + 4), 0xFFFFFFFFU);
        mask_high = pci_read_config32(dev->bus, dev->slot, dev->func, (uint16_t)(offset + 4));
        pci_write_config32(dev->bus, dev->slot, dev->func, (uint16_t)(offset + 4), orig_high);

        mask = ((uint64_t)mask_high << 32) | (uint64_t)(mask_low & ~0xFU);
        return (size_t)(~mask + 1U);
    }

    return (size_t)(~(mask_low & ~0xFU) + 1U);
}

int pci_request_region(pci_device_t *dev, int bar, const char *name) {
    uintptr_t base;
    size_t size;
    int type;

    if (dev == NULL || bar < 0 || bar >= PCI_BAR_COUNT) {
        return -EINVAL;
    }
    if (dev->bar_resource[bar] != NULL) {
        return 0;
    }

    type = pci_bar_type(dev, bar);
    base = pci_bar_base(dev, bar);
    size = pci_bar_size(dev, bar);
    if (type == PCI_BAR_NONE || base == 0 || size == 0) {
        return -ENODEV;
    }

    if (type == PCI_BAR_IO) {
        dev->bar_resource[bar] = request_region(base, size, name);
    } else {
        dev->bar_resource[bar] = request_mem_region(base, size, name);
    }

    return dev->bar_resource[bar] != NULL ? 0 : -EBUSY;
}

void *pci_iomap(pci_device_t *dev, int bar, size_t max_len) {
    uintptr_t base;
    size_t size;

    if (dev == NULL || pci_bar_type(dev, bar) == PCI_BAR_IO) {
        return NULL;
    }

    base = pci_bar_base(dev, bar);
    size = pci_bar_size(dev, bar);
    if (base == 0 || size == 0) {
        return NULL;
    }
    if (max_len != 0 && size > max_len) {
        size = max_len;
    }
    if (dev->bar_resource[bar] == NULL &&
        pci_request_region(dev, bar, dev->kdev ? dev->kdev->name : "pci") != 0) {
        return NULL;
    }

    return ioremap_resource(dev->bar_resource[bar], size);
}

int pci_get_irq(pci_device_t *dev) {
    uint8_t pin;
    uint8_t line;

    if (dev == NULL) {
        return PCI_IRQ_NONE;
    }

    pin = pci_read_config8(dev->bus, dev->slot, dev->func, 0x3DU);
    line = pci_read_config8(dev->bus, dev->slot, dev->func, 0x3CU);
    if (pin == 0U || line == 0xFFU || line == 0U) {
        return PCI_IRQ_NONE;
    }
    return (int)line;
}

int pci_enable_msi(pci_device_t *dev) {
    int off;
    uint16_t control;

    if (dev == NULL) {
        return -EINVAL;
    }

    off = pci_find_capability(dev, PCI_CAP_ID_MSI);
    if (off == 0) {
        return -ENODEV;
    }

    control = pci_read_config16(dev->bus, dev->slot, dev->func, (uint16_t)(off + 2));
    pci_write_config32(dev->bus, dev->slot, dev->func, (uint16_t)(off + 4), 0xFEE00000U);
    if (control & (1U << 7)) {
        pci_write_config16(dev->bus, dev->slot, dev->func, (uint16_t)(off + 12), 0x40U);
    } else {
        pci_write_config16(dev->bus, dev->slot, dev->func, (uint16_t)(off + 8), 0x40U);
    }
    control |= 0x0001U;
    pci_write_config16(dev->bus, dev->slot, dev->func, (uint16_t)(off + 2), control);
    return 0;
}

int pci_disable_msi(pci_device_t *dev) {
    int off;
    uint16_t control;

    if (dev == NULL) {
        return -EINVAL;
    }

    off = pci_find_capability(dev, PCI_CAP_ID_MSI);
    if (off == 0) {
        return -ENODEV;
    }

    control = pci_read_config16(dev->bus, dev->slot, dev->func, (uint16_t)(off + 2));
    control &= (uint16_t)~0x0001U;
    pci_write_config16(dev->bus, dev->slot, dev->func, (uint16_t)(off + 2), control);
    return 0;
}

int pci_enable_msix(pci_device_t *dev, int nvec) {
    int off;
    uint16_t control;
    uint16_t table_size;

    if (dev == NULL || nvec <= 0) {
        return -EINVAL;
    }

    off = pci_find_capability(dev, PCI_CAP_ID_MSIX);
    if (off == 0) {
        return -ENODEV;
    }

    control = pci_read_config16(dev->bus, dev->slot, dev->func, (uint16_t)(off + 2));
    table_size = (uint16_t)((control & 0x07FFU) + 1U);
    if ((uint16_t)nvec > table_size) {
        return -EINVAL;
    }

    control |= 0x8000U;
    control &= (uint16_t)~0x4000U;
    pci_write_config16(dev->bus, dev->slot, dev->func, (uint16_t)(off + 2), control);
    return 0;
}

int pci_hotplug_add(uint8_t bus, uint8_t slot) {
    uint8_t header_type;
    uint8_t functions;
    int found = 0;

    if (!pci_present()) {
        return 0;
    }
    if (pci_read_config16(bus, slot, 0, 0x00) == 0xFFFFU) {
        return 0;
    }

    header_type = pci_read_config8(bus, slot, 0, 0x0E);
    functions = (header_type & 0x80U) ? 8U : 1U;

    for (uint8_t func = 0; func < functions; func++) {
        if (pci_read_config16(bus, slot, func, 0x00) == 0xFFFFU) {
            continue;
        }
        if (pci_find_bdf(bus, slot, func) == NULL &&
            pci_record_device(bus, slot, func) != NULL) {
            found++;
        }
    }

    return found;
}

int pci_hotplug_remove(pci_device_t *dev) {
    return pci_remove_device(dev);
}

void pci_hotplug_poll(void) {
    pci_device_t *curr = pci_devices_head;

    if (!pci_present()) {
        return;
    }

    while (curr != NULL) {
        pci_device_t *next = curr->next;
        if (pci_read_config16(curr->bus, curr->slot, curr->func, 0x00) == 0xFFFFU) {
            (void)pci_hotplug_remove(curr);
        }
        curr = next;
    }

    for (uint16_t bus = 0; bus < 256; bus++) {
        for (uint8_t slot = 0; slot < 32; slot++) {
            (void)pci_hotplug_add((uint8_t)bus, slot);
        }
    }
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

pci_device_t *pci_first_device(void) {
    return pci_devices_head;
}

pci_device_t *pci_next_device(pci_device_t *dev) {
    return dev ? dev->next : NULL;
}

size_t pci_dump_devices(char *buf, size_t size) {
    pci_device_t *dev;
    size_t off = 0;

    if (buf == NULL || size == 0) {
        return 0;
    }

    for (dev = pci_first_device(); dev != NULL; dev = pci_next_device(dev)) {
        int ret = snprintf(off < size ? buf + off : NULL,
                           off < size ? size - off : 0,
                           "%02x:%02x.%u %04x:%04x class=%04x irq=%d\n",
                           (unsigned int)dev->bus,
                           (unsigned int)dev->slot,
                           (unsigned int)dev->func,
                           (unsigned int)dev->vendor_id,
                           (unsigned int)dev->device_id,
                           (unsigned int)dev->class_code,
                           pci_get_irq(dev));
        if (ret > 0) {
            off += (size_t)ret;
        }
    }

    return off;
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
