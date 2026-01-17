/*
 * test_vm_page_queue.c - Unit tests for vm_page.c queue operations
 *
 * Tests LRU queue management:
 * - activate/deactivate
 * - wire/unwire
 * - hold/unhold
 * - LRU scanning
 * - Eviction candidate detection
 */
#include <stdio.h>
#include <stdint.h>
#include "../kern/console.h"
#include "../vm/vm_page.h"
#include "../vm/phys_mem.h"

static int passed = 0;
static int failed = 0;

#define TEST_ASSERT(cond, msg) do { \
    if (!(cond)) { \
        kprint("[FAIL] "); kprint(msg); kprint("\n"); \
        failed++; \
        return; \
    } \
} while(0)

#define TEST_PASS(msg) do { \
    kprint("[PASS] "); kprint(msg); kprint("\n"); \
    passed++; \
} while(0)

/* Test: Page allocation sets initial state correctly */
static void test_page_alloc_initial_state(void) {
    vm_page_t *page = vm_page_alloc(NULL, 0, 0);
    TEST_ASSERT(page != NULL, "alloc_state: returned NULL");
    TEST_ASSERT(page->ref_count == 1, "alloc_state: ref_count != 1");
    TEST_ASSERT(page->flags & PG_BUSY, "alloc_state: not busy");
    TEST_ASSERT(!(page->flags & PG_FREE), "alloc_state: still marked free");
    
    vm_page_free(page);
    TEST_PASS("page_alloc_initial_state");
}

/* Test: vm_page_activate moves page to active queue */
static void test_page_activate(void) {
    vm_page_t *page = vm_page_alloc(NULL, 0, 0);
    TEST_ASSERT(page != NULL, "activate: alloc failed");
    
    /* Initially not active */
    page->flags &= ~PG_ACTIVE;
    
    vm_page_activate(page);
    TEST_ASSERT(page->flags & PG_ACTIVE, "activate: not active");
    TEST_ASSERT(!(page->flags & PG_INACTIVE), "activate: still inactive");
    
    vm_page_free(page);
    TEST_PASS("page_activate");
}

/* Test: vm_page_deactivate moves page to inactive queue */
static void test_page_deactivate(void) {
    vm_page_t *page = vm_page_alloc(NULL, 0, 0);
    TEST_ASSERT(page != NULL, "deactivate: alloc failed");
    
    vm_page_activate(page);
    TEST_ASSERT(page->flags & PG_ACTIVE, "deactivate: not active first");
    
    vm_page_deactivate(page);
    TEST_ASSERT(page->flags & PG_INACTIVE, "deactivate: not inactive");
    TEST_ASSERT(!(page->flags & PG_ACTIVE), "deactivate: still active");
    
    vm_page_free(page);
    TEST_PASS("page_deactivate");
}

/* Test: vm_page_wire increments wire_count and moves to wired queue */
static void test_page_wire(void) {
    vm_page_t *page = vm_page_alloc(NULL, 0, 0);
    TEST_ASSERT(page != NULL, "wire: alloc failed");
    TEST_ASSERT(page->wire_count == 0, "wire: initial wire_count != 0");
    
    vm_page_wire(page);
    TEST_ASSERT(page->wire_count == 1, "wire: wire_count != 1");
    
    /* Wire again */
    vm_page_wire(page);
    TEST_ASSERT(page->wire_count == 2, "wire: wire_count != 2");
    
    /* Unwire back */
    vm_page_unwire(page);
    vm_page_unwire(page);
    
    vm_page_free(page);
    TEST_PASS("page_wire");
}

/* Test: vm_page_unwire decrements wire_count */
static void test_page_unwire(void) {
    vm_page_t *page = vm_page_alloc(NULL, 0, 0);
    TEST_ASSERT(page != NULL, "unwire: alloc failed");
    
    vm_page_wire(page);
    vm_page_wire(page);
    TEST_ASSERT(page->wire_count == 2, "unwire: wire_count != 2");
    
    vm_page_unwire(page);
    TEST_ASSERT(page->wire_count == 1, "unwire: wire_count != 1");
    
    vm_page_unwire(page);
    TEST_ASSERT(page->wire_count == 0, "unwire: wire_count != 0");
    
    /* Unwire when already 0 should be safe */
    vm_page_unwire(page);
    TEST_ASSERT(page->wire_count == 0, "unwire: went negative?");
    
    vm_page_free(page);
    TEST_PASS("page_unwire");
}

