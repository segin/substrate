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
#include <string.h>
#include <kern/console.h>
#include <vm/vm_page.h>
#include <vm/phys_mem.h>
#include <vm/vm_object.h>

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

static void free_phys_page_list(vm_page_t *list) {
    while (list) {
        vm_page_t *next = list->next;
        vm_phys_free_page(list);
        list = next;
    }
}

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

/* Test: age scan decrements age and deactivates cold pages */
static void test_age_scan_deactivates_cold_page(void) {
    vm_page_t *page = vm_page_alloc(NULL, 0, 0);
    TEST_ASSERT(page != NULL, "age_scan: alloc failed");

    page->flags &= ~PG_BUSY;
    vm_page_activate(page);
    page->age = 1;

    vm_page_age_scan();

    TEST_ASSERT(page->age == 0, "age_scan: age not decremented to zero");
    TEST_ASSERT(page->flags & PG_INACTIVE, "age_scan: page not moved inactive");
    TEST_ASSERT(!(page->flags & PG_ACTIVE), "age_scan: page still active");

    vm_page_free(page);
    TEST_PASS("age_scan_deactivates_cold_page");
}

/* Test: policy switch changes active-page scan behavior */
static void test_page_policy_switching(void) {
    vm_page_t *clock_page = vm_page_alloc(NULL, 0, 0);
    vm_page_t *lru_page = vm_page_alloc(NULL, 0, 0);
    vm_page_policy_t saved_policy = vm_page_get_policy();

    TEST_ASSERT(clock_page != NULL && lru_page != NULL,
                "policy_switch: alloc failed");

    clock_page->flags &= ~PG_BUSY;
    lru_page->flags &= ~PG_BUSY;

    vm_page_set_policy(VM_PAGE_POLICY_CLOCK);
    vm_page_activate(clock_page);
    clock_page->age = 2;
    TEST_ASSERT(vm_pageout_scan(1) == 1,
                "policy_switch: CLOCK should deactivate cold page immediately");
    TEST_ASSERT(clock_page->flags & PG_INACTIVE,
                "policy_switch: CLOCK page not inactive");

    vm_page_set_policy(VM_PAGE_POLICY_LRU_APPROX);
    vm_page_activate(lru_page);
    lru_page->age = 2;
    TEST_ASSERT(vm_pageout_scan(1) == 0,
                "policy_switch: LRU should keep page active on first scan");
    TEST_ASSERT((lru_page->flags & PG_ACTIVE) != 0 && lru_page->age == 1,
                "policy_switch: LRU did not decrement age in place");
    TEST_ASSERT(vm_pageout_scan(1) == 1,
                "policy_switch: LRU should deactivate page on second scan");
    TEST_ASSERT(lru_page->flags & PG_INACTIVE,
                "policy_switch: LRU page not inactive after second scan");

    vm_page_set_policy(saved_policy);
    vm_page_free(clock_page);
    vm_page_free(lru_page);
    TEST_PASS("page_policy_switching");
}

/* Property: no page appears on two queues simultaneously */
static void test_queue_integrity_checker(void) {
    vm_page_t *active = vm_page_alloc(NULL, 0, 0);
    vm_page_t *inactive = vm_page_alloc(NULL, 0, 0);
    vm_page_t *wired = vm_page_alloc(NULL, 0, 0);

    TEST_ASSERT(active != NULL && inactive != NULL && wired != NULL,
                "queue_check: alloc failed");

    active->flags &= ~PG_BUSY;
    inactive->flags &= ~PG_BUSY;
    wired->flags &= ~PG_BUSY;

    vm_page_activate(active);
    vm_page_deactivate(inactive);
    vm_page_wire(wired);

    TEST_ASSERT(vm_page_check_queues(), "queue_check: integrity failed with mixed queues");

    vm_page_unwire(wired);
    vm_page_free(active);
    vm_page_free(inactive);
    vm_page_free(wired);

    TEST_ASSERT(vm_page_check_queues(), "queue_check: integrity failed after cleanup");
    TEST_PASS("queue_integrity_checker");
}

