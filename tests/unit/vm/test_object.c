#include <string.h>
#include <vm/vm_object.h>
#include <stdbool.h>
#include <stddef.h>

/*
 * VM Object Unit Tests
 */

bool test_vm_object_lifecycle(void) {
    vm_object_t *obj = vm_object_allocate(VM_OBJ_TYPE_DEFAULT, 4096);
    if (!obj) return false;
    
    if (obj->ref_count != 1) return false;
    
    vm_object_reference(obj);
    if (obj->ref_count != 2) return false;
    
    vm_object_deallocate(obj);
    if (obj->ref_count != 1) return false;
    
    vm_object_deallocate(obj); // Should be destroyed (type becomes DEAD)
    if (obj->type != VM_OBJ_TYPE_DEAD) return false;
    
    return true;
}

bool test_vm_object_page_mgmt(void) {
    vm_object_t *obj = vm_object_allocate(VM_OBJ_TYPE_DEFAULT, 8192);
    if (!obj) return false;
    
    vm_page_t p1, p2; memset(&p1, 0, sizeof(p1)); memset(&p2, 0, sizeof(p2)); p1.magic_head = VM_PAGE_MAGIC; p1.magic_tail = VM_PAGE_MAGIC; p2.magic_head = VM_PAGE_MAGIC; p2.magic_tail = VM_PAGE_MAGIC;
    p1.pindex = 0;
    p2.pindex = 1;
    
    vm_object_add_page(obj, &p1);
    vm_object_add_page(obj, &p2);
    
    if (obj->page_count != 2) return false;
    
    vm_page_t *found = vm_object_lookup_page(obj, 1);
    if (found != &p2) return false;
    
    vm_object_remove_page(obj, &p1);
    if (obj->page_count != 1) return false;
    
    vm_object_deallocate(obj);
    return true;
}
