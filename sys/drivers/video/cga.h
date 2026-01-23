#ifndef _DRIVERS_VIDEO_CGA_H
#define _DRIVERS_VIDEO_CGA_H

#include <stdint.h>

#define CGA_MEM_BASE    0xB8000
#define CGA_CTRL_PORT   0x3D4
#define CGA_DATA_PORT   0x3D5

/* CGA Registers */
#define CGA_REG_CURSOR_HIGH 0x0E
#define CGA_REG_CURSOR_LOW  0x0F

/* CGA Colors */
#define CGA_COLOR_BLACK         0
#define CGA_COLOR_BLUE          1
#define CGA_COLOR_GREEN         2
#define CGA_COLOR_CYAN          3
#define CGA_COLOR_RED           4
#define CGA_COLOR_MAGENTA       5
#define CGA_COLOR_BROWN         6
#define CGA_COLOR_LIGHT_GREY    7
#define CGA_COLOR_DARK_GREY     8
#define CGA_COLOR_LIGHT_BLUE    9
#define CGA_COLOR_LIGHT_GREEN   10
#define CGA_COLOR_LIGHT_CYAN    11
#define CGA_COLOR_LIGHT_RED     12
#define CGA_COLOR_LIGHT_MAGENTA 13
#define CGA_COLOR_LIGHT_BROWN   14 /* Yellow */
#define CGA_COLOR_WHITE         15

void cga_init(void);
void cga_putc(char c, uint8_t color);

#endif
