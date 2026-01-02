#include <stdbool.h>
#include <stdint.h>
#include "../../../sys/sys/lock.h"

/*
 * Property-based test: Spinlock Invariant
 * Prop: acquire(L) -> is_held(L) is true && L.locked == 1.
 */

extern uint32_t lapic_get_id(void);

bool prop_spinlock_mutual_exclusion(void) {
    spinlock_t lock;
    spinlock_init(&lock, "prop-test");
    
    spinlock_acquire(&lock);
    
    // Invariant: Held by us
    bool result = (lock.locked == 1 && lock.cpu_id == lapic_get_id() && spinlock_is_held(&lock));
    
    spinlock_release(&lock);
    
    // Invariant: Not held after release
    result &= (lock.locked == 0 && !spinlock_is_held(&lock));
    
    return result;
}

void run_locking_properties(void) {
    prop_spinlock_mutual_exclusion();
}
