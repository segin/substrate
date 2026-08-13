#include <stdio.h>
#include <string.h>

#include <arch/i386/pmm.h>
#include <kern/console.h>
#include <kern/device.h>
#include <kern/driver.h>
#include <kern/pci.h>
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

    /* [RF-18] Disable decode around the all-ones probe.  PCI 3.0 s6.2.5.1
     * requires it: while the probe value is in the BAR, the device decodes
     * a bogus window, and if firmware (SMM) or another agent touches the
     * device mid-probe the accesses land nowhere -- the xHCI audit
     * [P6-INIT-01] hit exactly this against a BIOS-owned controller.
     * Doing it here covers every caller, pci_iomap included. */
    uint16_t cmd_save = pci_read_config16(dev->bus, dev->slot, dev->func,
                                          PCI_CONFIG_COMMAND);
    pci_write_config16(dev->bus, dev->slot, dev->func, PCI_CONFIG_COMMAND,
                       cmd_save & (uint16_t)~(PCI_COMMAND_MEMORY | PCI_COMMAND_IO));

    orig_low = pci_read_config32(dev->bus, dev->slot, dev->func, offset);
    pci_write_config32(dev->bus, dev->slot, dev->func, offset, 0xFFFFFFFFU);
    mask_low = pci_read_config32(dev->bus, dev->slot, dev->func, offset);
    pci_write_config32(dev->bus, dev->slot, dev->func, offset, orig_low);

    if (type == PCI_BAR_IO) {
        pci_write_config16(dev->bus, dev->slot, dev->func,
                           PCI_CONFIG_COMMAND, cmd_save);
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
        pci_write_config16(dev->bus, dev->slot, dev->func,
                           PCI_CONFIG_COMMAND, cmd_save);
        return (size_t)(~mask + 1U);
    }

    pci_write_config16(dev->bus, dev->slot, dev->func,
                       PCI_CONFIG_COMMAND, cmd_save);
    return (size_t)(~(mask_low & ~0xFU) + 1U);
}

/* ---- 32-bit MMIO allocation -------------------------------------------
 *
 * Firmware assigns BARs; the kernel normally just reads them.  The exception
 * is a 64-bit BAR placed above 4 GiB, which a 32-bit kernel has no way to
 * map -- UEFI on a large-memory guest does this to the xHCI controller, and
 * the driver then mapped whatever the low dword aliased.  Such a BAR has to
 * be moved into the 32-bit PCI hole before anyone touches the device.
 *
 * The window is derived from the BARs firmware already placed rather than
 * from a memory map: everything it assigned below 4 GiB is by definition in
 * the PCI hole, so allocating above the highest such BAR is both inside MMIO
 * space and free of collisions, without needing to know where RAM ends.  The
 * top bound is the IOAPIC -- 0xFEC00000 and up is the IOAPIC, HPET and local
 * APIC, none of which appear as BARs and none of which may be overlapped.
 */
#define PCI_MMIO32_TOP    0xFEC00000U
/*
 * Floor when no 32-bit BAR exists to infer the hole from -- which is what
 * UEFI leaves when it places every 64-bit-capable BAR high.  This is not a
 * guess: PMM_PHYS_RAM_CAP is the ceiling on physical RAM the kernel will
 * manage, deliberately set at 3 GiB rather than chasing the last fraction
 * below the PCI hole, so nothing at or above it is ever memory.  The two
 * constants are the same number by design; see the comment on the cap.
 */
#define PCI_MMIO32_FLOOR  ((uint32_t)PMM_PHYS_RAM_CAP)
#define PCI_MMIO32_GRAIN  0x00100000U   /* keep assignments 1 MiB-tidy */

static uint32_t pci_mmio32_next;
static int      pci_mmio32_ready;

/* Base of a memory BAR as firmware left it, full width.  Returns 0 for an
 * unassigned or non-memory BAR; *is64 says whether it consumed two slots. */
static uint64_t pci_bar_base64(pci_device_t *dev, int bar, int *is64) {
    int type = pci_bar_type(dev, bar);
    uint32_t lo;
    uint64_t base;

    if (is64) *is64 = 0;
    if (type != PCI_BAR_MEM32 && type != PCI_BAR_MEM64)
        return 0;

    lo = pci_read_config32(dev->bus, dev->slot, dev->func,
                           (uint16_t)(0x10 + bar * 4));
    base = (uint64_t)(lo & ~0xFU);
    if (type == PCI_BAR_MEM64 && bar + 1 < PCI_BAR_COUNT) {
        base |= (uint64_t)pci_read_config32(dev->bus, dev->slot, dev->func,
                                            (uint16_t)(0x10 + (bar + 1) * 4)) << 32;
        if (is64) *is64 = 1;
    }
    return base;
}

static void pci_mmio32_init(void) {
    pci_device_t *dev;
    uint64_t top = 0;

    if (pci_mmio32_ready)
        return;
    pci_mmio32_ready = 1;

    for (dev = pci_first_device(); dev != NULL; dev = pci_next_device(dev)) {
        int bar;
        for (bar = 0; bar < PCI_BAR_COUNT; bar++) {
            int is64 = 0;
            uint64_t base = pci_bar_base64(dev, bar, &is64);
            uint64_t end;
            size_t size;

            if (base != 0 && (base >> 32) == 0) {
                size = pci_bar_size(dev, bar);
                if (size != 0) {
                    end = base + (uint64_t)size;
                    if (end <= PCI_MMIO32_TOP && end > top)
                        top = end;
                }
            }
            if (is64)
                bar++;              /* upper half is not a BAR of its own */
        }
    }

    if (top < PCI_MMIO32_FLOOR)
        top = PCI_MMIO32_FLOOR;
    top = (top + (PCI_MMIO32_GRAIN - 1)) & ~(uint64_t)(PCI_MMIO32_GRAIN - 1);
    pci_mmio32_next = (uint32_t)top;
}

