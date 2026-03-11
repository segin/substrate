#include <arch/i386/cpu.h>
#include <kern/console.h>
#include <stdio.h>
#include <string.h>

static struct i386_cpu_features cpu_features;
static volatile uint32_t cpu_cycle_fallback = 0;

static uint32_t i386_read_eflags(void) {
    uint32_t flags;
    __asm__ volatile("pushfl; popl %0" : "=r"(flags));
    return flags;
}

static void i386_write_eflags(uint32_t flags) {
    __asm__ volatile("pushl %0; popfl" :: "r"(flags) : "cc");
}

static int i386_eflags_bit_toggle_supported(uint32_t mask) {
    uint32_t before = i386_read_eflags();
    uint32_t after;

    i386_write_eflags(before ^ mask);
    after = i386_read_eflags();
    i386_write_eflags(before);

    return ((before ^ after) & mask) != 0;
}

static void i386_cpuid_leaf(uint32_t leaf, uint32_t subleaf,
                            uint32_t *eax, uint32_t *ebx,
                            uint32_t *ecx, uint32_t *edx) {
    __asm__ volatile("cpuid"
                     : "=a"(*eax), "=b"(*ebx), "=c"(*ecx), "=d"(*edx)
                     : "a"(leaf), "c"(subleaf));
}

void i386_cpu_init_early(void) {
    if (cpu_features.detected) {
        return;
    }

    memset(&cpu_features, 0, sizeof(cpu_features));
    strcpy(cpu_features.vendor, "unknown");

    cpu_features.is_486_or_newer = i386_eflags_bit_toggle_supported(1u << 18);
    cpu_features.has_cpuid = i386_eflags_bit_toggle_supported(1u << 21);

	    if (cpu_features.has_cpuid) {
	        uint32_t eax, ebx, ecx, edx;
	        uint32_t max_basic = 0;
	        uint32_t base_family;
        uint32_t ext_family;
        uint32_t base_model;
        uint32_t ext_model;

        i386_cpuid_leaf(0, 0, &eax, &ebx, &ecx, &edx);
        max_basic = eax;
        memcpy(cpu_features.vendor + 0, &ebx, sizeof(ebx));
        memcpy(cpu_features.vendor + 4, &edx, sizeof(edx));
        memcpy(cpu_features.vendor + 8, &ecx, sizeof(ecx));
        cpu_features.vendor[12] = '\0';

        if (max_basic >= 1) {
            i386_cpuid_leaf(1, 0, &eax, &ebx, &ecx, &edx);

            base_family = (eax >> 8) & 0x0F;
            ext_family = (eax >> 20) & 0xFF;
            base_model = (eax >> 4) & 0x0F;
            ext_model = (eax >> 16) & 0x0F;

            cpu_features.family = base_family;
            if (base_family == 0x0F) {
                cpu_features.family += ext_family;
            }

	            cpu_features.model = base_model;
	            if (base_family == 0x06 || base_family == 0x0F) {
	                cpu_features.model |= (ext_model << 4);
	            }
	            cpu_features.stepping = eax & 0x0F;

	            if (cpu_features.family >= 5) {
	                cpu_features.has_cr4 = 1;
	            }

	            cpu_features.has_tsc = (edx >> 4) & 1u;
	            cpu_features.has_apic = (edx >> 9) & 1u;
	            cpu_features.has_pse = cpu_features.has_cr4 && ((edx >> 3) & 1u);
	            cpu_features.has_pae = cpu_features.has_cr4 && ((edx >> 6) & 1u);
	            cpu_features.has_pge = cpu_features.has_cr4 && ((edx >> 13) & 1u);
	            cpu_features.has_fxsr = (edx >> 24) & 1u;
	            cpu_features.has_pcid = cpu_features.has_cr4 && ((ecx >> 17) & 1u);
	            cpu_features.has_rdrand = (ecx >> 30) & 1u;
	        }

        if (max_basic >= 7) {
            i386_cpuid_leaf(7, 0, &eax, &ebx, &ecx, &edx);
            cpu_features.has_rdseed = (ebx >> 18) & 1u;
        }
    } else if (cpu_features.is_486_or_newer) {
        cpu_features.family = 4;
        strcpy(cpu_features.vendor, "i486");
    } else {
        cpu_features.family = 3;
        strcpy(cpu_features.vendor, "i386");
    }

    cpu_features.detected = 1;

    kprint("CPU: ");
    kprint(cpu_features.vendor);
    kprint(" family ");
    {
        char buf[16];
        snprintf(buf, sizeof(buf), "%u", cpu_features.family);
        kprint(buf);
    }
    if (!cpu_features.has_cpuid) {
        kprint(" (no CPUID");
        if (!cpu_features.is_486_or_newer) {
            kprint(", pre-486");
        }
        kprint(")\n");
    } else {
        kprint(" model ");
        {
            char buf[16];
            snprintf(buf, sizeof(buf), "%u", cpu_features.model);
            kprint(buf);
        }
        kprint(" stepping ");
        {
            char buf[16];
            snprintf(buf, sizeof(buf), "%u", cpu_features.stepping);
            kprint(buf);
        }
        kprint("\n");
    }
}

const struct i386_cpu_features *i386_cpu_get_features(void) {
    return &cpu_features;
}

int i386_cpu_is_486_or_newer(void) { return cpu_features.is_486_or_newer; }
int i386_cpu_has_cpuid(void) { return cpu_features.has_cpuid; }
int i386_cpu_has_cr4(void) { return cpu_features.has_cr4; }
int i386_cpu_has_tsc(void) { return cpu_features.has_tsc; }
int i386_cpu_has_apic(void) { return cpu_features.has_apic; }
int i386_cpu_has_pse(void) { return cpu_features.has_pse; }
int i386_cpu_has_pae(void) { return cpu_features.has_pae; }
int i386_cpu_has_pge(void) { return cpu_features.has_pge; }
int i386_cpu_has_fxsr(void) { return cpu_features.has_fxsr; }
int i386_cpu_has_pcid(void) { return cpu_features.has_pcid; }
int i386_cpu_has_rdrand(void) { return cpu_features.has_rdrand; }
int i386_cpu_has_rdseed(void) { return cpu_features.has_rdseed; }

uint64_t i386_cpu_cycle_counter(void) {
    if (cpu_features.has_tsc) {
        uint64_t tsc;
        __asm__ volatile("rdtsc" : "=A"(tsc));
        return tsc;
    }

    return ++cpu_cycle_fallback;
}

void i386_cpu_cycle_counter_split(uint32_t *lo, uint32_t *hi) {
    uint64_t value = i386_cpu_cycle_counter();

    if (lo) {
        *lo = (uint32_t)value;
    }
    if (hi) {
        *hi = (uint32_t)(value >> 32);
    }
}
