#ifndef _MOUSE_H
#define _MOUSE_H

#include <stdint.h>
#include "../../arch/i386/idt.h"

typedef struct {
    int32_t dx;
    int32_t dy;
    uint8_t buttons;
} mouse_event_t;

void mouse_init(void);
void mouse_handler(registers_t *regs);
void mouse_get_state(int32_t *x, int32_t *y, uint8_t *buttons);
int  mouse_get_event(mouse_event_t *ev);

#endif
