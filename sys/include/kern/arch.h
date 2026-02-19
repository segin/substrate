#ifndef _KERN_ARCH_H
#define _KERN_ARCH_H

#include <sys/proc.h>
#include <stdint.h>

void arch_switch_to(thread_t *prev, thread_t *next);
void arch_set_kernel_stack(uintptr_t stack);

#endif
