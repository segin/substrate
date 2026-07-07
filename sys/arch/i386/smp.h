#ifndef _ARCH_I386_SMP_H
#define _ARCH_I386_SMP_H

#include <stdint.h>
#include <sys/smp.h>

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

#ifndef HOST_TEST
void trampoline_start(void);
void trampoline_end(void);
void trampoline_cr3(void);
void trampoline_cr4(void);
void trampoline_cr0(void);
void trampoline_stack(void);
void trampoline_entry(void);
#endif

#endif
