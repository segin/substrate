#ifndef _ARCH_X86_COMMON_MSR_H
#define _ARCH_X86_COMMON_MSR_H

#include <stdint.h>

// Model Specific Registers
#define MSR_EFER    0xC0000080

// EFER bits
#define EFER_SCE    (1 << 0)   // Syscall Enable
#define EFER_LME    (1 << 8)   // Long Mode Enable
#define EFER_LMA    (1 << 10)  // Long Mode Active
#define EFER_NXE    (1 << 11)  // No-Execute Enable

static inline uint64_t rdmsr(uint32_t msr) {
    uint32_t low, high;
    __asm__ volatile("rdmsr" : "=a"(low), "=d"(high) : "c"(msr));
    return ((uint64_t)high << 32) | low;
}

static inline void wrmsr(uint32_t msr, uint64_t val) {
    uint32_t low = val & 0xFFFFFFFF;
    uint32_t high = val >> 32;
    __asm__ volatile("wrmsr" :: "c"(msr), "a"(low), "d"(high));
}

#endif // _ARCH_X86_COMMON_MSR_H
