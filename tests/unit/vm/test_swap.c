#include <stdbool.h>
#include <stddef.h>
#include <vm/vm_page.h"

/*
 * Swap Subsystem Unit Tests
 */

extern int swap_out(vm_page_t *m);
extern int swap_in(vm_page_t *m);

bool test_swap_lifecycle(void) {
    vm_page_t m;
    m.flags = PG_VALID;
    m.phys_addr = 0x1000;
    
    // 1. Swap Out
    if (swap_out(&m) != 0) return false;
    if (!(m.flags & PG_SWAPPED)) return false;
    if (m.flags & PG_VALID) return false;
    
    // 2. Swap In
    if (swap_in(&m) != 0) return false;
    if (m.flags & PG_SWAPPED) return false;
    if (!(m.flags & PG_VALID)) return false;
    
    return true;
}

bool test_swap_full(void) {
    // Fill up the 1024 pages
    vm_page_t pages[1025];
    for (int i = 0; i < 1024; i++) {
        if (swap_out(&pages[i]) != 0) return false;
    }
    
    // 1025th should fail
    if (swap_out(&pages[1024]) == 0) return false;
    
    return true;
}
