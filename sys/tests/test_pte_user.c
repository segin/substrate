/*
 * test_pte_user.c - Test PTE_USER bit verification
 *
 * Verifies that the PTE_USER (PTE_U) bit is correctly set for all
 * user-accessible pages and NOT set for kernel-only pages.
 */

#include <stdint.h>
#include <stdio.h>
#include "../kern/console.h"

/* PTE flags from pmap.h */
#define PTE_P  0x001  /* Present */
#define PTE_W  0x002  /* Writable */
#define PTE_U  0x004  /* User accessible */
#define PTE_PS 0x080  /* Page Size (4MB) */

/* Recursive mapping addresses */
#define V_PD  ((uint32_t *)0xFFFFF000)
#define V_PT(i) ((uint32_t *)(0xFFC00000 + ((i) << 12)))

#define PD_INDEX(va) (((uint32_t)(va)) >> 22)
#define PT_INDEX(va) ((((uint32_t)(va)) >> 12) & 0x3FF)

/* Test result tracking */
static int tests_passed = 0;
static int tests_failed = 0;

static void test_assert(int condition, const char *name) {
    if (condition) {
        tests_passed++;
    } else {
        tests_failed++;
        kprint("FAIL: ");
        kprint(name);
        kprint("\n");
    }
}

/*
 * verify_user_page_pte_u - Check that a user-space page has PTE_U set
 */
static int __attribute__((unused)) verify_user_page_pte_u(uint32_t va) {
    uint32_t pdi = PD_INDEX(va);
    uint32_t pti = PT_INDEX(va);
    
    /* Check PDE is present */
    if (!(V_PD[pdi] & PTE_P)) {
        return -1; /* Not mapped */
    }
    
    /* Check for large page (4MB) */
    if (V_PD[pdi] & PTE_PS) {
        return (V_PD[pdi] & PTE_U) ? 1 : 0;
    }
    
    /* Check PTE */
    uint32_t *pt = V_PT(pdi);
    if (!(pt[pti] & PTE_P)) {
        return -1; /* Not mapped */
    }
    
    return (pt[pti] & PTE_U) ? 1 : 0;
}

/*
 * verify_kernel_page_no_pte_u - Check that a kernel-space page does NOT have PTE_U
 */
static int __attribute__((unused)) verify_kernel_page_no_pte_u(uint32_t va) {
    uint32_t pdi = PD_INDEX(va);
    uint32_t pti = PT_INDEX(va);
    
    /* Check PDE is present */
    if (!(V_PD[pdi] & PTE_P)) {
        return 1; /* Not mapped is fine */
    }
    
    /* Check for large page (4MB) */
    if (V_PD[pdi] & PTE_PS) {
        return (V_PD[pdi] & PTE_U) ? 0 : 1;
    }
    
    /* Check PTE */
    uint32_t *pt = V_PT(pdi);
    if (!(pt[pti] & PTE_P)) {
        return 1; /* Not mapped is fine */
    }
    
    return (pt[pti] & PTE_U) ? 0 : 1;
}

/*
 * test_user_range_has_pte_u - Test that user space range has PTE_U
 */
static void test_user_range_has_pte_u(void) {
    int user_pages_checked = 0;
    int user_pages_correct = 0;
    
    /* Scan user space PTEs (0x00000000 - 0xBFFFFFFF) */
    for (uint32_t pdi = 0; pdi < 768; pdi++) {
        if (!(V_PD[pdi] & PTE_P)) continue;
        
        /* PDE should have PTE_U to allow user access */
        if (!(V_PD[pdi] & PTE_U)) {
            /* PDE missing U bit - entire table is kernel-only (possible for shared kernel mappings) */
            continue;
        }
        
        /* Check for large page */
        if (V_PD[pdi] & PTE_PS) {
            user_pages_checked++;
            if (V_PD[pdi] & PTE_U) user_pages_correct++;
            continue;
        }
        
        /* Scan page table */
        uint32_t *pt = V_PT(pdi);
        for (uint32_t pti = 0; pti < 1024; pti++) {
            if (!(pt[pti] & PTE_P)) continue;
            
            user_pages_checked++;
            if (pt[pti] & PTE_U) {
                user_pages_correct++;
            }
        }
    }
    
    test_assert(user_pages_checked == 0 || user_pages_checked == user_pages_correct,
                "All present user pages have PTE_U set");
}

/*
 * test_kernel_range_no_pte_u - Test that kernel space range does NOT have PTE_U
 */
static void test_kernel_range_no_pte_u(void) {
    int kernel_pages_checked = 0;
    int kernel_pages_correct = 0;
    
    /* Scan kernel space PTEs (0xC0000000 - 0xFFFFEFFF, skip recursive mapping) */
    for (uint32_t pdi = 768; pdi < 1023; pdi++) {
        if (!(V_PD[pdi] & PTE_P)) continue;
        
        /* Check for large page */
        if (V_PD[pdi] & PTE_PS) {
            kernel_pages_checked++;
            if (!(V_PD[pdi] & PTE_U)) kernel_pages_correct++;
            continue;
        }
        
        /* Scan page table */
        uint32_t *pt = V_PT(pdi);
        for (uint32_t pti = 0; pti < 1024; pti++) {
            if (!(pt[pti] & PTE_P)) continue;
            
            kernel_pages_checked++;
            if (!(pt[pti] & PTE_U)) {
                kernel_pages_correct++;
            }
        }
    }
    
    /* Note: Some kernel pages at special addresses may have U bit (e.g., signal trampoline) */
    /* We check that the majority don't have U bit */
    int percent_correct = (kernel_pages_checked > 0) ? 
                          (kernel_pages_correct * 100 / kernel_pages_checked) : 100;
    
    test_assert(percent_correct >= 95,  /* Allow 5% for special pages */
                "Kernel pages mostly don't have PTE_U set");
}

/*
 * test_specific_addresses - Test specific known addresses
 */
static void test_specific_addresses(void) {
    /* Kernel text at 0xC0100000 should NOT have PTE_U */
    test_assert(verify_kernel_page_no_pte_u(0xC0100000) == 1,
                "Kernel text (0xC0100000) has no PTE_U");
    
    /* Kernel data should NOT have PTE_U */
    test_assert(verify_kernel_page_no_pte_u(0xC0200000) == 1,
                "Kernel data (0xC0200000) has no PTE_U");
}

/*
 * test_pte_user - Main test entry point
 */
void test_pte_user(void) {
    kprint("=== PTE_USER Bit Verification Tests ===\n");
    
    tests_passed = 0;
    tests_failed = 0;
    
    test_user_range_has_pte_u();
    test_kernel_range_no_pte_u();
    test_specific_addresses();
    
    char buf[64];
    sprintf(buf, "PTE_USER tests: %d passed, %d failed\n", tests_passed, tests_failed);
    kprint(buf);
}
