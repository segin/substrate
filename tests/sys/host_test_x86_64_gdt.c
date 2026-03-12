#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int current_cpu_id;
char stack_top[4096];

int smp_get_cpu_id(void) {
    return current_cpu_id;
}

#define HOST_TEST 1
#include "../../sys/arch/x86_64/gdt.c"

int main(void) {
    memset(per_cpu_gdt, 0, sizeof(per_cpu_gdt));
    memset(per_cpu_tss, 0, sizeof(per_cpu_tss));

    current_cpu_id = 0;
    gdt_init_percpu(0, 0x1111222233334444ULL);

    assert(per_cpu_gdt[0][1].access == (GDT_PRESENT | GDT_DPL0 | GDT_TYPE_CODE));
    assert((per_cpu_gdt[0][1].granularity & GDT_LONG_MODE) != 0);
    assert(per_cpu_gdt[0][3].access == (GDT_PRESENT | GDT_DPL3 | GDT_TYPE_DATA));
    assert(per_cpu_gdt[0][4].access == (GDT_PRESENT | GDT_DPL3 | GDT_TYPE_CODE));

    assert(per_cpu_tss[0].rsp0 == 0x1111222233334444ULL);
    assert(per_cpu_tss[0].ist1 != 0);
    assert(per_cpu_tss[0].ist2 != 0);
    assert(per_cpu_tss[0].ist3 != 0);
    assert(per_cpu_tss[0].iopb_offset == sizeof(struct tss64));

    tss_set_rsp0(0xAABBCCDDEEFF0011ULL);
    assert(tss_get()->rsp0 == 0xAABBCCDDEEFF0011ULL);

    current_cpu_id = 1;
    gdt_init_percpu(1, 0x12345678ULL);
    assert(per_cpu_tss[1].rsp0 == 0x12345678ULL);
    assert(tss_get() == &per_cpu_tss[1]);

    puts("host_test_x86_64_gdt: PASS");
    return 0;
}
