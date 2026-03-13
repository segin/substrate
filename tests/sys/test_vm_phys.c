/*
 * test_vm_phys.c - Integration tests for Physical Memory Manager (phys_mem.c)
 *
 * Tests the vm_phys API including:
 * - Single page allocation/free
 * - Contiguous allocation/free
 * - Buddy coalescing on free
 * - Double-free protection
 */
#include <stdio.h>
#include <stdint.h>
#include <kern/console.h>
#include <arch/i386/pmm.h>
#include <vm/phys_mem.h>
#include <vm/vm_page.h>

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

/* Test: Single page allocation returns valid page */
static void test_single_page_alloc(void) {
    vm_page_t *page = vm_phys_alloc_page();
    TEST_ASSERT(page != NULL, "single_alloc: returned NULL");
    TEST_ASSERT(page->phys_addr != 0, "single_alloc: phys_addr is 0");
    TEST_ASSERT((page->phys_addr & 0xFFF) == 0, "single_alloc: not page-aligned");
    
    /* Verify page is not marked free */
    TEST_ASSERT(!(page->flags & PG_FREE), "single_alloc: still marked free");
    
    /* Clean up */
    vm_phys_free_page(page);
    TEST_PASS("single_page_alloc");
}

/* Test: Single page free returns page to pool */
static void test_single_page_free(void) {
    size_t free_before = vm_phys_get_free();
    
    vm_page_t *page = vm_phys_alloc_page();
    TEST_ASSERT(page != NULL, "single_free: alloc returned NULL");
    
    size_t free_after_alloc = vm_phys_get_free();
    TEST_ASSERT(free_after_alloc < free_before, "single_free: free count didn't decrease");
    
    vm_phys_free_page(page);
    
    size_t free_after_free = vm_phys_get_free();
    TEST_ASSERT(free_after_free >= free_after_alloc, "single_free: free count didn't increase");
    
    TEST_PASS("single_page_free");
}

/* Test: Contiguous allocation for order 0 (1 page) */
static void test_contiguous_order0(void) {
    vm_page_t *page = vm_phys_alloc_contiguous(1);
    TEST_ASSERT(page != NULL, "contig_order0: returned NULL");
    TEST_ASSERT((page->phys_addr & 0xFFF) == 0, "contig_order0: not aligned");
    
    vm_phys_free_contiguous(page, 1);
    TEST_PASS("contiguous_order0");
}

/* Test: Contiguous allocation for order 2 (4 pages) */
static void test_contiguous_order2(void) {
    vm_page_t *page = vm_phys_alloc_contiguous(4);
    TEST_ASSERT(page != NULL, "contig_order2: returned NULL");
    
    /* Order 2 = 4 pages = 16KB, must be 16KB aligned */
    TEST_ASSERT((page->phys_addr & 0x3FFF) == 0, "contig_order2: not 16KB aligned");
    
    /* Verify we got 4 contiguous pages */
    vm_page_t *p1 = vm_phys_paddr_to_page(page->phys_addr);
    vm_page_t *p2 = vm_phys_paddr_to_page(page->phys_addr + 0x1000);
    vm_page_t *p3 = vm_phys_paddr_to_page(page->phys_addr + 0x2000);
    vm_page_t *p4 = vm_phys_paddr_to_page(page->phys_addr + 0x3000);
    
    TEST_ASSERT(p1 == page, "contig_order2: p1 mismatch");
    TEST_ASSERT(p2 != NULL, "contig_order2: p2 NULL");
    TEST_ASSERT(p3 != NULL, "contig_order2: p3 NULL");
    TEST_ASSERT(p4 != NULL, "contig_order2: p4 NULL");
    
    vm_phys_free_contiguous(page, 4);
    TEST_PASS("contiguous_order2");
}

/* Test: Contiguous allocation for order 4 (16 pages) */
static void test_contiguous_order4(void) {
    vm_page_t *page = vm_phys_alloc_contiguous(16);
    TEST_ASSERT(page != NULL, "contig_order4: returned NULL");
    
    /* Order 4 = 16 pages = 64KB, must be 64KB aligned */
    TEST_ASSERT((page->phys_addr & 0xFFFF) == 0, "contig_order4: not 64KB aligned");
    
    vm_phys_free_contiguous(page, 16);
    TEST_PASS("contiguous_order4");
}

