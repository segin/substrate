#ifndef _KEYBOARD_H
#define _KEYBOARD_H

#include <stdint.h>
#include "../../arch/i386/idt.h"

void keyboard_init(void);
void keyboard_handler(registers_t *regs);
char keyboard_getc(void);

#endif
