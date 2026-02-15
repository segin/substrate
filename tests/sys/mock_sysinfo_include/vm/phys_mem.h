#ifndef _VM_PHYS_MEM_H
#define _VM_PHYS_MEM_H

#include <stddef.h>

unsigned long vm_phys_get_free(void);
unsigned long vm_phys_get_used(void);

#endif