/* Property: free_count + all queue counts stays invariant under queue moves */
static void test_queue_accounting_invariant(void) {
    vm_vmstat_t before, during, after;
    vm_page_t *active = vm_page_alloc(NULL, 0, 0);
    vm_page_t *inactive = vm_page_alloc(NULL, 0, 0);
    vm_page_t *wired = vm_page_alloc(NULL, 0, 0);
    uint32_t accounted_before;
    uint32_t accounted_during;
    uint32_t accounted_after;

    TEST_ASSERT(active != NULL && inactive != NULL && wired != NULL,
                "queue_accounting: alloc failed");

    vm_page_get_vmstat(&before);
    accounted_before = before.free_count + before.active_count +
                       before.inactive_count + before.wire_count +
                       before.laundry_count;

    active->flags &= ~PG_BUSY;
    inactive->flags &= ~PG_BUSY;
    wired->flags &= ~PG_BUSY;

    vm_page_activate(active);
    vm_page_deactivate(inactive);
    vm_page_wire(wired);

    vm_page_get_vmstat(&during);
    accounted_during = during.free_count + during.active_count +
                       during.inactive_count + during.wire_count +
                       during.laundry_count;

    TEST_ASSERT(accounted_during == accounted_before,
                "queue_accounting: invariant broke after queue placement");

    vm_page_unwire(wired);
    vm_page_free(active);
    vm_page_free(inactive);
    vm_page_free(wired);

    vm_page_get_vmstat(&after);
    accounted_after = after.free_count + after.active_count +
                      after.inactive_count + after.wire_count +
                      after.laundry_count;

    TEST_ASSERT(accounted_after == accounted_before,
                "queue_accounting: invariant broke after cleanup");
    TEST_PASS("queue_accounting_invariant");
}

/* Unit: page daemon threshold trigger follows free_target */
static void test_pageout_threshold_trigger(void) {
    vm_page_thresholds_t thresholds;
    vm_page_t *allocated = NULL;
    vm_page_t *page;

    vm_page_get_thresholds(&thresholds);
    TEST_ASSERT(thresholds.free_target >= thresholds.free_min,
                "pageout_threshold: free_target below free_min");

    while (vm_phys_get_free() > thresholds.free_target) {
        page = vm_phys_alloc_page();
        TEST_ASSERT(page != NULL, "pageout_threshold: alloc to target failed");
        page->next = allocated;
        allocated = page;
    }

    TEST_ASSERT(vm_phys_get_free() == thresholds.free_target,
                "pageout_threshold: did not stop at free_target");
    TEST_ASSERT(!vm_page_should_pageout(),
                "pageout_threshold: triggered at free_target");

    page = vm_phys_alloc_page();
    TEST_ASSERT(page != NULL, "pageout_threshold: alloc below target failed");
    page->next = allocated;
    allocated = page;

    TEST_ASSERT(vm_phys_get_free() < thresholds.free_target,
                "pageout_threshold: free count not below target");
    TEST_ASSERT(vm_page_should_pageout(),
                "pageout_threshold: did not trigger below free_target");

    free_phys_page_list(allocated);
    TEST_PASS("pageout_threshold_trigger");
}

/* Test: vm_page_insert/remove maintains vm_object linkage */
static void test_vm_page_object_linkage(void) {
    vm_object_t *obj = vm_object_allocate(VM_OBJ_TYPE_DEFAULT, 0x4000);
    TEST_ASSERT(obj != NULL, "object_link: object alloc failed");

    vm_page_t page;
    memset(&page, 0, sizeof(page));

    vm_page_insert(&page, obj, 7);
    TEST_ASSERT(page.object == obj, "object_link: page object not set");
    TEST_ASSERT(page.pindex == 7, "object_link: page pindex not set");
    TEST_ASSERT(obj->page_count == 1, "object_link: object page_count != 1");
    TEST_ASSERT(vm_object_lookup_page(obj, 7) == &page, "object_link: lookup failed");

    vm_page_remove(&page);
    TEST_ASSERT(page.object == NULL, "object_link: page object not cleared");
    TEST_ASSERT(page.pindex == 0, "object_link: page pindex not cleared");
    TEST_ASSERT(obj->page_count == 0, "object_link: object page_count != 0");
    TEST_ASSERT(vm_object_lookup_page(obj, 7) == NULL, "object_link: lookup still found page");

    vm_object_deallocate(obj);
    TEST_PASS("vm_page_object_linkage");
}

