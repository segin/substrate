#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int pmap_enter_calls;
static int pmap_kremove_calls;
static uintptr_t last_va;
static uintptr_t last_pa;
static uint32_t last_flags;
static int fake_pat_wc_enabled;

void *kmalloc(size_t size) {
    return malloc(size);
}

void kfree(void *ptr, size_t size) {
    (void)size;
    free(ptr);
}

typedef void *pmap_t;
pmap_t pmap_kernel(void) {
    return (pmap_t)0x1;
}

int pmap_enter(pmap_t pmap, uintptr_t va, uintptr_t pa, uint32_t prot, uint32_t flags) {
    (void)pmap;
    (void)prot;
    pmap_enter_calls++;
    last_va = va;
    last_pa = pa;
    last_flags = flags;
    return 0;
}

void pmap_kremove(uintptr_t va) {
    pmap_kremove_calls++;
    last_va = va;
}

int i386_cpu_pat_wc_enabled(void) {
    return fake_pat_wc_enabled;
}

#define HOST_TEST 1
#include "../../sys/kern/resource.c"
#include "../../sys/kern/ioremap.c"

static void reset_state(void) {
    pmap_enter_calls = 0;
    pmap_kremove_calls = 0;
    last_va = 0;
    last_pa = 0;
    last_flags = 0;
    fake_pat_wc_enabled = 0;
    resource_init();
    ioremap_regions = NULL;
    ioremap_next = IOREMAP_BASE;
}

static void test_ioremap_maps_and_unmaps_uncached_pages(void) {
    void *addr;

    reset_state();
    addr = ioremap(0xFEC00020U, 0x100);
    assert(addr != NULL);
    assert(pmap_enter_calls == 1);
    assert(last_pa == 0xFEC00000U);
    assert((last_flags & PTE_PCD) != 0);

    iounmap(addr);
    assert(pmap_kremove_calls == 1);
}

static void test_ioremap_wc_uses_pat_when_available(void) {
    void *addr;

    reset_state();
    fake_pat_wc_enabled = 1;
    addr = ioremap_wc(0xE0001000U, 0x2000);
    assert(addr != NULL);
    assert(pmap_enter_calls == 2);
    assert(last_pa == 0xE0002000U);
    assert((last_flags & PTE_PAT) != 0);
    assert((last_flags & PTE_PWT) != 0);
    assert((last_flags & PTE_PCD) == 0);

    iounmap(addr);
    assert(pmap_kremove_calls == 2);
}

int main(void) {
    test_ioremap_maps_and_unmaps_uncached_pages();
    test_ioremap_wc_uses_pat_when_available();
    puts("host_test_ioremap: PASS");
    return 0;
}
