# VM Object Specification

## Overview
A `vm_object` is a machine-independent abstraction representing a source of data that can be mapped into a virtual address space. It manages a set of physical pages (`vm_page_t`) and provides a unified interface for different backing stores (anonymous memory, files, or devices).

## Design
- **Types:**
    - `VM_OBJ_TYPE_DEFAULT`: Anonymous zero-filled memory.
    - `VM_OBJ_TYPE_VNODE`: Backed by a file.
    - `VM_OBJ_TYPE_DEVICE`: Memory-mapped I/O.
- **Reference Counting:** Objects are shared via `vm_object_reference()` and destroyed when the count reaches zero.
- **Page Residency:** Objects maintain a list of their current physical pages.

## API
### `vm_object_t *vm_object_allocate(vm_object_type_t type, size_t size)`
Creates a new VM object of the specified type and size.

### `void vm_object_reference(vm_object_t *object)`
Increments the reference count of the object.

### `void vm_object_deallocate(vm_object_t *object)`
Decrements the reference count and destroys the object (and its pages) if it reaches zero.

### `vm_page_t *vm_object_lookup_page(vm_object_t *object, uint64_t pindex)`
Finds a resident physical page at the given index within the object.

## Constraints
- Not thread-safe in the current prototype (requires locking).
- Bootstrap pool is limited to 32 objects.
