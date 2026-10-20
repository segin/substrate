/*
 * A zone of very small items -- small enough that more than 254 of them
 * fit on a slab page.
 *
 * A slab's free chain keeps each item's next index in one byte, and the
 * bytes 0xFE and 0xFF mark items handed to a bucket or to a caller.  The
 * zone took as many items as fit on the page, so an index of 254 or 255
 * read as one of those markers: the chain ended early or an item looked
 * allocated while free, and freeing it was called a double free.  The
 * zone must hold at most 254 to a slab, and every item must come out and
 * go back.
 */
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

/* Pages are aligned as pages are: the allocator finds an item's slab by
 * the page the item is in. */
void *pmm_alloc_block(void) {
    void *p = aligned_alloc(4096, 4096);

    if (p != NULL) {
        memset(p, 0, 4096);
    }
    return p;
}

void pmm_free_block(void *p) {
    free(p);
}

void *pmm_alloc_contiguous(size_t pages) {
    return aligned_alloc(4096, pages * 4096);
}

void pmm_free_contiguous(void *p, size_t pages) {
    (void)pages;
    free(p);
}

int smp_get_cpu_count(void) { return 1; }
int smp_get_cpu_id(void) { return 0; }

uint32_t intr_disable(void) { return 0; }
void intr_restore(uint32_t flags) { (void)flags; }

void *kzalloc(size_t size) {
    return calloc(1, size);
}

void kfree(void *p, size_t size) {
    (void)size;
    free(p);
}

/* uma_core.c compiles its slab lock out under HOST_TEST, but
 * uma_zone_report() still takes it unconditionally. */
static unsigned long uma_slab_lock_acquire(void) { return 0; }
static void uma_slab_lock_release(unsigned long f) { (void)f; }

#include <vm/uma.h>
#include "../../sys/vm/uma_core.c"
#include "../../sys/vm/uma_debug.c"

#define NITEMS 1200

static int failed;

#define CHECK(cond, what) do { \
    if (!(cond)) { \
        fprintf(stderr, "FAIL: %s\n", what); \
        failed = 1; \
    } \
} while (0)

static void *items[NITEMS];

static void run(size_t item_size, const char *name) {
    uma_zone_t *zone = uma_zcreate(name, item_size, NULL, NULL, NULL, NULL, 1, UMA_ZONE_NOBUCKET);
    char what[128];

    if (zone == NULL) {
        fprintf(stderr, "FAIL: no zone %s\n", name);
        failed = 1;
        return;
    }
    snprintf(what, sizeof(what), "%s: %u items to a slab, more than 254", name,
             (unsigned)zone->uz_ipers);
    CHECK(zone->uz_ipers <= 254, what);

    /* Two passes over several slabs' worth: out, all different, back. */
    for (int pass = 0; pass < 2; pass++) {
        for (int i = 0; i < NITEMS; i++) {
            items[i] = uma_zalloc(zone, 0);
            CHECK(items[i] != NULL, "an item");
            if (items[i] != NULL) {
                memset(items[i], 0x5a, item_size);
            }
        }
        for (int i = 1; i < NITEMS; i++) {
            if (items[i] == items[i - 1]) {
                CHECK(0, "the same item twice");
                break;
            }
        }
        for (int i = 0; i < NITEMS; i++) {
            if (items[i] != NULL) {
                uma_zfree(zone, items[i]);
            }
        }
    }
    uma_zdestroy(zone);
}

int main(void) {
    uma_startup();
    uma_enable_dynamic_alloc();

    run(1, "one-byte");
    run(4, "four-byte");
    run(8, "eight-byte");

    if (failed) {
        return 1;
    }
    printf("PASS: zones of tiny items keep their slab indices below the markers\n");
    return 0;
}
