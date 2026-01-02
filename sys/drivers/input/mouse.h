#ifndef _MOUSE_H
#define _MOUSE_H

#include <stdint.h>
#include "../../arch/i386/idt.h"

void mouse_init(void);
void mouse_handler(registers_t *regs);

#endif
