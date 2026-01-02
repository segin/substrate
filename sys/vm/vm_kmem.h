#ifndef _VM_KMEM_H
#define _VM_KMEM_H

#include <stddef.h>

// General purpose variable-size kernel allocator (wrapper around zones)
void kmem_init(void);
void *kmalloc(size_t size);
void kfree(void *ptr, size_t size); // BSD kmem usually requires size for freeing

#endif
