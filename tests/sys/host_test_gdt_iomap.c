#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <arch/i386/percpu.h>
#include <arch/i386/gdt.h>

static struct percpu_data cpu0;

struct percpu_data *percpu_get(void) {
    return &cpu0;
}

struct percpu_data *percpu_get_cpu(int cpu_id) {
    return cpu_id == 0 ? &cpu0 : NULL;
}

void gdt_flush(uint32_t gdt_ptr) {
    (void)gdt_ptr;
}

void tss_flush(void) {
}

#define HOST_TEST 1
#include "../../sys/arch/i386/gdt.c"

int main(void) {
    memset(&cpu0, 0, sizeof(cpu0));

    gdt_init();
    tss_iomap_init();
    assert(cpu0.tss.iomap[0] == 0xFF);
    assert(cpu0.tss.iomap_end == 0xFF);

    tss_set_iomap(0x3F8, 1);
    assert((cpu0.tss.iomap[0x3F8 / 8] & (1U << (0x3F8 % 8))) == 0);

    tss_set_iomap(0x3F8, 0);
    assert((cpu0.tss.iomap[0x3F8 / 8] & (1U << (0x3F8 % 8))) != 0);

    tss_set_iomap_range(0x60, 0x64, 1);
    for (int port = 0x60; port <= 0x64; port++) {
        assert((cpu0.tss.iomap[port / 8] & (1U << (port % 8))) == 0);
    }

    puts("host_test_gdt_iomap: PASS");
    return 0;
}