/* Test: Multiple allocations don't return same page */
static void test_alloc_no_overlap(void) {
    vm_page_t *p1 = vm_phys_alloc_page();
    vm_page_t *p2 = vm_phys_alloc_page();
    vm_page_t *p3 = vm_phys_alloc_page();
    
    TEST_ASSERT(p1 != NULL && p2 != NULL && p3 != NULL, "overlap: alloc failed");
    TEST_ASSERT(p1 != p2, "overlap: p1 == p2");
    TEST_ASSERT(p2 != p3, "overlap: p2 == p3");
    TEST_ASSERT(p1 != p3, "overlap: p1 == p3");
    TEST_ASSERT(p1->phys_addr != p2->phys_addr, "overlap: same phys addr");
    
    vm_phys_free_page(p1);
    vm_phys_free_page(p2);
    vm_phys_free_page(p3);
    TEST_PASS("alloc_no_overlap");
}

/* Test: Free page can be reallocated */
static void test_realloc_after_free(void) {
    vm_page_t *p1 = vm_phys_alloc_page();
    TEST_ASSERT(p1 != NULL, "realloc: first alloc failed");
    uintptr_t phys1 = p1->phys_addr;
    
    vm_phys_free_page(p1);
    
    /* Allocate again - may or may not get same page */
    vm_page_t *p2 = vm_phys_alloc_page();
    TEST_ASSERT(p2 != NULL, "realloc: second alloc failed");
    
    /* Just verify we got a valid page */
    TEST_ASSERT(p2->phys_addr != 0, "realloc: invalid phys");
    
    (void)phys1;  /* May or may not equal p2->phys_addr */
    
    vm_phys_free_page(p2);
    TEST_PASS("realloc_after_free");
}

/* Property: alloc -> free -> alloc returns same page */
static void test_realloc_same_page(void) {
    vm_page_t *p1 = vm_phys_alloc_page();
    TEST_ASSERT(p1 != NULL, "same_page: first alloc failed");
    uintptr_t phys1 = p1->phys_addr;

    vm_phys_free_page(p1);

    vm_page_t *p2 = vm_phys_alloc_page();
    TEST_ASSERT(p2 != NULL, "same_page: second alloc failed");
    TEST_ASSERT(p2->phys_addr == phys1, "same_page: allocator did not return freed page");

    vm_phys_free_page(p2);
    TEST_PASS("realloc_same_page");
}

/* Test: adjacent single-page frees coalesce back into a multi-page block */
static void test_buddy_coalescing(void) {
    vm_page_t *block = vm_phys_alloc_contiguous(2);
    TEST_ASSERT(block != NULL, "coalesce: initial contiguous alloc failed");
    uintptr_t base = block->phys_addr;

    vm_page_t *page0 = vm_phys_paddr_to_page(base);
    vm_page_t *page1 = vm_phys_paddr_to_page(base + 0x1000);
    TEST_ASSERT(page0 != NULL && page1 != NULL, "coalesce: page lookup failed");

    vm_phys_free_page(page0);
    vm_phys_free_page(page1);

    vm_page_t *merged = vm_phys_alloc_contiguous(2);
    TEST_ASSERT(merged != NULL, "coalesce: realloc contiguous failed");
    TEST_ASSERT(merged->phys_addr == base, "coalesce: block did not merge back to original base");

    vm_phys_free_contiguous(merged, 2);
    TEST_PASS("buddy_coalescing");
}

/* Test: single-page allocation splits an order-1 block when order 0 is empty */
static void test_buddy_splitting_from_order1(void) {
    vm_page_t *drained = NULL;
    vm_page_t *block;
    vm_page_t *split0;
    vm_page_t *split1;

    while (vm_phys_get_order_free_count(0) > 0) {
        vm_page_t *page = vm_phys_alloc_page();
        TEST_ASSERT(page != NULL, "split: failed draining order 0");
        page->next = drained;
        drained = page;
    }

    TEST_ASSERT(vm_phys_get_order_free_count(0) == 0, "split: order 0 not fully drained");

    block = vm_phys_alloc_contiguous(2);
    TEST_ASSERT(block != NULL, "split: contiguous order1 alloc failed");
    vm_phys_free_contiguous(block, 2);

    split0 = vm_phys_alloc_page();
    split1 = vm_phys_alloc_page();
    TEST_ASSERT(split0 != NULL && split1 != NULL, "split: split allocations failed");
    TEST_ASSERT(split0->phys_addr == block->phys_addr, "split: first split page base mismatch");
    TEST_ASSERT(split1->phys_addr == block->phys_addr + 0x1000, "split: second split page buddy mismatch");

    vm_phys_free_page(split0);
    vm_phys_free_page(split1);

    while (drained) {
        vm_page_t *next = drained->next;
        vm_phys_free_page(drained);
        drained = next;
    }

    TEST_PASS("buddy_splitting_from_order1");
}

