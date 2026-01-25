/*
 * fb_console.h - Framebuffer Console Interface
 *
 * Console rendering on top of the framebuffer driver.
 */
#ifndef _FB_CONSOLE_H
#define _FB_CONSOLE_H

#include <stddef.h>
#include <stdint.h>

/* Initialize framebuffer console and register with console subsystem */
void fb_console_init(void);

/* Write string to framebuffer console */
void fb_write(const char *s, size_t n);

/* Put single character with colors */
void fb_putc(char c, uint32_t fg, uint32_t bg);

#endif /* _FB_CONSOLE_H */
