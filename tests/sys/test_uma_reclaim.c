#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

static int reclaim_calls = 0;

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

static void reclaim_cb(void) {
    reclaim_calls++;
}

int main(void) {
    void *item;
    uma_zone_t *zone;

    reclaim_calls = 0;
    uma_startup();
    uma_enable_dynamic_alloc();

    zone = uma_zcreate("test_reclaim", 64, NULL, NULL, NULL, NULL, 0, 0);
    if (!zone) {
        fprintf(stderr, "FAIL: could not create reclaim zone\n");
        return 1;
    }

    uma_zone_set_reclaim(zone, reclaim_cb);

    item = uma_zalloc(zone, 0);
    if (!item) {
        fprintf(stderr, "FAIL: alloc returned NULL\n");
        return 1;
    }
    uma_zfree(zone, item);

    uma_reclaim();

    if (reclaim_calls != 1) {
        fprintf(stderr, "FAIL: reclaim callback count=%d, expected 1\n", reclaim_calls);
        return 1;
    }

    if (zone->uz_free_slabs != NULL) {
        fprintf(stderr, "FAIL: free slab list not drained during reclaim\n");
        return 1;
    }

    printf("PASS: reclaim callback ran and free slabs were drained\n");
    return 0;
}
