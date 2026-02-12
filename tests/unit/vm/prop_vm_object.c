#include <stdbool.h>
#include <stddef.h>
#include <vm/vm_object.h"

/*
 * Property-based test: Consistency Invariant for VM Object
 * Prop: page_count == list_length(pages).
 */

bool prop_vm_object_consistency(vm_object_t *obj) {
    uint32_t count = 0;
    vm_page_t *p;
    
    for (p = obj->pages; p != NULL; p = p->next) {
        count++;
    }
    
    return (count == obj->page_count);
}

void run_vm_object_properties(void) {
    vm_object_t *obj = vm_object_allocate(VM_OBJ_TYPE_DEFAULT, 16384);
    
    vm_page_t p1, p2;
    vm_object_add_page(obj, &p1);
    vm_object_add_page(obj, &p2);
    
    prop_vm_object_consistency(obj);
    
    vm_object_deallocate(obj);
}
