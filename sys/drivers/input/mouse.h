#ifndef _MOUSE_H
#define _MOUSE_H

#include <stdint.h>
#include <arch/i386/idt.h>

/*
 * mouse_event_t.buttons bit layout (left to right, LSB first):
 *   bit 0 — left
 *   bit 1 — right
 *   bit 2 — middle
 *   bit 3 — button 4 (Explorer "back")
 *   bit 4 — button 5 (Explorer "forward")
 *
 * wheel is signed scroll delta (positive = up).  Zero on plain
 * 3-byte mice that have no wheel.
 */
typedef struct {
    int32_t dx;
    int32_t dy;
    int32_t wheel;
    uint8_t buttons;
} mouse_event_t;

void mouse_init(void);
void mouse_handler(registers_t *regs);
void mouse_get_state(int32_t *x, int32_t *y, uint8_t *buttons);
int  mouse_get_event(mouse_event_t *ev);

#endif
