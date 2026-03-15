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

/* Modifier state (read-only for external consumers) */
extern int kbd_shift;
extern int kbd_ctrl;
extern int kbd_alt;
extern int kbd_lshift, kbd_rshift;
extern int kbd_lctrl, kbd_rctrl;
extern int kbd_lalt, kbd_ralt;

#endif
