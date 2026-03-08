/*
 * Unit tests for VM Page Replacement Policy (Clock/LRU)
 */

#include <stdint.h>
#include <string.h>
#include <arch/i386/pmap.h>
#include <vm/vm_page.h>
#include <vm/vm_object.h>
#include <vm/vm_pager.h>
#include <exec/perso/personality.h>
#include <sys/proc.h>
#include <kern/console.h>
#include <kern/sched.h>
#include <pm/pm.h>
#include <vm/phys_mem.h>

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST_ASSERT(cond, msg) do { \
    if (!(cond)) { \
        kprint("FAIL: "); kprint(msg); kprint("\n"); \
        tests_failed++; \
        return; \
    } \
    tests_passed++; \
} while(0)

// Helper to manually set up page state
extern void vm_page_activate(vm_page_t *m);
extern void vm_page_deactivate(vm_page_t *m);

static void free_phys_page_list(vm_page_t *list) {
    while (list) {
        vm_page_t *next = list->next;
        vm_phys_free_page(list);
        list = next;
    }
}

void test_vm_policy_lru(void) {
    kprint("Test: vm_policy_lru (Clock Algorithm)\n");
    
    // Allocate a few pages
    vm_object_t *obj = vm_object_allocate(VM_OBJ_TYPE_DEFAULT, 0x5000);
    vm_page_t *p1 = vm_page_alloc(obj, 0, 0);
    vm_page_t *p2 = vm_page_alloc(obj, 1, 0);
    vm_page_t *p3 = vm_page_alloc(obj, 2, 0);
    vm_page_t *p4 = vm_page_alloc(obj, 3, 0);
    
    // Activate them (put on active queue)
    vm_page_activate(p1);
    vm_page_activate(p2);
    vm_page_activate(p3);
    vm_page_activate(p4);
    
    TEST_ASSERT(p1->flags & PG_ACTIVE, "p1 is active");
    
    // Scan should move unreferenced pages to inactive
    // We assume pmap_is_referenced returns 0 by default for these unused pages
    // (requires pmap module to verify no mappings or mocked to return 0)
    
    int deactivated = vm_pageout_scan(10);
    
    // Since we just allocated them and didn't map them, they are unreferenced.
    // They should all be deactivated.
    TEST_ASSERT(deactivated >= 4, "pages deactivated");
    TEST_ASSERT(p1->flags & PG_INACTIVE, "p1 inactive");
    TEST_ASSERT(p2->flags & PG_INACTIVE, "p2 inactive");
    
    // Now simulate access on p1 (manually set active again)
    vm_page_activate(p1);
    // And simulate pmap reference? We can't easily mock pmap_is_referenced here 
    // without hacking pmap.c or PV list.
    // But we can verify that ACTIVE pages stay active if scan count is low? 
    // No, scan moves them if unreferenced.
    
    // Cleanup
    vm_page_free(p1);
    vm_page_free(p2);
    vm_page_free(p3);
    vm_page_free(p4);
    vm_object_deallocate(obj);
    
    kprint("  PASS\n");
}

void test_vm_policy_writeback(void) {
    kprint("Test: vm_policy_writeback\n");
    
    // Create swap-backed object
    vm_object_t *obj = vm_object_allocate(VM_OBJ_TYPE_SWAP, 0x1000);
    vm_page_t *p1 = vm_page_alloc(obj, 0, 0);
    
    // Mark dirty and inactive (candidate for laundering)
    p1->flags |= PG_DIRTY;
    vm_page_deactivate(p1);
    
    TEST_ASSERT(p1->flags & PG_DIRTY, "p1 is dirty");
    TEST_ASSERT(p1->flags & PG_INACTIVE, "p1 is inactive");
    
    // Launcher should clean it
    extern void vm_page_launder(vm_page_t *m);
    vm_page_launder(p1);
    
    TEST_ASSERT(!(p1->flags & PG_DIRTY), "p1 is clean");
    TEST_ASSERT(p1->flags & PG_INACTIVE, "p1 still inactive (ready to free)");
    
    vm_page_free(p1);
    vm_object_deallocate(obj);
    
    kprint("  PASS\n");
}

void test_vm_pageout_prefers_inactive_then_active(void) {
    vm_page_thresholds_t thresholds;
    vm_page_t *pressure = NULL;
    vm_page_t *active;
    vm_page_t *inactive;

    kprint("Test: vm_pageout_prefers_inactive_then_active\n");

    vm_page_get_thresholds(&thresholds);

    active = vm_page_alloc(NULL, 0, 0);
    inactive = vm_page_alloc(NULL, 0, 0);
    TEST_ASSERT(active != NULL && inactive != NULL, "inactive_first: alloc failed");

    active->flags &= ~PG_BUSY;
    inactive->flags &= ~PG_BUSY;
    vm_page_activate(active);
    vm_page_deactivate(inactive);

    while (vm_phys_get_free() > thresholds.free_target - 1) {
        vm_page_t *page = vm_phys_alloc_page();
        TEST_ASSERT(page != NULL, "inactive_first: pressure alloc failed");
        page->next = pressure;
        pressure = page;
    }

    vm_pageout();

    TEST_ASSERT(vm_phys_get_free() >= thresholds.free_target,
                "inactive_first: pageout did not recover target");
    TEST_ASSERT(active->flags & PG_ACTIVE,
                "inactive_first: active page was disturbed");

    vm_page_free(active);
    free_phys_page_list(pressure);
    TEST_ASSERT(vm_phys_get_free() >= thresholds.free_target,
                "inactive_first: cleanup leaked free pages");

    kprint("  PASS\n");
}

