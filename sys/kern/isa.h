#ifndef _KERN_ISA_H
#define _KERN_ISA_H

#include <stddef.h>
#include <stdint.h>

#include <kern/bus.h>

struct device;

extern struct bus_type isa_bus_type;

void isa_init(void);
void isa_probe_legacy(void);
int isa_port_alive(uint16_t port);
size_t isa_dump_devices(char *buf, size_t size);
struct device *isa_first_device(void);
struct device *isa_next_device(struct device *dev);

#endif
