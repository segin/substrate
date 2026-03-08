#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include <vm/uma.h>
#include <vm/vm_kmem.h>

typedef struct alloc_rec {
    void *ptr;
    size_t size;
} alloc_rec_t;

static uma_zone_t test_zones[9];
static alloc_rec_t small_allocs[128];
static alloc_rec_t large_allocs[32];
static int uma_alloc_calls;
static int uma_free_calls;
static int pmm_alloc_calls;
static int pmm_free_calls;
static int dynamic_enabled;

static void alloc_rec_set(alloc_rec_t *table, size_t table_len, void *ptr, size_t size) {
    for (size_t i = 0; i < table_len; i++) {
        if (table[i].ptr == NULL || table[i].ptr == ptr) {
            table[i].ptr = ptr;
            table[i].size = size;
            return;
        }
    }
}

static size_t alloc_rec_take(alloc_rec_t *table, size_t table_len, void *ptr) {
    for (size_t i = 0; i < table_len; i++) {
        if (table[i].ptr == ptr) {
            size_t size = table[i].size;
            table[i].ptr = NULL;
            table[i].size = 0;
            return size;
        }
    }
    return 0;
}

static size_t alloc_rec_get(alloc_rec_t *table, size_t table_len, void *ptr) {
    for (size_t i = 0; i < table_len; i++) {
        if (table[i].ptr == ptr) {
            return table[i].size;
        }
    }
    return 0;
}

void kprint(const char *msg) {
    (void)msg;
}

uma_zone_t *uma_zcreate(const char *name, size_t size, uma_ctor ctor, uma_dtor dtor,
                        uma_init init, uma_fini fini, int align, uint32_t flags) {
    static size_t next_zone;

    (void)name;
    (void)ctor;
    (void)dtor;
    (void)init;
    (void)fini;
    (void)align;
    (void)flags;

    if (next_zone >= sizeof(test_zones) / sizeof(test_zones[0])) {
        return NULL;
    }

    memset(&test_zones[next_zone], 0, sizeof(test_zones[next_zone]));
    test_zones[next_zone].uz_size = size;
    return &test_zones[next_zone++];
}

void *uma_zalloc(uma_zone_t *zone, int flags) {
    void *ptr;

    (void)flags;
    uma_alloc_calls++;
    ptr = malloc(zone->uz_size);
    if (ptr) {
        alloc_rec_set(small_allocs, sizeof(small_allocs) / sizeof(small_allocs[0]), ptr, zone->uz_size);
    }
    return ptr;
}

void uma_zfree(uma_zone_t *zone, void *item) {
    size_t size;

    (void)zone;
    uma_free_calls++;
    size = alloc_rec_take(small_allocs, sizeof(small_allocs) / sizeof(small_allocs[0]), item);
    if (size == 0) {
        fprintf(stderr, "FAIL: small free for unknown pointer\n");
        exit(1);
    }
    free(item);
}

void uma_enable_dynamic_alloc(void) {
    dynamic_enabled = 1;
}

size_t uma_item_size(void *item) {
    return alloc_rec_get(small_allocs, sizeof(small_allocs) / sizeof(small_allocs[0]), item);
}

void *pmm_alloc_contiguous(size_t pages) {
    void *ptr;
    size_t bytes = pages * 4096;

    pmm_alloc_calls++;
    ptr = malloc(bytes);
    if (ptr) {
        alloc_rec_set(large_allocs, sizeof(large_allocs) / sizeof(large_allocs[0]), ptr, bytes);
    }
    return ptr;
}

void pmm_free_contiguous(void *ptr, size_t pages) {
    size_t bytes = alloc_rec_take(large_allocs, sizeof(large_allocs) / sizeof(large_allocs[0]), ptr);

    pmm_free_calls++;
    if (bytes != pages * 4096) {
        fprintf(stderr, "FAIL: large free size mismatch\n");
        exit(1);
    }
    free(ptr);
}