void test_vm_pageout_launders_before_scanning_active(void) {
    vm_page_thresholds_t thresholds;
    vm_page_t *pressure = NULL;
    vm_page_t *active;
    vm_object_t *obj;
    vm_page_t *dirty;

    kprint("Test: vm_pageout_launders_before_scanning_active\n");

    vm_page_get_thresholds(&thresholds);

    active = vm_page_alloc(NULL, 0, 0);
    obj = vm_object_allocate(VM_OBJ_TYPE_SWAP, 0x1000);
    dirty = vm_page_alloc(obj, 0, 0);
    TEST_ASSERT(active != NULL && obj != NULL && dirty != NULL,
                "laundry_first: alloc failed");

    active->flags &= ~PG_BUSY;
    dirty->flags &= ~PG_BUSY;
    active->flags &= ~PG_DIRTY;
    vm_page_activate(active);

    dirty->flags |= PG_DIRTY;
    vm_page_deactivate(dirty);

    while (vm_phys_get_free() > thresholds.free_target - 1) {
        vm_page_t *page = vm_phys_alloc_page();
        TEST_ASSERT(page != NULL, "laundry_first: pressure alloc failed");
        page->next = pressure;
        pressure = page;
    }

    vm_pageout();

    TEST_ASSERT(vm_phys_get_free() >= thresholds.free_target,
                "laundry_first: pageout did not recover target");
    TEST_ASSERT(active->flags & PG_ACTIVE,
                "laundry_first: active page was disturbed");

    vm_page_free(active);
    vm_object_deallocate(obj);
    free_phys_page_list(pressure);
    TEST_ASSERT(vm_phys_get_free() >= thresholds.free_target,
                "laundry_first: cleanup leaked free pages");

    kprint("  PASS\n");
}

void test_vm_pageout_oom_kills_largest_user_process(void) {
    vm_page_thresholds_t thresholds;
    vm_page_t *pressure = NULL;
    process_t *small;
    process_t *large;
    process_t *kproc;
    process_t *init;
    thread_t *small_thread;
    thread_t *large_thread;
    thread_t *kernel_thread;
    thread_t *init_thread = NULL;
    struct pmap small_pmap;
    struct pmap large_pmap;
    struct pmap kernel_pmap;

    kprint("Test: vm_pageout_oom_kills_largest_user_process\n");

    vm_page_get_thresholds(&thresholds);

    small = proc_create(PERS_NATIVE);
    large = proc_create(PERS_NATIVE);
    kproc = proc_create(PERS_NATIVE);
    TEST_ASSERT(small != NULL && large != NULL && kproc != NULL,
                "oom_kill: proc_create failed");

    small_thread = sched_alloc_thread(small);
    large_thread = sched_alloc_thread(large);
    kernel_thread = sched_alloc_thread(kproc);
    TEST_ASSERT(small_thread != NULL && large_thread != NULL && kernel_thread != NULL,
                "oom_kill: sched_alloc_thread failed");

    memset(&small_pmap, 0, sizeof(small_pmap));
    memset(&large_pmap, 0, sizeof(large_pmap));
    memset(&kernel_pmap, 0, sizeof(kernel_pmap));

    small->state = SRUN;
    large->state = SRUN;
    kproc->state = SRUN;
    small->is_kernel_task = 0;
    large->is_kernel_task = 0;
    kproc->is_kernel_task = 1;
    small->pmap = &small_pmap;
    large->pmap = &large_pmap;
    kproc->pmap = &kernel_pmap;
    small_pmap.resident_count = 8;
    large_pmap.resident_count = 32;
    kernel_pmap.resident_count = 128;

    small_thread->state = THREAD_READY;
    large_thread->state = THREAD_READY;
    kernel_thread->state = THREAD_READY;
    small_thread->sig_mask = 0;
    large_thread->sig_mask = 0;
    kernel_thread->sig_mask = 0;
    small_thread->sig_pending = 0;
    large_thread->sig_pending = 0;
    kernel_thread->sig_pending = 0;

    init = proc_find(1);
    if (init) {
        for (int i = 0; i < MAX_THREADS; i++) {
            if (threads[i].tid != -1 && threads[i].proc == init) {
                init_thread = &threads[i];
                break;
            }
        }
    }

    while (vm_phys_get_free() > thresholds.free_min - 1) {
        vm_page_t *page = vm_phys_alloc_page();
        TEST_ASSERT(page != NULL, "oom_kill: pressure alloc failed");
        page->next = pressure;
        pressure = page;
    }

    vm_pageout();

    TEST_ASSERT(large_thread->sig_pending & sigmask(SIGKILL),
                "oom_kill: largest user process not selected");
    TEST_ASSERT(!(small_thread->sig_pending & sigmask(SIGKILL)),
                "oom_kill: smaller user process killed");
    TEST_ASSERT(!(kernel_thread->sig_pending & sigmask(SIGKILL)),
                "oom_kill: kernel task selected");
    if (init_thread) {
        TEST_ASSERT(!(init_thread->sig_pending & sigmask(SIGKILL)),
                    "oom_kill: init selected");
    }

    small_thread->tid = -1;
    large_thread->tid = -1;
    kernel_thread->tid = -1;
    small->pid = -1;
    large->pid = -1;
    kproc->pid = -1;
    free_phys_page_list(pressure);

    kprint("  PASS\n");
}

void run_vm_policy_tests(void) {
    kprint("\n=== VM Policy Tests ===\n");
    test_vm_policy_lru();
    test_vm_policy_writeback();
    test_vm_pageout_prefers_inactive_then_active();
    test_vm_pageout_launders_before_scanning_active();
    test_vm_pageout_oom_kills_largest_user_process();
    kprint("\nVM Policy Tests Complete\n");
}
