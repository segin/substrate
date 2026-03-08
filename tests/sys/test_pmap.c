/*
 * Unit tests for pmap_create/destroy
 */

#include <arch/i386/pmap.h>
#include <arch/i386/pmm.h>
#include <kern/console.h>
#include <stdint.h>
#include <sys/proc.h>
#include <vm/vm_page.h>

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

// Test 1: Create and destroy pmap lifecycle
void test_pmap_lifecycle(void) {
    kprint("Test: pmap lifecycle\n");
    
    pmap_t pmap = pmap_create();
    TEST_ASSERT(pmap != 0, "pmap_create returned non-NULL");
    
    // Validate it's a proper physical address
    uint32_t phys = (uintptr_t)pmap;
    TEST_ASSERT((phys & 0xFFF) == 0, "pmap is page-aligned");
    
    // Destroy it
    pmap_destroy(pmap);
    
    kprint("  PASS\n");
}

// Test 10: Replace Page Table with Large Page
void test_pmap_large_replace(void) {
    kprint("Test: Replace PT with Large Page\n");

    // 1. Create a pmap
    pmap_t pmap = pmap_create();
    TEST_ASSERT(pmap != 0, "pmap created");

    // 2. Activate it (must be active for pmap_enter_large check)
    // Save current CR3 implicitly by switching back to kernel pmap later
    pmap_activate(pmap);

    // 3. Map a 4KB page at 0x400000 (4MB)
    // This will allocate a Page Table at PDE index 1.
    uint32_t va = 0x400000;

    // Allocate a page from PMM to be safe (avoid kernel text)
    void *page_v = pmm_alloc_block();
    TEST_ASSERT(page_v != 0, "pmm_alloc_block succeeded");
    uint32_t pa_small = (uint32_t)(uintptr_t)page_v - 0xC0000000;

    // Map it
    int ret = pmap_enter(pmap, va, pa_small, VM_PROT_READ | VM_PROT_WRITE, 0);
    TEST_ASSERT(ret == 0, "pmap_enter 4KB success");

    // 4. Attempt to replace with Large Page at 0x400000
    // This requires freeing the underlying Page Table
    uint32_t pa_large = 0x400000; // Align to 4MB
    ret = pmap_enter_large(pmap, va, pa_large, VM_PROT_READ | VM_PROT_WRITE, 0);

    TEST_ASSERT(ret == 0, "pmap_enter_large should succeed (replace PT)");

    // 5. Verify mapping is correct
    uint32_t extracted = pmap_extract(pmap, va);
    TEST_ASSERT(extracted == pa_large, "Virtual address maps to new Large Page PA");

    // Verify offset
    extracted = pmap_extract(pmap, va + 0x1000);
    TEST_ASSERT(extracted == pa_large + 0x1000, "Offset mapping correct");

    // 6. Restore kernel pmap
    pmap_activate(pmap_kernel());

    // 7. Destroy pmap
    pmap_destroy(pmap);

    kprint("  PASS\n");
}

// Test 2: Multiple pmaps can coexist
void test_multiple_pmaps(void) {
    kprint("Test: multiple pmaps\n");
    
    pmap_t pmap1 = pmap_create();
    pmap_t pmap2 = pmap_create();
    pmap_t pmap3 = pmap_create();
    
    TEST_ASSERT(pmap1 != 0, "pmap1 created");
    TEST_ASSERT(pmap2 != 0, "pmap2 created");
    TEST_ASSERT(pmap3 != 0, "pmap3 created");
    
    TEST_ASSERT(pmap1 != pmap2, "pmaps have different addresses");
    TEST_ASSERT(pmap2 != pmap3, "pmaps have different addresses");
    
    pmap_destroy(pmap1);
    pmap_destroy(pmap2);
    pmap_destroy(pmap3);
    
    kprint("  PASS\n");
}

// Test: pmap_enter + pmap_extract round-trip
void test_pmap_enter_extract(void) {
    kprint("Test: pmap_enter/pmap_extract round-trip\n");

    pmap_t pmap = pmap_create();
    TEST_ASSERT(pmap != 0, "pmap created");

    void *page_v = pmm_alloc_block();
    TEST_ASSERT(page_v != 0, "page allocated");

    uint32_t va = 0x401000;
    uint32_t pa = (uint32_t)(uintptr_t)page_v - 0xC0000000;

    pmap_activate(pmap);
    int ret = pmap_enter(pmap, va, pa, VM_PROT_READ | VM_PROT_WRITE | VM_PROT_USER, 0);
    TEST_ASSERT(ret == 0, "pmap_enter succeeded");
    TEST_ASSERT(pmap_extract(pmap, va) == pa, "pmap_extract returns mapped PA");
    TEST_ASSERT(pmap_extract(pmap, va + 0x234) == pa + 0x234, "pmap_extract preserves page offset");
    pmap_activate(pmap_kernel());

    pmap_destroy(pmap);
    kprint("  PASS\n");
}

