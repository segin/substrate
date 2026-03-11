#ifndef _PMAP_HAL_H
#define _PMAP_HAL_H

#include <arch/i386/cpu.h>
#include <stdint.h>

// ==================== HAL Layer ====================

#ifndef HOST_TEST
static inline void pmap_hal_invlpg(uintptr_t va) {
    __asm__ volatile("invlpg (%0)" :: "r"(va) : "memory");
}

static inline uint32_t pmap_hal_read_cr3(void) {
    uint32_t cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    return cr3;
}

static inline void pmap_hal_write_cr3(uint32_t cr3) {
    __asm__ volatile("mov %0, %%cr3" :: "r"(cr3) : "memory");
}

static inline uint32_t pmap_hal_read_cr4(void) {
    uint32_t cr4;
    if (!i386_cpu_has_cr4()) {
        return 0;
    }
    __asm__ volatile("mov %%cr4, %0" : "=r"(cr4));
    return cr4;
}

static inline void pmap_hal_write_cr4(uint32_t cr4) {
    if (!i386_cpu_has_cr4()) {
        return;
    }
    __asm__ volatile("mov %0, %%cr4" :: "r"(cr4));
}

static inline void pmap_hal_cpuid(uint32_t code, uint32_t *eax, uint32_t *ebx, uint32_t *ecx, uint32_t *edx) {
    if (!i386_cpu_has_cpuid()) {
        *eax = 0;
        *ebx = 0;
        *ecx = 0;
        *edx = 0;
        return;
    }
    __asm__ volatile("cpuid"
                     : "=a"(*eax), "=b"(*ebx), "=c"(*ecx), "=d"(*edx)
                     : "a"(code));
}
#else
// Host Test Mocks (Weak definitions or externs)
// Tests should define these or rely on weak symbols
void pmap_hal_invlpg(uintptr_t va);
uint32_t pmap_hal_read_cr3(void);
void pmap_hal_write_cr3(uint32_t cr3);
uint32_t pmap_hal_read_cr4(void);
void pmap_hal_write_cr4(uint32_t cr4);
void pmap_hal_cpuid(uint32_t code, uint32_t *eax, uint32_t *ebx, uint32_t *ecx, uint32_t *edx);
#endif

// ===================================================

#endif // _PMAP_HAL_H
