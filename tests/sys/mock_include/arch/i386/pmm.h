#ifndef _PMM_H
#define _PMM_H
#include <stdint.h>
#include <stddef.h>

void *pmm_alloc_block(void);
void *pmm_alloc_contiguous(size_t count);
void pmm_free_block(void *p);
void pmm_free_contiguous(void *p, size_t count);

#endif