// Test 3: Cannot destroy kernel pmap
void test_kernel_pmap_protection(void) {
    kprint("Test: kernel pmap protection\n");
    
    pmap_t kernel = pmap_kernel();
    TEST_ASSERT(kernel != 0, "kernel pmap exists");
    
    // This should be a no-op
    pmap_destroy(kernel);
    
    // Kernel should still be valid
    TEST_ASSERT(pmap_kernel() == kernel, "kernel pmap unchanged");
    
    kprint("  PASS\n");
}

// Test 4: NULL pmap handling
void test_null_pmap(void) {
    kprint("Test: NULL pmap handling\n");
    
    // Should not crash
    pmap_destroy(0);
    
    kprint("  PASS\n");
}

// Check for memory leaks
static void test_memory_leak(void) {
    kprint("Test: Memory Leak Check... ");
    
    extern size_t pmm_get_used_blocks(void);
    size_t start_blocks = pmm_get_used_blocks();
    
    // Create and destroy 10 pmaps
    for (int i = 0; i < 10; i++) {
        pmap_t pmap = pmap_create();
        TEST_ASSERT(pmap != 0, "pmap created");
        pmap_destroy(pmap);
    }
    
    size_t final_blocks = pmm_get_used_blocks();
    
    // Should have same number of used blocks (no leak)
    TEST_ASSERT(start_blocks == final_blocks, "no memory leak detected");
    
    kprint("  PASS\n");
}

static void test_pmap_destroy_reclaims_mapped_pages(void) {
    extern size_t pmm_get_used_blocks(void);
    size_t start_blocks;
    size_t mapped_blocks;

    kprint("Test: pmap_destroy reclaims mapped pages\n");

    start_blocks = pmm_get_used_blocks();

    pmap_t pmap = pmap_create();
    TEST_ASSERT(pmap != 0, "mapped_destroy: pmap created");

    void *page0_v = pmm_alloc_block();
    void *page1_v = pmm_alloc_block();
    TEST_ASSERT(page0_v != 0 && page1_v != 0, "mapped_destroy: backing pages allocated");

    uint32_t pa0 = (uint32_t)(uintptr_t)page0_v - 0xC0000000;
    uint32_t pa1 = (uint32_t)(uintptr_t)page1_v - 0xC0000000;

    pmap_activate(pmap);
    TEST_ASSERT(pmap_enter(pmap, 0x401000, pa0, VM_PROT_READ | VM_PROT_WRITE | VM_PROT_USER, 0) == 0,
                "mapped_destroy: first mapping created");
    TEST_ASSERT(pmap_enter(pmap, 0x402000, pa1, VM_PROT_READ | VM_PROT_WRITE | VM_PROT_USER, 0) == 0,
                "mapped_destroy: second mapping created");
    TEST_ASSERT(pmap->resident_count == 2, "mapped_destroy: resident_count tracks mapped pages");
    pmap_activate(pmap_kernel());

    mapped_blocks = pmm_get_used_blocks();
    TEST_ASSERT(mapped_blocks > start_blocks, "mapped_destroy: PMM usage increased after mappings");

    pmap_destroy(pmap);

    TEST_ASSERT(pmm_get_used_blocks() == start_blocks,
                "mapped_destroy: destroy reclaimed resident pages and page-table backing");
    kprint("  PASS\n");
}
// Test 5: PSE 4MB Page Support
void test_pmap_pse(void) {
    kprint("Test: PSE 4MB Mapping\n");
    pmap_t pmap = pmap_create();
    TEST_ASSERT(pmap != 0, "pmap created");

    // Activate pmap (required for pmap_enter_large)
    pmap_activate(pmap);

    // Try valid 4MB alignment
    uint32_t va = 0x800000; // 8MB
    uint32_t pa = 0x400000; // 4MB
    int ret = pmap_enter_large(pmap, va, pa, VM_PROT_READ | VM_PROT_WRITE | VM_PROT_USER, 0);
    TEST_ASSERT(ret == 0, "pmap_enter_large valid alignment");

    // Try invalid alignment
    ret = pmap_enter_large(pmap, va + 0x1000, pa, 0, 0);
    TEST_ASSERT(ret != 0, "pmap_enter_large invalid VA alignment");
    
    ret = pmap_enter_large(pmap, va, pa + 0x1000, 0, 0);
    TEST_ASSERT(ret != 0, "pmap_enter_large invalid PA alignment");

    // Restore kernel pmap
    pmap_activate(pmap_kernel());

    pmap_destroy(pmap);
    kprint("  PASS\n");
}

