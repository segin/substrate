#ifndef _ARCH_I386_SMP_H
#define _ARCH_I386_SMP_H

#include <stdint.h>

#define MAX_CPUS 32

typedef struct {
    uint8_t lapic_id;
    uint8_t processor_id;
    uint8_t flags;
} cpu_info_t;

extern cpu_info_t cpus[MAX_CPUS];
extern int cpu_count;

void smp_discover_cores(void);
int smp_get_cpu_count(void);
int smp_boot_ap(uint8_t apic_id);
void smp_boot_all_aps(void);

#endif
