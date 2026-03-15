#ifndef _SYS_IRQ_H
#define _SYS_IRQ_H

#include <stddef.h>
#include <stdint.h>

#define IRQF_SHARED 0x0001U

#define IRQ_VECTOR_FIRST 0x50
#define IRQ_VECTOR_LAST  0xBF

typedef int (*irq_handler_t)(unsigned int irq, void *dev_id, void *frame);

int request_irq(unsigned int irq, irq_handler_t handler, unsigned long flags,
                const char *name, void *dev_id);
void free_irq(unsigned int irq, void *dev_id);
int irq_dispatch(unsigned int irq, void *frame);
int irq_alloc_vector(void);
void irq_free_vector(int vector);

#endif
