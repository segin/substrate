#ifndef _MOUSE_H
#define _MOUSE_H

#include <stdint.h>
#include "../../arch/i386/idt.h"

void mouse_init(void);
void mouse_handler(registers_t *regs);
void mouse_get_state(int32_t *x, int32_t *y, uint8_t *buttons);

#endif