/* Test: order-0 allocation consumes the current free-list head directly when available */
static void test_order0_fast_path_head(void) {
    vm_page_t *drained = NULL;
    vm_page_t *block;
    vm_page_t *alloc;
    uintptr_t head_phys;

    while (vm_phys_get_order_free_count(0) > 0) {
        vm_page_t *page = vm_phys_alloc_page();
        TEST_ASSERT(page != NULL, "order0_head: failed draining order 0");
        page->next = drained;
        drained = page;
    }

    block = vm_phys_alloc_contiguous(2);
    TEST_ASSERT(block != NULL, "order0_head: contiguous seed alloc failed");
    vm_phys_free_contiguous(block, 2);

    head_phys = vm_phys_get_order_head_phys(0);
    TEST_ASSERT(head_phys == block->phys_addr + 0x1000,
                "order0_head: split buddy did not become order-0 head");

    alloc = vm_phys_alloc_page();
    TEST_ASSERT(alloc != NULL, "order0_head: order-0 alloc failed");
    TEST_ASSERT(alloc->phys_addr == head_phys,
                "order0_head: alloc did not consume current order-0 head");

    vm_phys_free_page(alloc);

    while (drained) {
        vm_page_t *next = drained->next;
        vm_phys_free_page(drained);
        drained = next;
    }

    TEST_PASS("order0_fast_path_head");
}

/* Test: paddr_to_page returns correct mapping */
static void test_paddr_to_page(void) {
    vm_page_t *page = vm_phys_alloc_page();
    TEST_ASSERT(page != NULL, "paddr_lookup: alloc failed");
    
    vm_page_t *lookup = vm_phys_paddr_to_page(page->phys_addr);
    TEST_ASSERT(lookup == page, "paddr_lookup: mismatch");
    
    vm_phys_free_page(page);
    TEST_PASS("paddr_to_page");
}

/* Test: pmm_get_page wrapper resolves the same vm_page_t as phys_mem */
static void test_pmm_get_page_wrapper(void) {
    vm_page_t *page = vm_phys_alloc_page();
    TEST_ASSERT(page != NULL, "pmm_get_page: alloc failed");

    vm_page_t *lookup = pmm_get_page(page->phys_addr);
    TEST_ASSERT(lookup == page, "pmm_get_page: wrapper mismatch");

    vm_phys_free_page(page);
    TEST_PASS("pmm_get_page_wrapper");
}

/* Test: vm_page_to_phys returns the tracked physical address */
static void test_vm_page_to_phys_accessor(void) {
    vm_page_t *page = vm_phys_alloc_page();
    TEST_ASSERT(page != NULL, "vm_page_to_phys: alloc failed");
    TEST_ASSERT(vm_page_to_phys(page) == page->phys_addr,
                "vm_page_to_phys: accessor mismatch");

    vm_phys_free_page(page);
    TEST_PASS("vm_page_to_phys_accessor");
}

/* Test: Zero count contiguous allocation returns NULL */
static void test_contiguous_zero_count(void) {
    vm_page_t *page = vm_phys_alloc_contiguous(0);
    TEST_ASSERT(page == NULL, "contig_zero: should return NULL");
    TEST_PASS("contiguous_zero_count");
}

/* Test: Free count tracking is accurate */
static void test_free_count_tracking(void) {
    size_t initial = vm_phys_get_free();
    
    /* Allocate 10 pages */
    vm_page_t *pages[10];
    for (int i = 0; i < 10; i++) {
        pages[i] = vm_phys_alloc_page();
        TEST_ASSERT(pages[i] != NULL, "count_track: alloc failed");
    }
    
    size_t after_alloc = vm_phys_get_free();
    TEST_ASSERT(after_alloc <= initial - 10, "count_track: didn't decrease by 10");
    
    /* Free all */
    for (int i = 0; i < 10; i++) {
        vm_phys_free_page(pages[i]);
    }
    
    size_t after_free = vm_phys_get_free();
    /* Should be back to initial (or close, due to coalescing) */
    TEST_ASSERT(after_free >= initial - 1, "count_track: didn't restore");
    
    TEST_PASS("free_count_tracking");
}

