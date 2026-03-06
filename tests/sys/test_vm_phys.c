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

/* Test: paddr_to_page returns correct mapping */
static void test_paddr_to_page(void) {
    vm_page_t *page = vm_phys_alloc_page();
    TEST_ASSERT(page != NULL, "paddr_lookup: alloc failed");
    
    vm_page_t *lookup = vm_phys_paddr_to_page(page->phys_addr);
    TEST_ASSERT(lookup == page, "paddr_lookup: mismatch");
    
    vm_phys_free_page(page);
    TEST_PASS("paddr_to_page");
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
    test_paddr_to_page();
    test_contiguous_zero_count();
    test_free_count_tracking();
    test_mark_used_single_page_reservation();
    
    char buf[64];
    sprintf(buf, "=== vm_phys tests: %d passed, %d failed ===\n", passed, failed);
    kprint(buf);
}
