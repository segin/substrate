#include <sys/types.h>
#include <sys/klog.h>
#include <sys/mm.h>
#include <arch/i386/pmap.h>

// Define PTE_PS if not visible (it is in pmap.h usually)
#ifndef PTE_PS
#define PTE_PS 0x80
#endif

void test_vm_large(void) {
    klog_info("TEST: Starting Large Page (4MB) Tests...");
    
    pmap_t kpmap = pmap_kernel();
    
    // 1. Pick a 4MB aligned virtual address in kernel space that is likely free
    // The kernel heap is usually at KERNEL_HEAP_START. 
    // Let's pick a very high address, e.g., 0xF0000000 (3.75GB)
    // Ensure it's not mapped.
    uint32_t va = 0xF0000000;
    
    if (pmap_extract(kpmap, va) != 0) {
        klog_fail("TEST: 0xF0000000 is already mapped! Choose another address.");
        return;
    }
    
    // 2. Pick a physical address. Let's use 0x400000 (4MB mark), which is usually available 
    // (first 128MB is identity mapped but we can remap or alias it)
    uint32_t pa = 0x400000;
    
    // 3. Map it
    int ret = pmap_enter_large(kpmap, va, pa, VM_PROT_READ | VM_PROT_WRITE, 0);
    if (ret != 0) {
        klog_fail("TEST: pmap_enter_large failed with %d", ret);
        return;
    }
    
    // 4. Verify Extract
    uint32_t extracted = pmap_extract(kpmap, va);
    if (extracted != pa) {
        klog_fail("TEST: pmap_extract(base) failed. Expected %p, got %p", pa, extracted);
        return;
    }
    
    // Verify Offset Extract (1MB into 4MB page)
    extracted = pmap_extract(kpmap, va + 0x100000);
    if (extracted != (pa + 0x100000)) {
        klog_fail("TEST: pmap_extract(offset) failed. Expected %p, got %p", pa + 0x100000, extracted);
        return;
    }
    
    // 5. Verify Write/Read
    volatile uint32_t *ptr = (volatile uint32_t *)va;
    *ptr = 0xDEADBEEF;
    if (*ptr != 0xDEADBEEF) {
        klog_fail("TEST: Readback failed. Expected 0xDEADBEEF, got %p", *ptr);
        return;
    }
    
    // 6. Cleanup
    pmap_remove(kpmap, va);
    if (pmap_extract(kpmap, va) != 0) {
        klog_fail("TEST: pmap_remove failed to clear mapping.");
        return;
    }
    
    klog_ok("TEST: Large Page Support Verified.");
}
