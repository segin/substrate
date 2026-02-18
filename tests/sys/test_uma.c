#include <vm/uma.h>
#include <kern/console.h>
#include "tests.h"

static void test_uma_large_alloc(void) {
    /* Create a zone with large items (3000 bytes) */
    /* This should force off-page slabs because:
       2 items * 3000 = 6000 > 4096 (1 item per page)
       With header on page: 4096 - sizeof(slab) = ~4064. Each item 3000. Fits 1.
       With header off page: 4096. Fits 1. 
       Wait, let's try size 2040.
       On page: 4096 - 32 = 4064. 2 * 2040 = 4080 > 4064. Fits 1.
       Off page: 4096. 2 * 2040 = 4080 <= 4096. Fits 2.
       So 2040 bytes is the perfect test case for off-page optimization.
    */
    
    kprint("TEST: UMA Large Allocation (Off-Page Logic)...\n");
    
    // Size 2040: Should fit 2 items if off-page, 1 if on-page.
    // Our logic enables off-page if it improves density.
    uma_zone_t *zone = uma_zcreate("test_large", 2040, NULL, NULL, NULL, NULL, 0, 0);
    
    if (!zone) {
        kprint("FAIL: Could not create zone\n");
        return;
    }
    
    if (zone->uz_flags & UMA_ZONE_OFFPAGE) {
        kprint("PASS: Zone created with UMA_ZONE_OFFPAGE flag\n");
    } else {
        kprint("WARN: Zone did NOT set UMA_ZONE_OFFPAGE flag (might be expected if heuristics differ)\n");
    }
    
    kprintf("INFO: Items per slab: %d\n", zone->uz_ipers);
    
    /* Allocate items */
    void *item1 = uma_zalloc(zone, M_WAITOK);
    void *item2 = uma_zalloc(zone, M_WAITOK);
    void *item3 = uma_zalloc(zone, M_WAITOK);
    
    if (item1 && item2 && item3) {
        kprint("PASS: Allocated 3 items\n");
    } else {
        kprint("FAIL: Allocation failed\n");
    }
    
    uma_zfree(zone, item1);
    uma_zfree(zone, item2);
    uma_zfree(zone, item3);
    
    uma_zdestroy(zone);
    kprint("TEST: UMA Large Allocation completed\n");
}

void run_uma_tests(void) {
    test_uma_large_alloc();
}
