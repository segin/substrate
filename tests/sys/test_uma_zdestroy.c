/*
 * Destroying a zone whose items sit in per-CPU and depot buckets, and
 * whose items have a fini hook and redzones.
 *
 * uma_zdestroy freed the slabs and the zone but left the per-CPU buckets
 * installed and the zone's full buckets in the shared depot, still naming
 * the freed zone: a later zone created at the same address took those
 * buckets as its own and handed out items of freed pages.  And
 * uma_slab_free gave uz_fini the start of each redzoned object, not the
 * item uz_init had been given, 8 bytes further on.
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

static int failed;

#define CHECK(cond, what) do { \
    if (!(cond)) { \
        fprintf(stderr, "FAIL: %s\n", what); \
        failed = 1; \
    } \
} while (0)

/* Every item uz_init saw, and how many of them uz_fini saw again. */
#define MAXSEEN 4096
static void *seen[MAXSEEN];
static int nseen, fini_match, fini_calls;

static int item_init(void *obj, int size, int flags) {
    (void)size; (void)flags;
    if (nseen < MAXSEEN) {
        seen[nseen++] = obj;
    }
    return 0;
}

static void item_fini(void *obj, int size) {
    (void)size;
    fini_calls++;
    for (int i = 0; i < nseen; i++) {
        if (seen[i] == obj) {
            fini_match++;
            return;
        }
    }
}

static int on_list(const struct uma_bucket *list, const struct uma_bucket *b) {
    for (; list != NULL; list = list->ub_next) {
        if (list == b) {
            return 1;
        }
    }
    return 0;
}

int main(void) {
    enum { N = 300 };
    static void *items[N];

    uma_startup();
    uma_enable_dynamic_alloc();

    uma_zone_t *zone = uma_zcreate("doomed", 48, NULL, NULL, item_init, item_fini,
                                   8, UMA_ZONE_REDZONE);
    CHECK(zone != NULL, "a zone");
    if (zone == NULL) {
        return 1;
    }

    /* Enough frees to fill the per-CPU buckets and push full ones to the
     * depot. */
    for (int i = 0; i < N; i++) {
        items[i] = uma_zalloc(zone, 0);
    }
    for (int i = 0; i < N; i++) {
        if (items[i] != NULL) {
            uma_zfree(zone, items[i]);
        }
    }
    struct uma_bucket *ab = zone->uz_cpu[0].uc_allocbucket;
    struct uma_bucket *fb = zone->uz_cpu[0].uc_freebucket;
    int depot = 0;
    for (struct uma_bucket *b = uma_bucket_full_depot; b; b = b->ub_next) {
        depot += b->ub_zone == zone;
    }
    printf("before: alloc bucket %p, free bucket %p, %d full in the depot\n",
           (void *)ab, (void *)fb, depot);
    CHECK(ab != NULL || fb != NULL || depot > 0, "some items are in buckets");

    uma_zdestroy(zone);

    for (struct uma_bucket *b = uma_bucket_full_depot; b; b = b->ub_next) {
        if (b->ub_zone == zone) {
            CHECK(0, "a depot bucket still names the destroyed zone");
            break;
        }
    }
    if (ab != NULL) {
        CHECK(on_list(uma_bucket_empty_depot, ab), "the alloc bucket is back in the empty depot");
    }
    if (fb != NULL) {
        CHECK(on_list(uma_bucket_empty_depot, fb), "the free bucket is back in the empty depot");
    }
    char what[96];
    snprintf(what, sizeof(what), "uz_fini got the item uz_init got (%d of %d)",
             fini_match, fini_calls);
    CHECK(fini_calls > 0 && fini_match == fini_calls, what);

    if (failed) {
        return 1;
    }
    printf("PASS: a destroyed zone leaves no bucket behind, and fini sees init's item\n");
    return 0;
}