/* Test: pv_entry insert/remove/remove_all maintains backlink list */
static void test_vm_page_try_to_free(void) {
    vm_page_t *page = vm_page_alloc(NULL, 0, 0);
    TEST_ASSERT(page != NULL, "try_to_free: initial alloc failed");

    // NULL check
    TEST_ASSERT(vm_page_try_to_free(NULL) == 0, "try_to_free: handled NULL incorrectly");

    // Clear busy flag so we can test other conditions
    page->flags &= ~PG_BUSY;

    // Test dirty page
    page->flags |= PG_DIRTY;
    TEST_ASSERT(vm_page_try_to_free(page) == 0, "try_to_free: freed a dirty page");
    page->flags &= ~PG_DIRTY;

    // Test busy page
    page->flags |= PG_BUSY;
    TEST_ASSERT(vm_page_try_to_free(page) == 0, "try_to_free: freed a busy page");
    page->flags &= ~PG_BUSY;

    // Test wired page
    page->wire_count = 1;
    TEST_ASSERT(vm_page_try_to_free(page) == 0, "try_to_free: freed a wired page");
    page->wire_count = 0;

    // Test clean, inactive page (should succeed and free it)
    TEST_ASSERT(vm_page_try_to_free(page) == 1, "try_to_free: failed to free a clean inactive page");

    TEST_PASS("vm_page_try_to_free");
}

static void test_pv_entry_list_manipulation(void) {
    vm_page_t page;
    struct pmap *pmap1 = (struct pmap *)(uintptr_t)0x1000;
    struct pmap *pmap2 = (struct pmap *)(uintptr_t)0x2000;

    memset(&page, 0, sizeof(page));

    pv_insert(&page, pmap1, 0x4000);
    TEST_ASSERT(page.pv_list != NULL, "pv_list: first insert failed");
    TEST_ASSERT(page.pv_list->pmap == pmap1, "pv_list: first pmap mismatch");
    TEST_ASSERT(page.pv_list->va == 0x4000, "pv_list: first va mismatch");

    pv_insert(&page, pmap2, 0x8000);
    TEST_ASSERT(page.pv_list != NULL, "pv_list: second insert failed");
    TEST_ASSERT(page.pv_list->pmap == pmap2, "pv_list: second insert not at head");
    TEST_ASSERT(page.pv_list->next != NULL, "pv_list: missing second element");
    TEST_ASSERT(page.pv_list->next->pmap == pmap1, "pv_list: first mapping lost");

    pv_remove(&page, pmap2, 0x8000);
    TEST_ASSERT(page.pv_list != NULL, "pv_list: head removed incorrectly");
    TEST_ASSERT(page.pv_list->pmap == pmap1, "pv_list: wrong entry remained after remove");
    TEST_ASSERT(page.pv_list->next == NULL, "pv_list: unexpected extra entry after remove");

    pv_remove_all(&page);
    TEST_ASSERT(page.pv_list == NULL, "pv_list: remove_all did not clear list");

    TEST_PASS("pv_entry_list_manipulation");
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
    test_age_scan_deactivates_cold_page();
    test_page_policy_switching();
    test_pageout_threshold_trigger();
    test_queue_integrity_checker();
    test_queue_accounting_invariant();
    test_vm_page_object_linkage();
    test_pv_entry_list_manipulation();
    test_vm_page_try_to_free();
    
    char buf[64];
    sprintf(buf, "=== vm_page tests: %d passed, %d failed ===\n", passed, failed);
    kprint(buf);
}
