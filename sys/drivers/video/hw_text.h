#ifndef _DRIVERS_VIDEO_HW_TEXT_H
#define _DRIVERS_VIDEO_HW_TEXT_H

#include <stddef.h>
#include <stdint.h>

void hw_text_init(void);
void hw_text_late_init(void);
void hw_text_putc(char c);
void hw_text_write(const char *data, size_t len);
void hw_text_clear_screen(void);
void hw_text_set_color(uint8_t fg, uint8_t bg);
int hw_text_set_tab_width(unsigned int width);
unsigned int hw_text_get_tab_width(void);
void hw_text_refresh_statusline(void);
void hw_text_tick_1hz(void);
void hw_text_redraw_active(void);

#endif