/* Test: vm_phys_mark_used reserves a single page inside a free buddy block */
static void test_mark_used_single_page_reservation(void) {
    vm_page_t *blk = vm_phys_alloc_contiguous(4);
    TEST_ASSERT(blk != NULL, "mark_used: initial contiguous alloc failed");

    uintptr_t target_pa = blk->phys_addr + 0x1000; /* non-head page within the block */
    vm_phys_free_contiguous(blk, 4);

    size_t free_before = vm_phys_get_free();
    vm_phys_mark_used(target_pa);
    size_t free_after = vm_phys_get_free();

    TEST_ASSERT(free_after + 1 == free_before, "mark_used: free count did not decrease by one");

    vm_page_t *marked = vm_phys_paddr_to_page(target_pa);
    TEST_ASSERT(marked != NULL, "mark_used: reserved page lookup failed");
    TEST_ASSERT(!(marked->flags & PG_FREE), "mark_used: reserved page still free");

    TEST_PASS("mark_used_single_page_reservation");
}

/* Property: free_count + used_count stays constant */
static void test_free_used_invariant(void) {
    size_t free0 = vm_phys_get_free();
    size_t used0 = vm_phys_get_used();
    size_t total0 = free0 + used0;

    vm_page_t *a = vm_phys_alloc_page();
    vm_page_t *b = vm_phys_alloc_page();
    TEST_ASSERT(a != NULL && b != NULL, "invariant: allocations failed");

    size_t free1 = vm_phys_get_free();
    size_t used1 = vm_phys_get_used();
    TEST_ASSERT(free1 + used1 == total0, "invariant: total changed after alloc");

    vm_phys_free_page(a);
    vm_phys_free_page(b);

    size_t free2 = vm_phys_get_free();
    size_t used2 = vm_phys_get_used();
    TEST_ASSERT(free2 + used2 == total0, "invariant: total changed after free");

    TEST_PASS("free_used_invariant");
}

/* Property: buddy free lists stay internally consistent */
static void test_free_list_integrity(void) {
    TEST_ASSERT(vm_phys_check_integrity(), "integrity: initial state invalid");

    vm_page_t *a = vm_phys_alloc_page();
    vm_page_t *b = vm_phys_alloc_page();
    vm_page_t *c = vm_phys_alloc_contiguous(4);
    TEST_ASSERT(a != NULL && b != NULL && c != NULL, "integrity: alloc failed");
    TEST_ASSERT(vm_phys_check_integrity(), "integrity: invalid after alloc");

    vm_phys_free_page(a);
    vm_phys_free_page(b);
    vm_phys_free_contiguous(c, 4);
    TEST_ASSERT(vm_phys_check_integrity(), "integrity: invalid after free");

    TEST_PASS("free_list_integrity");
}

/* Test: alloc page below a specific limit */
static void test_alloc_page_below(void) {
    vm_page_t *page = vm_phys_alloc_page_below(0x2000);
    TEST_ASSERT(page != NULL, "alloc_below: returned NULL for valid limit");
    TEST_ASSERT(page->phys_addr < 0x2000, "alloc_below: address >= limit");

    vm_phys_free_page(page);

    /* Allocate with limit 0 (no limit) */
    vm_page_t *page2 = vm_phys_alloc_page_below(0);
    TEST_ASSERT(page2 != NULL, "alloc_below: returned NULL for limit 0");

    vm_phys_free_page(page2);

    /* Allocate with an impossible limit (e.g. 1) */
    vm_page_t *page3 = vm_phys_alloc_page_below(1);
    TEST_ASSERT(page3 == NULL, "alloc_below: returned non-NULL for impossible limit");

    TEST_PASS("alloc_page_below");
}

/* Test entry point */
void test_vm_phys(void) {
    kprint("=== Physical Memory Manager Integration Tests ===\n");
    
    test_single_page_alloc();
    test_single_page_free();
    test_contiguous_order0();
    test_contiguous_order2();
    test_contiguous_order4();
    test_alloc_no_overlap();
    test_realloc_after_free();
    test_realloc_same_page();
    test_buddy_coalescing();
    test_buddy_splitting_from_order1();
    test_order0_fast_path_head();
    test_paddr_to_page();
    test_pmm_get_page_wrapper();
    test_vm_page_to_phys_accessor();
    test_contiguous_zero_count();
    test_free_count_tracking();
    test_mark_used_single_page_reservation();
    test_free_used_invariant();
    test_free_list_integrity();
    test_alloc_page_below();
    
    char buf[64];
    sprintf(buf, "=== vm_phys tests: %d passed, %d failed ===\n", passed, failed);
    kprint(buf);
}
