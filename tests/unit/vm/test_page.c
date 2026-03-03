#include <vm/vm_page.h>
#include <stdbool.h>
#include <stddef.h>

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
    
    vm_page_init();
    
    // Manually test free flow
    vm_page_free(&p1);
    vm_page_free(&p2);
    
    return true;
}

bool test_vm_page_flags(void) {
    vm_page_t p;
    memset(&p, 0, sizeof(p));
    p.magic_head = p.magic_tail = VM_PAGE_MAGIC;
    p.flags = 0;
    
    vm_page_init();
    vm_page_free(&p);
    
    if (!(p.flags & PG_FREE)) return false;
    
    return true;
}
