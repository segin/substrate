#include <sys/ldt.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    gdt_entry_t ldt[4];
    uintptr_t linear = 0;

    memset(ldt, 0, sizeof(ldt));
    ldt[1].limit_low = 0x3FFFU;
    ldt[1].base_low = 0x0000U;
    ldt[1].base_middle = 0x03U;
    ldt[1].access = 0xF2U;
    ldt[1].granularity = 0x00U;
    ldt[1].base_high = 0x00U;

    if (ldt_translate_selector_offset(ldt, 4, (uint16_t)((1U << 3) | 4U | 3U),
                                      0x0123U, &linear) != 0) {
        fprintf(stderr, "FAIL: valid ELKS pointer translation rejected\n");
        return 1;
    }
    if (linear != 0x00030123U) {
        fprintf(stderr, "FAIL: ELKS pointer translation wrong: 0x%08lx\n",
                (unsigned long)linear);
        return 1;
    }
    if (ldt_translate_selector_offset(ldt, 4, (uint16_t)((1U << 3) | 4U | 3U),
                                      0x4000U, &linear) != -EFAULT) {
        fprintf(stderr, "FAIL: ELKS out-of-bounds offset not rejected\n");
        return 1;
    }
    if (ldt_translate_selector_offset(ldt, 4, (uint16_t)((1U << 3) | 3U),
                                      0x0010U, &linear) != -EINVAL) {
        fprintf(stderr, "FAIL: GDT selector accepted as ELKS LDT pointer\n");
        return 1;
    }

    puts("host_test_elks_ptr_translation: ok");
    return 0;
}
