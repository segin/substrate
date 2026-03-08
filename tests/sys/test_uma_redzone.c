#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

/* Mock panic to record the failure without aborting the host test. */
static int panic_called = 0;
void panic(const char *msg) {
    printf("PANIC CALLED: %s\n", msg);
    panic_called = 1;
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

int main() {
    uma_startup();
    uma_enable_dynamic_alloc();

    uma_zone_t *zone = uma_zcreate("test_zone", 32, NULL, NULL, NULL, NULL, 0, UMA_ZONE_REDZONE);
    if (!zone) {
        fprintf(stderr, "FAIL: could not create redzone zone\n");
        return 1;
    }

    void *item = uma_zalloc(zone, 0);
    if (!item) {
        fprintf(stderr, "FAIL: could not allocate redzone object\n");
        return 1;
    }

    /* Corrupt the post-redzone and verify the real checker trips panic. */
    uint8_t *post = (uint8_t *)item + zone->uz_size;
    *post = 0x00;
    uma_zfree(zone, item);

    if (!panic_called) {
        fprintf(stderr, "FAIL: redzone corruption did not trigger panic\n");
        return 1;
    }

    printf("PASS: redzone corruption triggered panic path\n");
    return 0;
}