/* Test: vm_page_hold increments ref_count */
static void test_page_hold(void) {
    vm_page_t *page = vm_page_alloc(NULL, 0, 0);
    TEST_ASSERT(page != NULL, "hold: alloc failed");
    TEST_ASSERT(page->ref_count == 1, "hold: initial ref_count != 1");
    
    vm_page_hold(page);
    TEST_ASSERT(page->ref_count == 2, "hold: ref_count != 2");
    
    vm_page_unhold(page);
    vm_page_free(page);
    TEST_PASS("page_hold");
}

/* Test: vm_page_unhold decrements ref_count */
static void test_page_unhold(void) {
    vm_page_t *page = vm_page_alloc(NULL, 0, 0);
    TEST_ASSERT(page != NULL, "unhold: alloc failed");
    
    vm_page_hold(page);
    TEST_ASSERT(page->ref_count == 2, "unhold: ref_count != 2");
    
    vm_page_unhold(page);
    TEST_ASSERT(page->ref_count == 1, "unhold: ref_count != 1");
    
    vm_page_free(page);
    TEST_PASS("page_unhold");
}

/* Test: wired pages are not eviction candidates */
static void test_wired_not_evictable(void) {
    vm_page_t *page = vm_page_alloc(NULL, 0, 0);
    TEST_ASSERT(page != NULL, "wired_evict: alloc failed");
    
    vm_page_wire(page);
    TEST_ASSERT(!vm_page_is_evict_candidate(page), "wired_evict: wired page evictable");
    
    vm_page_unwire(page);
    vm_page_free(page);
    TEST_PASS("wired_not_evictable");
}

/* Test: busy pages are not eviction candidates */
static void test_busy_not_evictable(void) {
    vm_page_t *page = vm_page_alloc(NULL, 0, 0);
    TEST_ASSERT(page != NULL, "busy_evict: alloc failed");
    
    page->flags |= PG_BUSY;
    TEST_ASSERT(!vm_page_is_evict_candidate(page), "busy_evict: busy page evictable");
    
    page->flags &= ~PG_BUSY;
    vm_page_free(page);
    TEST_PASS("busy_not_evictable");
}

/* Test: active pages are not eviction candidates */
static void test_active_not_evictable(void) {
    vm_page_t *page = vm_page_alloc(NULL, 0, 0);
    TEST_ASSERT(page != NULL, "active_evict: alloc failed");
    
    vm_page_activate(page);
    page->flags &= ~PG_BUSY;  /* Clear busy for test */
    TEST_ASSERT(!vm_page_is_evict_candidate(page), "active_evict: active evictable");
    
    vm_page_free(page);
    TEST_PASS("active_not_evictable");
}

/* Test: inactive page with age 0 is eviction candidate */
static void test_inactive_age0_evictable(void) {
    vm_page_t *page = vm_page_alloc(NULL, 0, 0);
    TEST_ASSERT(page != NULL, "inactive_evict: alloc failed");
    
    vm_page_deactivate(page);
    page->flags &= ~PG_BUSY;
    page->age = 0;
    
    TEST_ASSERT(vm_page_is_evict_candidate(page), "inactive_evict: not evictable");
    
    vm_page_free(page);
    TEST_PASS("inactive_age0_evictable");
}

/* Test: vm_page_get_stats returns sensible values */
static void test_get_stats(void) {
    vm_page_stats_t stats;
    vm_page_get_stats(&stats);
    
    /* Stats should be non-negative */
    TEST_ASSERT(stats.active_count >= 0, "stats: negative active");
    TEST_ASSERT(stats.inactive_count >= 0, "stats: negative inactive");
    TEST_ASSERT(stats.free_count >= 0, "stats: negative free");
    TEST_ASSERT(stats.dirty_count >= 0, "stats: negative dirty");
    
    TEST_PASS("get_stats");
}

/* Test entry point */
void test_vm_page_queue(void) {
    kprint("=== VM Page Queue Unit Tests ===\n");
    
    test_page_alloc_initial_state();
    test_page_activate();
    test_page_deactivate();
    test_page_wire();
    test_page_unwire();
    test_page_hold();
    test_page_unhold();
    test_wired_not_evictable();
    test_busy_not_evictable();
    test_active_not_evictable();
    test_inactive_age0_evictable();
    test_get_stats();
    
    char buf[64];
    sprintf(buf, "=== vm_page tests: %d passed, %d failed ===\n", passed, failed);
    kprint(buf);
}
