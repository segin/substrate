#ifndef _VM_KMEM_H
#define _VM_KMEM_H
#include <stddef.h>
void *kmalloc(size_t size);
void kfree(void *ptr, size_t size);
#endif
