#include <kern/isa.h>

#include <kern/console.h>
#include <kern/device.h>
#include <kern/isapnp.h>
#include <kern/resource.h>
#include <stdio.h>
#include <string.h>
#include <vm/vm_kmem.h>

#ifndef HOST_TEST
#include <arch/x86-common/io.h>
#define isa_inb(port) inb(port)
#define isa_outb(port, value) outb((port), (value))
#else
/* isa_test_inb/isa_test_outb are declared in <kern/isa.h> under HOST_TEST. */
#define isa_inb(port) isa_test_inb(port)
#define isa_outb(port, value) isa_test_outb((port), (value))
#endif

typedef int (*isa_probe_fn_t)(uint16_t base);

typedef struct isa_legacy_entry {
    const char *name;
    uint16_t base;
    uint16_t span;
    isa_probe_fn_t probe;
} isa_legacy_entry_t;

struct bus_type isa_bus_type = {
    .name = "isa",
};

static int isa_bus_initialized;
static int isa_pnp_probed;
static void isa_ensure_init(void);

static int isa_probe_default(uint16_t base) {
    return isa_port_alive(base);
}

static int isa_probe_uart(uint16_t base) {
    uint8_t old;
    uint8_t probe;

    old = isa_inb((uint16_t)(base + 7));
    isa_outb((uint16_t)(base + 7), 0x5A);
    probe = isa_inb((uint16_t)(base + 7));
    isa_outb((uint16_t)(base + 7), old);
    return probe == 0x5A;
}

static int isa_probe_lpt(uint16_t base) {
    return isa_inb((uint16_t)(base + 1)) != 0xFF;
}

static int isa_probe_ide(uint16_t base) {
    return isa_inb((uint16_t)(base + 7)) != 0xFF;
}

static int isa_probe_ps2(uint16_t base) {
    return isa_inb((uint16_t)(base + 4)) != 0xFF;
}

static int isa_probe_floppy(uint16_t base) {
    return isa_inb((uint16_t)(base + 4)) != 0xFF;
}

static isa_legacy_entry_t isa_legacy_table[] = {
    { "serial0", 0x3F8, 8, isa_probe_uart },
    { "serial1", 0x2F8, 8, isa_probe_uart },
    { "serial2", 0x3E8, 8, isa_probe_uart },
    { "serial3", 0x2E8, 8, isa_probe_uart },
    { "parallel0", 0x378, 8, isa_probe_lpt },
    { "parallel1", 0x278, 8, isa_probe_lpt },
    { "parallel2", 0x3BC, 8, isa_probe_lpt },
    { "ide-primary", 0x1F0, 8, isa_probe_ide },
    { "ide-secondary", 0x170, 8, isa_probe_ide },
    { "ide-tertiary", 0x1E8, 8, isa_probe_ide },
    { "ide-quaternary", 0x168, 8, isa_probe_ide },
    { "floppy-primary", 0x3F0, 8, isa_probe_floppy },
    { "floppy-secondary", 0x370, 8, isa_probe_floppy },
    { "ps2", 0x60, 8, isa_probe_ps2 },
    { NULL, 0, 0, NULL },
};

static struct device *isa_find_device_by_name(const char *name) {
    struct device *curr = isa_bus_type.devices_list;

    while (curr != NULL) {
        if (strcmp(curr->name, name) == 0) {
            return curr;
        }
        curr = curr->bus_next;
    }
    return NULL;
}

static int isa_attach_resource_copy(struct device *dev, uint32_t type,
                                    resource_size_t start,
                                    resource_size_t length,
                                    const char *name) {
    struct resource *res;

    if (dev == NULL || length == 0) {
        return -1;
    }

    res = kmalloc(sizeof(*res));
    if (res == NULL) {
        return -1;
    }

    memset(res, 0, sizeof(*res));
    res->type = type;
    res->start = start;
    res->end = start + length - 1;
    res->name = name;
    res->owner = dev;
    res->sibling = dev->resources;
    dev->resources = res;
    return 0;
}

static void isa_set_pnp_compatibles(struct device *dev,
                                    const isapnp_logical_device_t *logical) {
    char *compat;
    size_t total = strlen(logical->id) + 2;
    size_t used = 0;
    size_t i;

    if (dev == NULL || logical == NULL) {
        return;
    }

    for (i = 0; i < logical->compat_count; i++) {
        char id[8];

        isapnp_eisa_id_to_string(logical->compat_ids[i], id);
        total += strlen(id) + 1;
    }

    compat = kmalloc(total);
    if (compat == NULL) {
        return;
    }

    strlcpy(compat + used, logical->id, total - used);
    used += strlen(compat + used) + 1;
    for (i = 0; i < logical->compat_count; i++) {
        char id[8];

        isapnp_eisa_id_to_string(logical->compat_ids[i], id);
        if (total > used) {
            strlcpy(compat + used, id, total - used);
            used += strlen(compat + used) + 1;
        }
    }

    if (total > used) {
        compat[used] = '\0';
    } else {
        compat[total - 1] = '\0';
    }
    dev->compatible = compat;
}

struct resource *isa_device_resource(struct device *dev, uint32_t type, unsigned index) {
    struct resource *res = dev ? dev->resources : NULL;

    while (res != NULL) {
        if (res->type == type) {
            if (index == 0) {
                return res;
            }
            index--;
        }
        res = res->sibling;
    }

    return NULL;
}

