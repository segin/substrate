#include <arch/i386/cpu.h>
#include <fs/procfs.h>
#include <sys/smp.h>
#include <kern/console.h>
#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

struct cpuid_feature_desc {
    uint8_t reg; /* 0 = EDX, 1 = ECX */
    uint8_t bit;
    const char *name;
};

static const struct cpuid_feature_desc cpuid_features[] = {
    {0, 0,  "fpu"},
    {0, 1,  "vme"},
    {0, 2,  "de"},
    {0, 3,  "pse"},
    {0, 4,  "tsc"},
    {0, 5,  "msr"},
    {0, 6,  "pae"},
    {0, 7,  "mce"},
    {0, 8,  "cx8"},
    {0, 9,  "apic"},
    {0, 11, "sep"},
    {0, 12, "mtrr"},
    {0, 13, "pge"},
    {0, 14, "mca"},
    {0, 15, "cmov"},
    {0, 16, "pat"},
    {0, 17, "pse36"},
    {0, 19, "clflush"},
    {0, 23, "mmx"},
    {0, 24, "fxsr"},
    {0, 25, "sse"},
    {0, 26, "sse2"},
    {0, 28, "ht"},
    {1, 0,  "sse3"},
    {1, 1,  "pclmulqdq"},
    {1, 9,  "ssse3"},
    {1, 12, "fma"},
    {1, 13, "cx16"},
    {1, 19, "sse4_1"},
    {1, 20, "sse4_2"},
    {1, 23, "popcnt"},
    {1, 25, "aes"},
    {1, 26, "xsave"},
    {1, 28, "avx"},
    {1, 30, "rdrand"},
};

static inline void cpuid_leaf(uint32_t leaf, uint32_t subleaf,
                              uint32_t *eax, uint32_t *ebx,
                              uint32_t *ecx, uint32_t *edx) {
    if (!i386_cpu_has_cpuid()) {
        *eax = 0;
        *ebx = 0;
        *ecx = 0;
        *edx = 0;
        return;
    }
    __asm__ volatile("cpuid"
                     : "=a"(*eax), "=b"(*ebx), "=c"(*ecx), "=d"(*edx)
                     : "a"(leaf), "c"(subleaf));
}

static size_t cpuid_appendf(char *buf, size_t size, size_t off, const char *fmt, ...) {
    if (off >= size) return off;

    va_list ap;
    va_start(ap, fmt);
    int ret = vsnprintf(buf + off, size - off, fmt, ap);
    va_end(ap);

    if (ret <= 0) return off;
    off += (size_t)ret;
    return off;
}

static void cpuid_get_vendor(char vendor[13]) {
    const struct i386_cpu_features *features = i386_cpu_get_features();
    uint32_t eax, ebx, ecx, edx;
    if (!i386_cpu_has_cpuid()) {
        strncpy(vendor, features->vendor, 12);
        vendor[12] = '\0';
        return;
    }

    cpuid_leaf(0, 0, &eax, &ebx, &ecx, &edx);

    memcpy(vendor + 0, &ebx, sizeof(ebx));
    memcpy(vendor + 4, &edx, sizeof(edx));
    memcpy(vendor + 8, &ecx, sizeof(ecx));
    vendor[12] = '\0';
}

static void cpuid_get_brand(char brand[49]) {
    uint32_t eax, ebx, ecx, edx;
    brand[0] = '\0';

    if (!i386_cpu_has_cpuid()) {
        if (i386_cpu_get_features()->family >= 4) {
            strncpy(brand, "i486-compatible CPU", 48);
        } else {
            strncpy(brand, "i386-compatible CPU", 48);
        }
        brand[48] = '\0';
        return;
    }

    cpuid_leaf(0x80000000u, 0, &eax, &ebx, &ecx, &edx);
    if (eax < 0x80000004u) {
        return;
    }

    uint32_t *words = (uint32_t *)brand;
    for (uint32_t leaf = 0; leaf < 3; leaf++) {
        cpuid_leaf(0x80000002u + leaf, 0, &eax, &ebx, &ecx, &edx);
        words[leaf * 4 + 0] = eax;
        words[leaf * 4 + 1] = ebx;
        words[leaf * 4 + 2] = ecx;
        words[leaf * 4 + 3] = edx;
    }
    brand[48] = '\0';

    /* Trim leading spaces in-place. */
    size_t lead = 0;
    while (brand[lead] == ' ') lead++;
    if (lead > 0) {
        memmove(brand, brand + lead, strlen(brand + lead) + 1);
    }
}

static size_t cpuid_append_flags(char *buf, size_t size, size_t off, uint32_t ecx, uint32_t edx) {
    int first = 1;

    for (size_t i = 0; i < (sizeof(cpuid_features) / sizeof(cpuid_features[0])); i++) {
        const struct cpuid_feature_desc *f = &cpuid_features[i];
        uint32_t reg = (f->reg == 0) ? edx : ecx;
        if (((reg >> f->bit) & 1u) == 0) continue;

        off = cpuid_appendf(buf, size, off, "%s%s", first ? "" : " ", f->name);
        first = 0;
    }

    return off;
}

static uint32_t cpuid_proc_cpuinfo(char *buf, size_t size, void *opaque) {
    (void)opaque;

    uint32_t eax, ebx, ecx, edx;
    char vendor[13];
    char brand[49];
    const struct i386_cpu_features *features = i386_cpu_get_features();

    cpuid_get_vendor(vendor);
    cpuid_get_brand(brand);

    if (i386_cpu_has_cpuid()) {
        cpuid_leaf(1, 0, &eax, &ebx, &ecx, &edx);
    } else {
        eax = ebx = ecx = edx = 0;
    }

    uint32_t family = features->family;
    uint32_t model = features->model;
    uint32_t stepping = features->stepping;

    const char *model_name = (brand[0] != '\0') ? brand : "Substrate x86 CPU";

    int ncpu = smp_get_cpu_count();
    if (ncpu < 1) ncpu = 1;

    size_t off = 0;
    for (int cpu = 0; cpu < ncpu; cpu++) {
        off = cpuid_appendf(buf, size, off,
                            "processor\t: %d\n"
                            "vendor_id\t: %s\n"
                            "cpu family\t: %u\n"
                            "model\t\t: %u\n"
                            "stepping\t: %u\n"
                            "model name\t: %s\n"
                            "flags\t\t: ",
                            cpu, vendor, family, model, stepping, model_name);
        off = cpuid_append_flags(buf, size, off, ecx, edx);
        off = cpuid_appendf(buf, size, off, "\n\n");
    }

    if (off >= size) {
        return (uint32_t)size;
    }
    return (uint32_t)off;
}

void cpuid_init(void) {
    if (procfs_register_entry("cpuinfo", cpuid_proc_cpuinfo, NULL) == 0) {
        if (i386_cpu_has_cpuid()) {
            kprint("cpuid: /proc/cpuinfo provider registered\n");
        } else {
            kprint("cpuid: /proc/cpuinfo provider registered (generic fallback)\n");
        }
    } else {
        kprint("cpuid: failed to register /proc/cpuinfo provider\n");
    }
}
