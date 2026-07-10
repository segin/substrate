#ifndef _KERN_ISA_H
#define _KERN_ISA_H

#include <stddef.h>
#include <stdint.h>

#include <kern/bus.h>

struct device;
struct resource;

extern struct bus_type isa_bus_type;

void isa_init(void);
void isa_probe_legacy(void);
void isa_probe_pnp(void);
int isa_port_alive(uint16_t port);
int isa_device_present(const char *name);
size_t isa_dump_devices(char *buf, size_t size);
struct device *isa_first_device(void);
struct device *isa_next_device(struct device *dev);
struct resource *isa_device_resource(struct device *dev, uint32_t type, unsigned index);

#ifdef HOST_TEST
/*
 * Host-unit-test I/O hooks.  The real kernel build routes port access
 * through <arch/x86-common/io.h> (inb/outb); the host test harness
 * (tests/sys/host_test_isa.c) supplies these shims instead.  Declared
 * here so isa.c carries no manual extern prototypes.
 */
uint8_t isa_test_inb(uint16_t port);
void isa_test_outb(uint16_t port, uint8_t value);
#endif

#endif