static void isa_ensure_init(void) {
    if (isa_bus_initialized) {
        return;
    }

    memset(&isa_bus_type.lock, 0, sizeof(isa_bus_type.lock));
    spinlock_init(&isa_bus_type.lock, "isa_bus");
    isa_bus_type.name = "isa";
    isa_bus_type.match = NULL;
    isa_bus_type.probe = NULL;
    isa_bus_type.remove = NULL;
    isa_bus_type.devices_list = NULL;
    isa_bus_type.drivers_list = NULL;
    isa_bus_type.next_registered = NULL;
    (void)bus_register_type(&isa_bus_type);
    isa_bus_initialized = 1;
}

int isa_device_present(const char *name) {
    if (name == NULL || *name == '\0') {
        return 0;
    }

    isa_ensure_init();
    return isa_find_device_by_name(name) != NULL;
}

void isa_init(void) {
    isa_ensure_init();
    isapnp_init();
}

int isa_port_alive(uint16_t port) {
    return isa_inb(port) != 0xFF;
}

void isa_probe_legacy(void) {
    size_t i;

    isa_ensure_init();
    for (i = 0; isa_legacy_table[i].name != NULL; i++) {
        struct device *dev;
        int present;

        if (resource_find(RES_IO, isa_legacy_table[i].base,
                          isa_legacy_table[i].span) != NULL) {
            continue;
        }

        present = isa_legacy_table[i].probe
            ? isa_legacy_table[i].probe(isa_legacy_table[i].base)
            : isa_probe_default(isa_legacy_table[i].base);
        if (!present) {
            continue;
        }
        if (isa_find_device_by_name(isa_legacy_table[i].name) != NULL) {
            continue;
        }

        dev = device_create(isa_legacy_table[i].name, NULL);
        if (dev == NULL) {
            continue;
        }
        dev->class = 0;
        dev->subclass = 0;
        dev->vendor_id = 0;
        dev->device_id = isa_legacy_table[i].base;
        if (device_register(dev, &isa_bus_type) != 0) {
            device_put(dev);
            continue;
        }
        kprintf("isa: detected %s at 0x%x\n", isa_legacy_table[i].name, isa_legacy_table[i].base);
    }
}

void isa_probe_pnp(void) {
    int cards;
    int csn;

    isa_ensure_init();
    if (isa_pnp_probed) {
        return;
    }

    cards = isapnp_isolate();
    if (cards <= 0) {
        isa_pnp_probed = 1;
        return;
    }

    for (csn = 1; csn <= cards; csn++) {
        isapnp_device_t card;
        unsigned logical_index;

        if (isapnp_read_resources((uint8_t)csn, &card) != 0) {
            continue;
        }
        if (isapnp_activate(&card) != 0) {
            continue;
        }

        for (logical_index = 0; logical_index < card.logical_count; logical_index++) {
            isapnp_logical_device_t *logical = &card.logical[logical_index];
            struct device *dev;
            char name[32];
            unsigned idx;

            snprintf(name, sizeof(name), "pnp-%s-%u-%u",
                     logical->id,
                     (unsigned)csn,
                     (unsigned)logical->logical_device);
            if (isa_find_device_by_name(name) != NULL) {
                continue;
            }

            dev = device_create(name, NULL);
            if (dev == NULL) {
                continue;
            }

            dev->vendor_id = logical->vendor_id;
            dev->device_id = logical->device_id;
            snprintf(dev->serial, sizeof(dev->serial),
                     "isapnp-%02u-%02u-%08x",
                     (unsigned)csn,
                     (unsigned)logical->logical_device,
                     (unsigned)card.serial);
            isa_set_pnp_compatibles(dev, logical);

            for (idx = 0; idx < logical->io_count; idx++) {
                if (logical->io[idx].base != 0 && logical->io[idx].length != 0) {
                    (void)isa_attach_resource_copy(dev, RES_IO,
                                                   logical->io[idx].base,
                                                   logical->io[idx].length,
                                                   name);
                }
            }
            for (idx = 0; idx < logical->irq_count; idx++) {
                if (logical->irq[idx].irq != 0xFFU) {
                    (void)isa_attach_resource_copy(dev, RES_IRQ,
                                                   logical->irq[idx].irq,
                                                   1,
                                                   name);
                }
            }
            for (idx = 0; idx < logical->dma_count; idx++) {
                if (logical->dma[idx].channel != 0xFFU) {
                    (void)isa_attach_resource_copy(dev, RES_DMA,
                                                   logical->dma[idx].channel,
                                                   1,
                                                   name);
                }
            }
            for (idx = 0; idx < logical->mem_count; idx++) {
                if (logical->mem[idx].base != 0 && logical->mem[idx].length != 0) {
                    (void)isa_attach_resource_copy(dev, RES_MEM,
                                                   logical->mem[idx].base,
                                                   logical->mem[idx].length,
                                                   name);
                }
            }

            if (device_register(dev, &isa_bus_type) != 0) {
                device_put(dev);
                continue;
            }

            kprintf("isa-pnp: detected %s (%s)\n", name, logical->id);
        }
    }

    isa_pnp_probed = 1;
}

struct device *isa_first_device(void) {
    isa_ensure_init();
    return isa_bus_type.devices_list;
}

struct device *isa_next_device(struct device *dev) {
    if (dev == NULL) {
        return NULL;
    }
    return dev->bus_next;
}

size_t isa_dump_devices(char *buf, size_t size) {
    size_t off = 0;
    struct device *dev;

    if (buf == NULL || size == 0) {
        return 0;
    }

    isa_ensure_init();
    dev = isa_bus_type.devices_list;
    while (dev != NULL) {
        int ret = snprintf(off < size ? buf + off : NULL,
                           off < size ? size - off : 0,
                           "%s\n",
                           dev->name[0] ? dev->name : "(unnamed)");
        if (ret > 0) {
            off += (size_t)ret;
        }
        dev = dev->bus_next;
    }
    return off;
}
