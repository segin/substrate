#ifndef _VM_KMEM_H
#define _VM_KMEM_H

#include <stddef.h>
#include <stdint.h>

/* General purpose variable-size kernel allocator (wrapper around UMA zones) */
void kmem_init(void);
void *kmalloc(size_t size);
void *kzalloc(size_t size);  /* Allocate and zero memory */
void kfree(void *ptr, size_t size); /* BSD kmem usually requires size for freeing */

/* Statistics */
void kmem_get_stats(uint64_t *allocs, uint64_t *frees, uint64_t *bytes);

#endif
