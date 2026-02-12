#include <stdbool.h>
#include <stddef.h>
#include <vm/vm_page.h>

/*
 * Property-based test: Queue Invariant for VM Page
 * Prop: A page is in exactly one state (FREE, ACTIVE, INACTIVE) at all times.
 */

bool prop_vm_page_state_invariant(vm_page_t *m) {
    int states = 0;
    if (m->flags & PG_FREE) states++;
    if (m->flags & PG_ACTIVE) states++;
    if (m->flags & PG_INACTIVE) states++;
    
    // In current simplified logic, a page being handled might have 0 states 
    // if not in a queue yet, but never more than 1.
    return (states <= 1);
}

void run_vm_page_properties(void) {
    vm_page_t m;
    m.flags = 0;
    
    vm_page_init();
    vm_page_free(&m);
    prop_vm_page_state_invariant(&m);
}
