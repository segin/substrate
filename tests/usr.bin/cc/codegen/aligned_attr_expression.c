/*
 * aligned()'s argument is a constant expression, read whole.  cc took the
 * first number at the outer level: Linux's __aligned(PAGE_SIZE), which
 * expands to aligned(((1UL) << 12)), had none and got pointer alignment,
 * so empty_zero_page was not page-aligned, the fixmap test_wp_bit() made
 * of it was garbage, and the kernel wrote it over the page-fault gate.
 * `(1UL) << 6` was taken for 6, and refused.
 */
#include <stdint.h>
#include <stdio.h>

const unsigned char page[((1UL) << 12)] __attribute__((__aligned__(((1UL) << 12))));
unsigned char line[64] __attribute__((aligned((1UL) << 6)));
unsigned char pair[16] __attribute__((aligned(sizeof(long long) * 2)));

int main(void) {
    printf("page %d line %d pair %d\n", (int)((uintptr_t)page % 4096 == 0), (int)((uintptr_t)line % 64 == 0),
           (int)((uintptr_t)pair % 16 == 0));
    printf("alignof %d %d %d\n", (int)_Alignof(page), (int)_Alignof(line), (int)_Alignof(pair));
    return 0;
}
