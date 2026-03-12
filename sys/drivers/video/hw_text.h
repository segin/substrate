#ifndef _DRIVERS_VIDEO_HW_TEXT_H
#define _DRIVERS_VIDEO_HW_TEXT_H

#include <stdint.h>

void hw_text_init(void);
void hw_text_putc(char c);
void hw_text_clear_screen(void);
void hw_text_set_color(uint8_t fg, uint8_t bg);
void hw_text_refresh_statusline(void);
void hw_text_tick_1hz(void);

#endif
