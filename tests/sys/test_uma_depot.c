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
    void *items[UMA_CACHE_BUCKET_SIZE + 1];
    uma_zone_t *zone;
    uma_cache_t *cache;

    uma_startup();
    uma_enable_dynamic_alloc();

    zone = uma_zcreate("test_depot", 32, NULL, NULL, NULL, NULL, 0, 0);
    if (!zone) {
        fprintf(stderr, "FAIL: could not create depot zone\n");
        return 1;
    }

    cache = &zone->uz_cpu[0];

    for (int i = 0; i < UMA_CACHE_BUCKET_SIZE + 1; i++) {
        items[i] = uma_zalloc(zone, 0);
        if (!items[i]) {
            fprintf(stderr, "FAIL: alloc %d returned NULL\n", i);
            return 1;
        }
    }

    for (int i = 0; i < UMA_CACHE_BUCKET_SIZE + 1; i++) {
        uma_zfree(zone, items[i]);
    }

    if (!uma_bucket_full_depot || uma_bucket_full_depot->ub_zone != zone ||
        uma_bucket_full_depot->ub_cnt != UMA_CACHE_BUCKET_SIZE) {
        fprintf(stderr, "FAIL: full bucket was not published to depot\n");
        return 1;
    }

    if (!cache->uc_freebucket || cache->uc_freebucket->ub_cnt != 1) {
        fprintf(stderr, "FAIL: CPU free bucket did not retain spill item\n");
        return 1;
    }

    if (!uma_zalloc(zone, 0)) {
        fprintf(stderr, "FAIL: first depot reuse allocation failed\n");
        return 1;
    }

    if (!uma_zalloc(zone, 0)) {
        fprintf(stderr, "FAIL: second depot reuse allocation failed\n");
        return 1;
    }

    if (uma_bucket_full_depot != NULL) {
        fprintf(stderr, "FAIL: depot bucket was not consumed back into alloc path\n");
        return 1;
    }

    if (!cache->uc_allocbucket || cache->uc_allocbucket->ub_cnt != UMA_CACHE_BUCKET_SIZE - 1) {
        fprintf(stderr, "FAIL: alloc path did not pull expected full bucket from depot\n");
        return 1;
    }

    printf("PASS: depot recycles full magazines back into the alloc path\n");
    return 0;
}
