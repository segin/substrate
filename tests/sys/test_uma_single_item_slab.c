/*
 * A zone whose slabs hold one item each -- any zone of items larger than
 * half a page, and the 64K zone a full-size LDT comes from -- with no
 * per-CPU cache in front of it, so that every allocation and free goes
 * to the slab layer.
 *
 * A slab that holds one item is full as soon as it is made.  It was put
 * on the zone's list of partly-used slabs and left there; freeing its
 * item then took it for one on the list of full slabs, failed to find it
 * there, linked it to itself, and freed its header with the zone still
 * pointing at it.  The next allocation used the freed header.  That was
 * the panic in uma_slab_unlink() when a process with a full-size LDT had
 * forked a few times and its children were reaped.
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

#include <vm/uma.h>
#include "../../sys/vm/uma_core.c"
#include "../../sys/vm/uma_debug.c"

static int failed;

#define CHECK(cond, what) do { \
    if (!(cond)) { \
        fprintf(stderr, "FAIL: %s\n", what); \
        failed = 1; \
    } \
} while (0)

static unsigned list_len(const uma_slab_t *s) {
    unsigned n = 0;

    /* A list that loops is a failure, not a hang. */
    for (; s != NULL && n < 1000; s = s->us_next) {
        n++;
    }
    return n;
}

/* Every slab of the zone is on the list its count of free items says. */
static void check_lists(const uma_zone_t *zone, unsigned want_full, const char *when) {
    const uma_slab_t *s;
    char what[128];

    snprintf(what, sizeof(what), "%s: %u full slabs, not %u", when, list_len(zone->uz_full_slabs), want_full);
    CHECK(list_len(zone->uz_full_slabs) == want_full, what);
    snprintf(what, sizeof(what), "%s: a slab is on the partly-used list", when);
    CHECK(zone->uz_part_slabs == NULL, what);
    for (s = zone->uz_full_slabs; s != NULL; s = s->us_next) {
        snprintf(what, sizeof(what), "%s: a slab on the full list has free items", when);
        CHECK(s->us_freecount == 0, what);
    }
}

static void run(size_t item_size, const char *name) {
    uma_zone_t *zone = uma_zcreate(name, item_size, NULL, NULL, NULL, NULL, 16, UMA_ZONE_NOBUCKET);
    void *a, *b, *c;
    int round;

    if (zone == NULL) {
        fprintf(stderr, "FAIL: no zone %s\n", name);
        failed = 1;
        return;
    }
    CHECK(zone->uz_ipers == 1, "one item to a slab");

    /* One item, many times: what a process with such an LDT does as it
     * forks and its children exit. */
    for (round = 0; round < 8; ++round) {
        a = uma_zalloc(zone, 0);
        CHECK(a != NULL, "an item");
        check_lists(zone, 1, "one item out");
        memset(a, 0xa5, item_size);
        uma_zfree(zone, a);
        check_lists(zone, 0, "none out");
    }

    /* Several at once, freed in each order. */
    a = uma_zalloc(zone, 0);
    b = uma_zalloc(zone, 0);
    c = uma_zalloc(zone, 0);
    CHECK(a != NULL && b != NULL && c != NULL, "three items");
    CHECK(a != b && b != c && a != c, "three different items");
    check_lists(zone, 3, "three out");
    uma_zfree(zone, b);
    check_lists(zone, 2, "the middle one back");
    uma_zfree(zone, c);
    check_lists(zone, 1, "the last one back");
    b = uma_zalloc(zone, 0);
    CHECK(b != NULL, "one more");
    check_lists(zone, 2, "two out again");
    uma_zfree(zone, a);
    uma_zfree(zone, b);
    check_lists(zone, 0, "all back");

    uma_zdestroy(zone);
}

int main(void) {
    uma_startup();
    uma_enable_dynamic_alloc();

    run(4096, "one-page");
    run(65536, "ldt-sized");

    if (failed) {
        return 1;
    }
    printf("PASS: slabs of one item go on and off the zone's lists as they fill and empty\n");
    return 0;
}
