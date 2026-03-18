/*
 * fb_console.h - Framebuffer Console Interface
 *
 * Console rendering on top of the framebuffer driver.
 */
#ifndef _FB_CONSOLE_H
#define _FB_CONSOLE_H

#include <stddef.h>
#include <stdint.h>

/* Character rendering attributes (bitmask) */
#define FB_ATTR_NONE          0x00
#define FB_ATTR_BOLD          0x01
#define FB_ATTR_ITALIC        0x02
#define FB_ATTR_UNDERLINE     0x04
#define FB_ATTR_STRIKETHROUGH 0x08
#define FB_ATTR_REVERSE       0x10

/* Initialize framebuffer console and register with console subsystem */
void fb_console_init(void);

/* Write string to framebuffer console */
void fb_write(const char *s, size_t n);

/* Put single character with colors */
void fb_putc(char c, uint32_t fg, uint32_t bg);

/* Put single character with colors and rendering attributes */
void fb_putc_attr(char c, uint32_t fg, uint32_t bg, uint8_t attr);

#endif /* _FB_CONSOLE_H */