// Test 6: pmap_check consistency check
void test_pmap_check(void) {
    kprint("Test: pmap_check consistency\n");
    pmap_t pmap = pmap_create();
    TEST_ASSERT(pmap != 0, "pmap created");

    int ret = pmap_check(pmap);
    TEST_ASSERT(ret == 0, "pmap_check returns 0 for valid pmap");

    pmap_destroy(pmap);
    kprint("  PASS\n");
}

// Test 7: pmap_dump smoke test (just verify no crash)
void test_pmap_dump(void) {
    kprint("Test: pmap_dump smoke test\n");
    pmap_t pmap = pmap_create();
    TEST_ASSERT(pmap != 0, "pmap created");

    pmap_dump(pmap);  // Should not crash

    pmap_destroy(pmap);
    kprint("  PASS\n");
}

void test_pmap_mapping_counters(void) {
    kprint("Test: pmap mapping counters\n");

    pmap_t pmap = pmap_create();
    TEST_ASSERT(pmap != 0, "pmap created");

    void *page_v = pmm_alloc_block();
    TEST_ASSERT(page_v != 0, "page allocated");

    uint32_t va = 0x404000;
    uint32_t pa = (uint32_t)(uintptr_t)page_v - 0xC0000000;

    pmap_activate(pmap);
    TEST_ASSERT(pmap_enter(pmap, va, pa, VM_PROT_READ | VM_PROT_WRITE | VM_PROT_USER, 0) == 0,
                "mapping created");
    TEST_ASSERT(pmap->wired_count == 0, "wired_count remains zero without wired mappings");
    TEST_ASSERT(pmap->stats.faults == 0, "fault count starts at zero for direct map");
    TEST_ASSERT(pmap->stats.cow_faults == 0, "cow fault count starts at zero");
    TEST_ASSERT(pmap->resident_count == 1, "resident_count increments");
    TEST_ASSERT(pmap->mapped_count == 1, "mapped_count increments");

    pmap_remove(pmap, va);
    TEST_ASSERT(pmap->resident_count == 0, "resident_count decrements");
    TEST_ASSERT(pmap->mapped_count == 0, "mapped_count decrements");

    pmap_activate(pmap_kernel());
    pmm_free_block(page_v);
    pmap_destroy(pmap);
    kprint("  PASS\n");
}

void test_pmap_growkernel_sync(void) {
    kprint("Test: pmap_growkernel sync\n");

    pmap_t pmap = pmap_create();
    TEST_ASSERT(pmap != 0, "pmap created");

    pmap_t kernel = pmap_kernel();
    uint32_t pdi = 0;
    for (uint32_t i = 1020; i < 1023; i++) {
        if (!(kernel->pdir[i] & PTE_P)) {
            pdi = i;
            break;
        }
    }
    TEST_ASSERT(pdi != 0, "free kernel PDE found");

    uint32_t va = pdi << 22;
    TEST_ASSERT((pmap->pdir[pdi] & PTE_P) == 0, "child pmap initially lacks PDE");

    void *page_v = pmm_alloc_block();
    TEST_ASSERT(page_v != 0, "kernel page allocated");
    uint32_t pa = (uint32_t)(uintptr_t)page_v - 0xC0000000;

    pmap_activate(kernel);
    pmap_kenter(va, pa);
    TEST_ASSERT(pmap->pdir[pdi] == kernel->pdir[pdi], "kernel PDE propagated to existing pmap");

    pmap_activate(pmap);
    TEST_ASSERT(pmap_extract(pmap, va) == pa, "propagated kernel mapping visible");

    pmap_activate(kernel);
    pmap_kremove(va);
    TEST_ASSERT((kernel->pdir[pdi] & PTE_P) != 0, "kernel PDE retained for cleanup");

    uint32_t pt_phys = kernel->pdir[pdi] & PTE_FRAME;
    kernel->pdir[pdi] = 0;
    pmap->pdir[pdi] = 0;
    pmap_invalidate_page(va);
    pmap_invalidate_page((uint32_t)V_PT(pdi));
    pmm_free_block((void *)(uintptr_t)(pt_phys + 0xC0000000));
    pmm_free_block(page_v);

    pmap_destroy(pmap);
    kprint("  PASS\n");
}

