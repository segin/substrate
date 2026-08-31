#include <vm/vm_page.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

/*
 * VM Page Unit Tests
 */

bool test_vm_page_queue_ops(void) {
    vm_page_t p1, p2;
    memset(&p1, 0, sizeof(p1));
    memset(&p2, 0, sizeof(p2));
    p1.phys_addr = 0x1000;
    p2.phys_addr = 0x2000;
    p1.magic_head = p1.magic_tail = VM_PAGE_MAGIC;
    p2.magic_head = p2.magic_tail = VM_PAGE_MAGIC;
    p1.prev = p1.next = NULL;
    p2.prev = p2.next = NULL;
    p1.object = NULL;
    p2.object = NULL;
    p1.flags = 0;
    p2.flags = 0;
    p1.object = NULL;
    p2.object = NULL;
    p1.pv_list = NULL;
    p2.pv_list = NULL;
    p1.ref_count = 0;
    p2.ref_count = 0;
    
    vm_page_init();
    
    // Manually test free flow
    vm_page_free(&p1);
    vm_page_free(&p2);
    
    return true;
}

bool test_vm_page_try_to_free(void) {
    // Need a valid allocated page so it can be added to the free queue safely
    vm_page_t *p = vm_page_alloc(NULL, 0, 0);
    if (!p) return false;

    // Clean up its state for tests
    p->flags &= ~PG_BUSY;

    // Null check
    if (vm_page_try_to_free(NULL) != 0) return false;

    // Dirty page check
    p->flags |= PG_DIRTY;
    if (vm_page_try_to_free(p) != 0) return false;
    p->flags &= ~PG_DIRTY;

    // Busy page check
    p->flags |= PG_BUSY;
    if (vm_page_try_to_free(p) != 0) return false;
    p->flags &= ~PG_BUSY;

    // Wired page check
    p->wire_count = 1;
    if (vm_page_try_to_free(p) != 0) return false;
    p->wire_count = 0;

    // Valid page check (should succeed and free the page correctly to the VM subsystem)
    int result = vm_page_try_to_free(p);

    if (result != 1) return false;

    return true;
}

bool test_vm_page_flags(void) {
    vm_page_t p;
    memset(&p, 0, sizeof(p));
    p.magic_head = p.magic_tail = VM_PAGE_MAGIC;
    p.flags = 0;
    p.object = NULL;
    p.pv_list = NULL;
    p.ref_count = 0;

    vm_page_init();
    vm_page_free(&p);

    /*
     * vm_page_free() deliberately does NOT set PG_FREE.  vm_phys_free_page()
     * reads PG_FREE as "already on the buddy free list" and early-returns to
     * catch double frees, so setting it here would mean the page never
     * reached vm_phys_buddy_enqueue() and the frame was silently lost from
     * the allocator's view.  PG_FREE is buddy_enqueue's to set, once the page
     * really is on the list -- and this page was never in the buddy allocator
     * at all, so it does not get there.  What vm_page_free() guarantees is
     * that the page is off every queue, detached, and quiescent.  This test
     * asserted PG_FREE, which is the bug that convention was written to stop.
     */
    if (p.flags & PG_FREE)     return false;
    if (p.flags & PG_ACTIVE)   return false;
    if (p.flags & PG_INACTIVE) return false;
    if (p.ref_count  != 0)     return false;
    if (p.wire_count != 0)     return false;
    if (p.object     != NULL)  return false;

    return true;
}