#include "../../sys/vm/vm_kmem.c"

static void require(int cond, const char *msg) {
    if (!cond) {
        fprintf(stderr, "FAIL: %s\n", msg);
        exit(1);
    }
}

int main(void) {
    kmem_stat_snapshot_t snapshot;

    kmem_init();
    require(dynamic_enabled, "kmem_init enables dynamic UMA allocation");

    void *small = kmalloc(32);
    require(small != NULL, "small kmalloc succeeds");
    require(uma_alloc_calls == 1, "small kmalloc uses UMA");
    require(pmm_alloc_calls == 0, "small kmalloc does not use PMM");
    kmem_get_snapshot(&snapshot);
    require(snapshot.buckets[1].bucket_size == 32, "32-byte bucket exposed");
    require(snapshot.buckets[1].allocs == 1, "32-byte bucket alloc count increments");
    require(snapshot.buckets[1].bytes_outstanding == 32, "32-byte bucket outstanding bytes tracked");
    require(snapshot.buckets[1].objects_outstanding == 1, "32-byte bucket outstanding objects tracked");

    memset(small, 0x5A, 32);
    small = krealloc(small, 64);
    require(small != NULL, "small krealloc grow succeeds");
    require(uma_alloc_calls == 2, "small krealloc grow allocates via UMA");
    for (int i = 0; i < 32; i++) {
        require(((unsigned char *)small)[i] == 0x5A, "small krealloc preserves data");
    }

    small = krealloc(small, 8);
    require(small != NULL, "small krealloc shrink succeeds");
    for (int i = 0; i < 8; i++) {
        require(((unsigned char *)small)[i] == 0x5A, "small krealloc shrink preserves prefix");
    }
    kmem_get_snapshot(&snapshot);
    require(snapshot.buckets[2].allocs == 1, "64-byte bucket alloc tracked");
    require(snapshot.buckets[0].allocs == 1, "16-byte bucket alloc tracked");

    require(krealloc(small, 0) == NULL, "krealloc(ptr, 0) returns NULL");
    require(uma_free_calls >= 3, "small krealloc path frees old allocations");
    kmem_get_snapshot(&snapshot);
    require(snapshot.buckets[0].objects_outstanding == 0, "small outstanding object count drains");
    require(snapshot.buckets[1].objects_outstanding == 0, "32-byte bucket drains");
    require(snapshot.buckets[2].objects_outstanding == 0, "64-byte bucket drains");

    void *large = kmalloc(5000);
    require(large != NULL, "large kmalloc succeeds");
    require(pmm_alloc_calls == 1, "large kmalloc bypasses UMA");
    kmem_get_snapshot(&snapshot);
    require(snapshot.large_allocs == 1, "large alloc count increments");
    require(snapshot.large_bytes_outstanding == 5000, "large outstanding bytes tracked");

    memset(large, 0xA5, 5000);
    large = krealloc(large, 7000);
    require(large != NULL, "large krealloc grow succeeds");
    require(pmm_alloc_calls == 2, "large krealloc allocates via PMM");
    for (int i = 0; i < 5000; i++) {
        require(((unsigned char *)large)[i] == 0xA5, "large krealloc preserves data");
    }

    require(krealloc(large, 0) == NULL, "large krealloc(ptr, 0) returns NULL");
    kmem_get_snapshot(&snapshot);
    require(snapshot.large_frees == 2, "large free count tracks reallocation and final free");
    require(snapshot.large_bytes_outstanding == 0, "large outstanding bytes drain");

    require(krealloc(NULL, 48) != NULL, "krealloc(NULL, size) behaves like kmalloc");
    kmem_get_snapshot(&snapshot);
    require(snapshot.total_allocs >= snapshot.total_frees, "global alloc count not below frees");

    printf("PASS: vm_kmem large path and krealloc behavior validated\n");
    return 0;
}