// Test 8: PGE detection via CR4
void test_pge_detection(void) {
    kprint("Test: PGE detection\n");
    
    // Check if PGE is enabled in CR4
    uint32_t cr4;
    __asm__ volatile("mov %%cr4, %0" : "=r"(cr4));
    
    int pge_enabled = (cr4 >> 7) & 1;
    if (pge_enabled) {
        kprint("  PGE is enabled in CR4\n");
    } else {
        kprint("  PGE is NOT enabled (may be unsupported CPU)\n");
    }
    
    // Also verify PGE bit via CPUID
    uint32_t eax, ebx, ecx, edx;
    __asm__ volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(1));
    int has_pge = (edx >> 13) & 1;
    
    if (has_pge && pge_enabled) {
        kprint("  CPUID reports PGE support, CR4.PGE enabled - PASS\n");
    } else if (!has_pge) {
        kprint("  CPUID reports no PGE support - SKIP\n");
    } else {
        kprint("  PGE supported but not enabled - WARN\n");
    }
}

// Test 9: Global page flush function (smoke test)
void test_pge_global_flush(void) {
    kprint("Test: pmap_flush_global_pages\n");
    
    // Just verify it doesn't crash
    pmap_flush_global_pages();
    
    // Verify stats incremented
    struct pmap_stats stats;
    sys_pmap_stats(&stats);
    TEST_ASSERT(stats.tlb_full_flush_count > 0, "tlb_full_flush_count incremented");
    
    kprint("  PASS\n");
}

// Test: reference/modify tracking helpers
void test_pmap_refmod_tracking(void) {
    kprint("Test: pmap ref/modify tracking\n");

    pmap_t pmap = pmap_create();
    TEST_ASSERT(pmap != 0, "pmap created");

    void *page_v = pmm_alloc_block();
    TEST_ASSERT(page_v != 0, "page allocated");

    uint32_t va = 0x403000;
    uint32_t pa = (uint32_t)(uintptr_t)page_v - 0xC0000000;

    pmap_activate(pmap);
    TEST_ASSERT(pmap_enter(pmap, va, pa, VM_PROT_READ | VM_PROT_WRITE | VM_PROT_USER, 0) == 0,
                "mapping created");

    uint32_t pdi = PD_INDEX(va);
    uint32_t pti = PT_INDEX(va);
    V_PT(pdi)[pti] |= PTE_A | PTE_D;

    TEST_ASSERT(pmap_is_referenced(pmap, va) == 1, "A-bit visible");
    TEST_ASSERT(pmap_is_modified(pmap, va) == 1, "D-bit visible");
    TEST_ASSERT(pmap_is_referenced_range(pmap, va, va + 0x1000) == 1, "referenced range count");
    TEST_ASSERT(pmap_is_modified_range(pmap, va, va + 0x1000) == 1, "modified range count");

    pmap_clear_reference(pmap, va);
    pmap_clear_modify(pmap, va);
    TEST_ASSERT(pmap_is_referenced(pmap, va) == 0, "A-bit cleared");
    TEST_ASSERT(pmap_is_modified(pmap, va) == 0, "D-bit cleared");

    V_PT(pdi)[pti] |= PTE_A | PTE_D;
    struct vm_page *page = pmm_get_page(pa);
    TEST_ASSERT(page != 0, "vm_page found for mapping");
    TEST_ASSERT(pmap_test_and_clear_ref(page) == 1, "test_and_clear_ref succeeds");
    TEST_ASSERT(pmap_is_referenced(pmap, va) == 0, "A-bit cleared through pv list");
    TEST_ASSERT(pmap_test_and_clear_modify(page) == 1, "test_and_clear_modify succeeds");
    TEST_ASSERT(pmap_is_modified(pmap, va) == 0, "D-bit cleared through pv list");

    pmap_activate(pmap_kernel());
    pmap_destroy(pmap);
    kprint("  PASS\n");
}

