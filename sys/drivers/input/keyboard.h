#ifndef _KEYBOARD_H
#define _KEYBOARD_H

#include <stdint.h>
#include <arch/i386/idt.h>

struct keymap;

void keyboard_init(void);
void keyboard_handler(registers_t *regs);
char keyboard_getc(void);
void kbd_push(char c);
void keyboard_set_keymap(const struct keymap *km);
void keyboard_set_typematic(uint8_t delay, uint8_t rate);

/* Process a hardware-independent keycode (shared with USB HID) */
void process_keycode(uint16_t keycode, int pressed);

/* Modifier state (read-only for external consumers) */
extern int kbd_shift;
extern int kbd_ctrl;
extern int kbd_alt;
extern int kbd_lshift, kbd_rshift;
extern int kbd_lctrl, kbd_rctrl;
extern int kbd_lalt, kbd_ralt;

/* LED state management for VT switch persistence */
uint8_t keyboard_get_led_state(void);
void keyboard_set_led_state(uint8_t state);

#endif
