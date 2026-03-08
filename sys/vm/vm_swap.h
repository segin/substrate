#ifndef _VM_SWAP_H
#define _VM_SWAP_H

#include <stdint.h>

void vm_swap_get_stats(uint64_t *total_pages, uint64_t *free_pages);
int vm_swapon(void *node);
int vm_swapoff(void);

#endif