// Test: pmap_protect upgrade/downgrade
void test_pmap_protect_rw(void) {
    kprint("Test: pmap_protect upgrade/downgrade\n");

    pmap_t pmap = pmap_create();
    TEST_ASSERT(pmap != 0, "pmap created");

    void *page_v = pmm_alloc_block();
    TEST_ASSERT(page_v != 0, "page allocated");

    uint32_t va = 0x402000;
    uint32_t pa = (uint32_t)(uintptr_t)page_v - 0xC0000000;

    pmap_activate(pmap);
    TEST_ASSERT(pmap_enter(pmap, va, pa, VM_PROT_READ | VM_PROT_USER, 0) == 0, "read-only mapping created");

    uint32_t pdi = PD_INDEX(va);
    uint32_t pti = PT_INDEX(va);
    TEST_ASSERT((V_PT(pdi)[pti] & PTE_W) == 0, "initial mapping is read-only");

    TEST_ASSERT(pmap_protect(pmap, va, va + 0x1000, VM_PROT_READ | VM_PROT_WRITE | VM_PROT_USER) == 0,
                "pmap_protect upgrade succeeds");
    TEST_ASSERT((V_PT(pdi)[pti] & PTE_W) != 0, "upgrade sets write bit");

    TEST_ASSERT(pmap_protect(pmap, va, va + 0x1000, VM_PROT_READ | VM_PROT_USER) == 0,
                "pmap_protect downgrade succeeds");
    TEST_ASSERT((V_PT(pdi)[pti] & PTE_W) == 0, "downgrade clears write bit");

    pmap_activate(pmap_kernel());
    pmap_destroy(pmap);
    kprint("  PASS\n");
}

// Test: pmap_fork + COW fault keeps parent isolated
void test_pmap_fork_cow_fault(void) {
    kprint("Test: pmap_fork COW fault isolation\n");

    pmap_t parent = pmap_create();
    TEST_ASSERT(parent != 0, "parent pmap created");

    void *page_v = pmm_alloc_block();
    TEST_ASSERT(page_v != 0, "parent page allocated");

    uint32_t va = 0x404000;
    uint32_t pa = (uint32_t)(uintptr_t)page_v - 0xC0000000;

    pmap_activate(parent);
    TEST_ASSERT(pmap_enter(parent, va, pa, VM_PROT_READ | VM_PROT_WRITE | VM_PROT_USER, 0) == 0,
                "parent mapping created");
    *(volatile uint32_t *)va = 0x11223344;

    pmap_t child = pmap_fork(parent);
    TEST_ASSERT(child != 0, "child pmap created");

    process_t fake_proc = {0};
    process_t *saved_proc = current_process;
    fake_proc.pmap = child;
    current_process = &fake_proc;

    pmap_activate(child);
    TEST_ASSERT(pmap_page_is_cow(child, va) == 1, "child mapping is COW");
    TEST_ASSERT(pmap_fault(0x3, va) == 1, "COW fault handled");
    *(volatile uint32_t *)va = 0x55667788;

    uint32_t child_pa = pmap_extract(child, va);
    TEST_ASSERT(child_pa != 0, "child mapping still present");

    pmap_activate(parent);
    uint32_t parent_pa = pmap_extract(parent, va);
    TEST_ASSERT(parent_pa != 0, "parent mapping still present");
    TEST_ASSERT(parent_pa != child_pa, "child received a private page");
    TEST_ASSERT(*(volatile uint32_t *)va == 0x11223344, "parent contents unchanged");

    current_process = saved_proc;
    pmap_activate(pmap_kernel());
    pmap_destroy(child);
    pmap_destroy(parent);
    kprint("  PASS\n");
}

