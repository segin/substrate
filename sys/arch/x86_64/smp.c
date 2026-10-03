/*
 * smp.c - x86_64 processor discovery
 *
 * The 64-bit kernel runs on the boot processor only for now: discovery
 * reports one CPU, and no application processor is started.  (The i386
 * kernel's own APs are parked as well; see arch/i386/smp_discovery.c.)
 */
#include <stdint.h>

#include <arch/x86_64/smp.h>
#include <kern/console.h>

cpu_info_t cpus[MAX_CPUS];
int cpu_count = 1;

void smp_init(void) {
}

void smp_discover_cores(void) {
    cpus[0].lapic_id = 0;
    cpus[0].processor_id = 0;
    cpus[0].flags = 1;
    cpu_count = 1;
}

int smp_get_cpu_count(void) {
    return cpu_count;
}

int smp_get_cpu_id(void) {
    return 0;
}

int smp_boot_ap(uint8_t apic_id) {
    (void)apic_id;
    return -1;
}

void smp_boot_all_aps(void) {
}
