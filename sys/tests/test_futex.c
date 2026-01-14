#include <sys/futex.h>
#include <errno.h>
#include <sys/proc.h>
#include <arch/i386/pmap.h>
#include <pmm.h>
#include <kern/console.h>
#include <stdio.h>
#include <string.h>
#include "tests.h"

// Declaration of sys_futex (not in header usually)
extern int sys_futex(int *uaddr, int op, int val, void *timeout, int *uaddr2, int val3);

void test_futex(void) {
    kprint("TEST: futex\n");
    
    // 1. Allocate a page to act as user memory
    void *page = pmm_alloc_block();
    if (!page) {
        kprint("  [FAIL] OOM allocating page\n");
        return;
    }
    
    // 2. Map it at a user address (e.g., 0x20000000 = 512MB)
    // Ensure it doesn't conflict (kernel test env usually clean)
    uintptr_t uaddr = 0x20000000;
    uint32_t pa = (uint32_t)((uintptr_t)page - 0xC0000000);
    
    // Map with User permissions
    // Note: pmap_enter usually flushes TLB
    pmap_enter(pmap_kernel(), uaddr, pa, VM_PROT_READ | VM_PROT_WRITE, 0); // User is implicit if < KernelBase? No, need US bit?
    // pmap_enter implementation adds PTE_U if < 0xC0000000 automatically.
    
    // 3. Access via user pointer (kernel can read user pages)
    volatile int *ptr = (int *)uaddr;
    *ptr = 1;
    
    // 4. Test FUTEX_WAIT mismatch (EAGAIN)
    // *ptr is 1. Wait for 0. Should fail.
    int ret = sys_futex((int*)uaddr, FUTEX_WAIT, 0, NULL, NULL, 0);
    if (ret == -EAGAIN) {
        kprint("  [PASS] FUTEX_WAIT mismatch (val=1, exp=0) -> EAGAIN\n");
    } else {
        kprint("  [FAIL] FUTEX_WAIT mismatch (expected -EAGAIN)\n");
    }
    
    // 5. Test FUTEX_WAKE (0 waiters)
    // Should return 0
    ret = sys_futex((int*)uaddr, FUTEX_WAKE, 1, NULL, NULL, 0);
    if (ret == 0) {
        kprint("  [PASS] FUTEX_WAKE (no waiters) -> 0\n");
    } else {
        kprint("  [FAIL] FUTEX_WAKE (expected 0)\n");
    }
    
    // Cleanup
    pmap_remove(pmap_kernel(), uaddr);
    pmm_free_block(page);
}
