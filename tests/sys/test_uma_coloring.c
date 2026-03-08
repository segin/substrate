#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

void panic(const char *msg) {
    fprintf(stderr, "PANIC: %s\n", msg);
    exit(1);
}

void kprint(const char *msg) {
    printf("KPRINT: %s", msg);
}

int kprintf(const char *fmt, ...) {
    va_list ap;
    int ret;

    va_start(ap, fmt);
    ret = vprintf(fmt, ap);
    va_end(ap);
    return ret;
}

void *pmm_alloc_block(void) {
    return calloc(1, 4096);
}

void pmm_free_block(void *p) {
    free(p);
}

void *pmm_alloc_contiguous(size_t pages) {
    return calloc(pages, 4096);
}

void pmm_free_contiguous(void *p, size_t pages) {
    (void)pages;
    free(p);
}

int smp_get_cpu_count(void) { return 1; }
int smp_get_cpu_id(void) { return 0; }

void *kzalloc(size_t size) {
    return calloc(1, size);
}

void kfree(void *p, size_t size) {
    (void)size;
    free(p);
}

#include <vm/uma.h>
#include "../../sys/vm/uma_core.c"
#include "../../sys/vm/uma_debug.c"

int main(void) {
    void *items[256];
    uma_slab_t *full_slab;
    uma_slab_t *part_slab;
    uma_zone_t *zone;

    uma_startup();
    uma_enable_dynamic_alloc();

    zone = uma_zcreate("test_color", 24, NULL, NULL, NULL, NULL, 0, 0);
    if (!zone) {
        fprintf(stderr, "FAIL: could not create coloring zone\n");
        return 1;
    }

    if (zone->uz_color_max == 0) {
        fprintf(stderr, "FAIL: zone did not expose any coloring slack\n");
        return 1;
    }

    for (size_t i = 0; i < zone->uz_ipers + 1; i++) {
        items[i] = uma_zalloc(zone, 0);
        if (!items[i]) {
            fprintf(stderr, "FAIL: allocation %zu returned NULL\n", i);
            return 1;
        }
    }

    full_slab = zone->uz_full_slabs;
    part_slab = zone->uz_part_slabs;

    if (!full_slab || !part_slab) {
        fprintf(stderr, "FAIL: expected one full slab and one partial slab\n");
        return 1;
    }

    if ((full_slab->us_offset % zone->uz_align) != 0 ||
        (part_slab->us_offset % zone->uz_align) != 0) {
        fprintf(stderr, "FAIL: slab coloring offsets are not alignment-safe\n");
        return 1;
    }

    if (full_slab->us_offset == part_slab->us_offset) {
        fprintf(stderr, "FAIL: slab coloring did not rotate offsets between slabs\n");
        return 1;
    }

    if (part_slab->us_offset > zone->uz_color_max * zone->uz_align) {
        fprintf(stderr, "FAIL: slab coloring offset exceeded configured range\n");
        return 1;
    }

    printf("PASS: slab coloring rotated offsets across slabs\n");
    return 0;
}