uint32_t pci_alloc_mmio32(uint64_t size, uint64_t align) {
    uint64_t base;

    if (size == 0)
        return 0;
    pci_mmio32_init();

    /* PCI requires a BAR to be naturally aligned to its own size. */
    if (align < size)
        align = size;
    if (align < 0x1000U)
        align = 0x1000U;

    base = ((uint64_t)pci_mmio32_next + (align - 1)) & ~(align - 1);
    if (base + size > PCI_MMIO32_TOP) {
        kprintf("pci: no 32-bit MMIO space left for a %u-byte window "
                "(next=0x%x, top=0x%x)\n",
                (unsigned)size, (unsigned)pci_mmio32_next, PCI_MMIO32_TOP);
        return 0;
    }
    pci_mmio32_next = (uint32_t)(base + size);
    return (uint32_t)base;
}

int pci_relocate_bar32(pci_device_t *dev, int bar) {
    uint16_t off, cmd;
    uint32_t lo, newbase, readback;
    uint64_t size;
    int type;

    if (dev == NULL || bar < 0 || bar >= PCI_BAR_COUNT)
        return -1;
    type = pci_bar_type(dev, bar);
    if (type != PCI_BAR_MEM32 && type != PCI_BAR_MEM64)
        return -1;

    size = (uint64_t)pci_bar_size(dev, bar);
    if (size == 0)
        return -1;

    newbase = pci_alloc_mmio32(size, size);
    if (newbase == 0)
        return -1;

    off = (uint16_t)(0x10 + bar * 4);
    lo  = pci_read_config32(dev->bus, dev->slot, dev->func, off);

    /*
     * Stop the device decoding while the address is in flux -- a half-written
     * 64-bit BAR briefly names an address that belongs to something else.
     */
    cmd = pci_read_config16(dev->bus, dev->slot, dev->func, PCI_CONFIG_COMMAND);
    pci_write_config16(dev->bus, dev->slot, dev->func, PCI_CONFIG_COMMAND,
                       (uint16_t)(cmd & ~0x0002U));

    if (type == PCI_BAR_MEM64 && bar + 1 < PCI_BAR_COUNT)
        pci_write_config32(dev->bus, dev->slot, dev->func,
                           (uint16_t)(off + 4), 0);
    pci_write_config32(dev->bus, dev->slot, dev->func, off,
                       newbase | (lo & 0xFU));

    pci_write_config16(dev->bus, dev->slot, dev->func, PCI_CONFIG_COMMAND, cmd);

    /* Confirm the device actually took it: a BAR can be read-only. */
    readback = pci_read_config32(dev->bus, dev->slot, dev->func, off) & ~0xFU;
    if (readback != newbase) {
        kprintf("pci: %02x:%02x.%u BAR%d would not move to 0x%x "
                "(reads back 0x%x)\n",
                dev->bus, dev->slot, dev->func, bar,
                (unsigned)newbase, (unsigned)readback);
        return -1;
    }

    kprintf("pci: %02x:%02x.%u BAR%d relocated below 4 GiB to 0x%x "
            "(%u KiB)\n", dev->bus, dev->slot, dev->func, bar,
            (unsigned)newbase, (unsigned)(size / 1024));
    return 0;
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

/*
 * Enable MSI on `dev`, routing its message to LAPIC interrupt `vector`
 * (an IDT vector in the dynamic 0x50..0xBF range, allocated via
 * irq_alloc_vector()).  Programs a single-message, fixed-delivery,
 * physical-destination MSI to the BSP.  Returns 0, -ENODEV if the device
 * has no MSI capability, -EINVAL on a bad argument.
 */
int pci_enable_msi(pci_device_t *dev, uint8_t vector) {
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
    /* Request exactly one message vector (Multiple Message Enable = 0). */
    control &= (uint16_t)~(7U << 4);

    /* Message Address: 0xFEE00000 = LAPIC, destination APIC ID 0 (BSP),
     * physical destination, fixed delivery.  Message Data = the vector. */
    pci_write_config32(dev->bus, dev->slot, dev->func, (uint16_t)(off + 4), 0xFEE00000U);
    if (control & (1U << 7)) {          /* 64-bit message address capable */
        pci_write_config32(dev->bus, dev->slot, dev->func, (uint16_t)(off + 8), 0U);
        pci_write_config16(dev->bus, dev->slot, dev->func, (uint16_t)(off + 12), vector);
    } else {
        pci_write_config16(dev->bus, dev->slot, dev->func, (uint16_t)(off + 8), vector);
    }
    control |= 0x0001U;                 /* MSI Enable */
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

pci_device_t *pci_find_device_by_kdev(struct device *kdev) {
    pci_device_t *curr = pci_devices_head;

    while (curr != NULL) {
        if (curr->kdev == kdev) {
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
