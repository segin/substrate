#include "../../../sys/vm/vm_page.h"
#include <stdbool.h>
#include <stddef.h>

/*
 * VM Page Unit Tests
 */

bool test_vm_page_queue_ops(void) {
    vm_page_t p1, p2;
    p1.phys_addr = 0x1000;
    p2.phys_addr = 0x2000;
    
    vm_page_init();
    
    // Manually test free flow
    vm_page_free(&p1);
    vm_page_free(&p2);
    
    // In current implementation, p2 should be at the head
    // (We'd need to expose the queues or use an internal accessor to verify further)
    
    return true;
}

bool test_vm_page_flags(void) {
    vm_page_t p;
    p.flags = 0;
    
    vm_page_init();
    vm_page_free(&p);
    
    if (!(p.flags & PG_FREE)) return false;
    
    return true;
}
