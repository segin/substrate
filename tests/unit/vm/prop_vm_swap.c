#include <stdbool.h>
#include <stddef.h>
#include <vm/vm_page.h"

/*
 * Property-based test: Swap Invariant
 * Prop: swap_in(swap_out(m)) == identity operation on swap pool.
 */

extern int swap_out(vm_page_t *m);
extern int swap_in(vm_page_t *m);

bool prop_swap_conservation(vm_page_t *m) {
    // Action
    if (swap_out(m) != 0) return true; // Ignore if swap full
    int block = (int)m->phys_addr;
    
    swap_in(m);
    
    // Invariant: The block should now be free.
    // We test this by seeing if we can allocate it again.
    vm_page_t m2;
    if (swap_out(&m2) != 0) return false;
    
    bool result = ((int)m2.phys_addr == block);
    swap_in(&m2);
    
    return result;
}

void run_swap_properties(void) {
    vm_page_t m;
    m.flags = PG_VALID;
    prop_swap_conservation(&m);
}
