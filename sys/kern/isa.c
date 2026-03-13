#include <kern/isa.h>

#include <kern/console.h>
#include <kern/device.h>
#include <stdio.h>
#include <string.h>

#ifndef HOST_TEST
#include <arch/x86-common/io.h>
#define isa_inb(port) inb(port)
#define isa_outb(port, value) outb((port), (value))
#else
extern uint8_t isa_test_inb(uint16_t port);
extern void isa_test_outb(uint16_t port, uint8_t value);
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
