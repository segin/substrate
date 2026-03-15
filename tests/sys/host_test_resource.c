#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void *kmalloc(size_t size) {
    return malloc(size);
}

void kfree(void *ptr, size_t size) {
    (void)size;
    free(ptr);
}

#include "../../sys/kern/resource.c"

static void test_resource_roots_initialize(void) {
    resource_init();
    assert(resource_root(RES_IO) != NULL);
    assert(resource_root(RES_MEM) != NULL);
    assert(resource_root(RES_IO)->start == 0);
    assert(resource_root(RES_IO)->end == 0xFFFF);
    assert(resource_root(RES_MEM)->child == NULL);
}

static void test_request_region_detects_conflicts_and_release_reclaims(void) {
    struct resource *res;

    resource_init();
    res = request_region(0x3F8, 8, "com1");
    assert(res != NULL);
    assert(strcmp(res->name, "com1") == 0);
    assert(request_region(0x3FC, 4, "overlap") == NULL);
    release_region(0x3F8, 8);
    assert(request_region(0x3F8, 8, "com1-again") != NULL);
}

static void test_request_mem_region_tracks_mmio_ranges(void) {
    struct resource *res;

    resource_init();
    res = request_mem_region(0xFEC00000ULL, 0x1000, "ioapic");
    assert(res != NULL);
    assert(res->start == 0xFEC00000ULL);
    assert(res->end == 0xFEC00FFFULL);
    assert(request_mem_region(0xFEC00800ULL, 0x100, "conflict") == NULL);
}

static void test_resource_dump_reports_allocated_ranges(void) {
    char buf[256];

    resource_init();
    assert(request_region(0x3F8, 8, "com1") != NULL);
    assert(request_mem_region(0xFEC00000ULL, 0x1000, "ioapic") != NULL);

    memset(buf, 0, sizeof(buf));
    assert(resource_dump(RES_IO, buf, sizeof(buf)) > 0);
    assert(strstr(buf, "3f8-3ff : com1") != NULL);

    memset(buf, 0, sizeof(buf));
    assert(resource_dump(RES_MEM, buf, sizeof(buf)) > 0);
    assert(strstr(buf, "fec00000-fec00fff : ioapic") != NULL);
}

int main(void) {
    test_resource_roots_initialize();
    test_request_region_detects_conflicts_and_release_reclaims();
    test_request_mem_region_tracks_mmio_ranges();
    test_resource_dump_reports_allocated_ranges();
    puts("host_test_resource: PASS");
    return 0;
}