// Test: pmap_copy handles mixed private and shared ranges
void test_pmap_copy_mixed(void) {
    kprint("Test: pmap_copy mixed private/shared\n");

    pmap_t src = pmap_create();
    pmap_t dst = pmap_create();
    TEST_ASSERT(src != 0, "source pmap created");
    TEST_ASSERT(dst != 0, "destination pmap created");

    void *private_v = pmm_alloc_block();
    void *shared_v = pmm_alloc_block();
    TEST_ASSERT(private_v != 0, "private page allocated");
    TEST_ASSERT(shared_v != 0, "shared page allocated");

    uint32_t va_private = 0x405000;
    uint32_t va_shared = 0x406000;
    uint32_t pa_private = (uint32_t)(uintptr_t)private_v - 0xC0000000;
    uint32_t pa_shared = (uint32_t)(uintptr_t)shared_v - 0xC0000000;

    struct vm_page *private_page = pmm_get_page(pa_private);
    TEST_ASSERT(private_page != 0, "private vm_page found");
    private_page->flags |= PG_PRIVATE;

    pmap_activate(src);
    TEST_ASSERT(pmap_enter(src, va_private, pa_private, VM_PROT_READ | VM_PROT_WRITE | VM_PROT_USER, 0) == 0,
                "private source mapping created");
    TEST_ASSERT(pmap_enter(src, va_shared, pa_shared, VM_PROT_READ | VM_PROT_WRITE | VM_PROT_USER, 0) == 0,
                "shared source mapping created");

    TEST_ASSERT(pmap_copy(dst, src, va_private, va_shared + 0x1000, 1) == 0, "pmap_copy succeeded");
    TEST_ASSERT((V_PT(PD_INDEX(va_shared))[PT_INDEX(va_shared)] & PTE_W) == 0,
                "source shared mapping downgraded to COW");

    pmap_activate(dst);
    uint32_t dst_private_pa = pmap_extract(dst, va_private);
    uint32_t dst_shared_pa = pmap_extract(dst, va_shared);
    TEST_ASSERT(dst_private_pa != 0, "private mapping copied");
    TEST_ASSERT(dst_shared_pa != 0, "shared mapping copied");
    TEST_ASSERT(dst_private_pa != pa_private, "private mapping duplicated");
    TEST_ASSERT(dst_shared_pa == pa_shared, "shared mapping reused");
    TEST_ASSERT((V_PT(PD_INDEX(va_shared))[PT_INDEX(va_shared)] & PTE_W) == 0,
                "destination shared mapping is read-only");

    struct vm_page *dst_private_page = pmm_get_page(dst_private_pa);
    TEST_ASSERT(dst_private_page != 0, "copied private vm_page found");
    TEST_ASSERT((dst_private_page->flags & PG_PRIVATE) != 0, "copied private page stays private");

    pmap_activate(pmap_kernel());
    pmap_destroy(dst);
    pmap_destroy(src);
    kprint("  PASS\n");
}

// Test: large page remove path
void test_pmap_large_remove(void) {
    kprint("Test: pmap_remove large page\n");

    pmap_t pmap = pmap_create();
    TEST_ASSERT(pmap != 0, "pmap created");

    uint32_t va = 0x800000;
    uint32_t pa = 0x400000;

    pmap_activate(pmap);
    TEST_ASSERT(pmap_enter_large(pmap, va, pa, VM_PROT_READ | VM_PROT_WRITE | VM_PROT_USER, 0) == 0,
                "large mapping created");
    TEST_ASSERT(pmap_extract(pmap, va) == pa, "large mapping visible");

    pmap_remove(pmap, va);
    TEST_ASSERT(pmap_extract(pmap, va) == 0, "large mapping removed");

    pmap_activate(pmap_kernel());
    pmap_destroy(pmap);
    kprint("  PASS\n");
}

static void itoa(int val, char *buf) {
    if (val == 0) {
        buf[0] = '0';
        buf[1] = '\0';
        return;
    }

    int is_neg = 0;
    unsigned int uval;

    if (val < 0) {
        is_neg = 1;
        uval = (unsigned int)(-(val + 1)) + 1; // Handle INT_MIN
    } else {
        uval = val;
    }

    char tmp[32];
    int k = 0;
    while (uval > 0) {
        tmp[k++] = (uval % 10) + '0';
        uval /= 10;
    }

    if (is_neg) {
        tmp[k++] = '-';
    }

    // Reverse into buf
    int i = 0;
    while (k > 0) {
        buf[i++] = tmp[--k];
    }
    buf[i] = '\0';
}

void run_pmap_tests(void) {
    char buf[32];

    kprint("\n=== PMAP Unit Tests ===\n");
    
    test_pmap_lifecycle();
    test_multiple_pmaps();
    test_kernel_pmap_protection();
    test_null_pmap();
    test_pmap_enter_extract();
    test_pmap_pse();
    test_pmap_check();
    test_pmap_dump();
    test_pmap_mapping_counters();
    test_pmap_growkernel_sync();
    test_pge_detection();
    test_pge_global_flush();
    test_pmap_refmod_tracking();
    test_pmap_protect_rw();
    test_pmap_fork_cow_fault();
    test_pmap_copy_mixed();
    test_pmap_large_replace();
    test_pmap_large_remove();
    test_memory_leak();
    test_pmap_destroy_reclaims_mapped_pages();
    
    kprint("\nResults: ");
    kprint("Passed: ");
    itoa(tests_passed, buf);
    kprint(buf);
    kprint(" Failed: ");
    itoa(tests_failed, buf);
    kprint(buf);
    kprint("\n");
}
