#include "vm_object.h"
#include <stddef.h>

// Static pool for bootstrap objects (until kmalloc is ready)
#define MAX_BOOTSTRAP_OBJECTS 32
static vm_object_t bootstrap_objects[MAX_BOOTSTRAP_OBJECTS];
static int next_bootstrap_object = 0;

static vm_object_t *alloc_object(void) {
    if (next_bootstrap_object < MAX_BOOTSTRAP_OBJECTS) {
        return &bootstrap_objects[next_bootstrap_object++];
    }
    return NULL;
}

void vm_object_init(void) {
    next_bootstrap_object = 0;
}

vm_object_t *vm_object_allocate(vm_object_type_t type, size_t size) {
    vm_object_t *obj = alloc_object();
    if (!obj) return NULL;

    obj->type = type;
    obj->size = size;
    obj->ref_count = 1;
    obj->pages = NULL;
    obj->page_count = 0;
    obj->handle = NULL;

    return obj;
}

void vm_object_reference(vm_object_t *object) {
    if (object) {
        object->ref_count++;
    }
}

void vm_object_deallocate(vm_object_t *object) {
    if (!object) return;

    object->ref_count--;
    if (object->ref_count == 0) {
        // Free all pages
        vm_page_t *p = object->pages;
        while (p) {
            vm_page_t *next = p->next;
            vm_page_free(p);
            p = next;
        }
        // TODO: kfree(object) if dynamic
        object->type = VM_OBJ_TYPE_DEAD;
    }
}

void vm_object_add_page(vm_object_t *object, vm_page_t *page) {
    page->object = object;
    
    // Add to head of object's page list
    page->next = object->pages;
    if (object->pages) {
        object->pages->prev = page;
    }
    object->pages = page;
    page->prev = NULL;
    
    object->page_count++;
}

void vm_object_remove_page(vm_object_t *object, vm_page_t *page) {
    if (page->prev) {
        page->prev->next = page->next;
    } else {
        object->pages = page->next;
    }
    if (page->next) {
        page->next->prev = page->prev;
    }
    page->object = NULL;
    object->page_count--;
}

vm_page_t *vm_object_lookup_page(vm_object_t *object, uint64_t pindex) {
    vm_page_t *p;
    for (p = object->pages; p != NULL; p = p->next) {
        if (p->pindex == pindex) return p;
    }
    return NULL;
}
