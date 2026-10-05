#ifndef _ARCH_I386_CPU_H
#define _ARCH_I386_CPU_H

#include <stdint.h>

struct i386_cpu_features {
    uint8_t detected;
    uint8_t is_486_or_newer;
    uint8_t has_cpuid;
    uint8_t has_cr4;
    uint8_t has_tsc;
    uint8_t has_apic;
    uint8_t has_pse;
    uint8_t has_pae;
    uint8_t has_pge;
    uint8_t has_fxsr;
    uint8_t has_xsave;
    uint8_t has_msr;
    uint8_t has_pat;
    uint8_t pat_wc_enabled;
    uint8_t has_pcid;
    uint8_t has_rdrand;
    uint8_t has_rdseed;
    uint32_t family;
    uint32_t model;
    uint32_t stepping;
    char vendor[13];
};

void i386_cpu_init_early(void);
const struct i386_cpu_features *i386_cpu_get_features(void);

int i386_cpu_is_486_or_newer(void);
int i386_cpu_has_cpuid(void);
int i386_cpu_has_cr4(void);
int i386_cpu_has_tsc(void);
int i386_cpu_has_apic(void);
int i386_cpu_has_pse(void);
int i386_cpu_has_pae(void);
int i386_cpu_has_pge(void);
int i386_cpu_has_fxsr(void);
int i386_cpu_has_xsave(void);
/* CPUID(leaf, subleaf) into eax, ebx, ecx, edx.  The caller must know the
 * CPU has CPUID and that the leaf is within its range. */
void i386_cpuid(uint32_t leaf, uint32_t subleaf, uint32_t *eax, uint32_t *ebx,
                uint32_t *ecx, uint32_t *edx);
/* The highest basic CPUID leaf, 0 if the CPU has no CPUID. */
uint32_t i386_cpuid_max_basic(void);
int i386_cpu_has_msr(void);
int i386_cpu_has_pat(void);
int i386_cpu_pat_wc_enabled(void);
int i386_cpu_has_pcid(void);
int i386_cpu_has_rdrand(void);
int i386_cpu_has_rdseed(void);

uint64_t i386_cpu_cycle_counter(void);
void i386_cpu_cycle_counter_split(uint32_t *lo, uint32_t *hi);

/*
 * Reset the machine by triple fault: load an empty IDT and raise an
 * exception, which cannot be delivered, nor can the double fault that
 * follows.  The last resort of a reboot, after the keyboard controller
 * has been asked and has not obliged.
 *
 * LIDT reads a limit and a base the size of a pointer -- six bytes on
 * i386, ten in long mode.  Giving it a six-byte operand there made the
 * upper half of the base whatever followed it on the stack, and a
 * non-canonical base is a #GP at the LIDT itself: the 64-bit kernel
 * panicked instead of resetting.
 */
static inline void x86_triple_fault(void) {
    struct {
        uint16_t limit;
        uintptr_t base;
    } __attribute__((packed)) idtr = { 0, 0 };

    __asm__ volatile("lidt %0; int3" : : "m"(idtr));
    for (;;) {
        __asm__ volatile("hlt");
    }
}

#endif
