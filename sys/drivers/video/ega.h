#ifndef _DRIVERS_VIDEO_EGA_H
#define _DRIVERS_VIDEO_EGA_H

#include <stdint.h>

#define EGA_MEM_BASE    0xA0000
#define EGA_CTRL_PORT   0x3C4
#define EGA_DATA_PORT   0x3C5 /* Presumably, if following CGA pattern, though EGA differs */

/* EGA Registers */
/* EGA Registers */
#define EGA_MISC_OUT_W  0x3C2
#define EGA_MISC_OUT_R  0x3CC
#define EGA_AC_INDEX    0x3C0
#define EGA_AC_WRITE    0x3C0
#define EGA_AC_READ     0x3C1
#define EGA_SEQ_INDEX   0x3C4
#define EGA_SEQ_DATA    0x3C5
#define EGA_DAC_MASK    0x3C6
#define EGA_DAC_READ    0x3C7
#define EGA_DAC_WRITE   0x3C8
#define EGA_DAC_DATA    0x3C9
#define EGA_GC_INDEX    0x3CE
#define EGA_GC_DATA     0x3CF
#define EGA_CRTC_INDEX  0x3D4 /* Color */
#define EGA_CRTC_DATA   0x3D5
#define EGA_INPUT_STAT1 0x3DA

/* Sequencer Registers */
#define EGA_SEQ_RESET          0x00
#define EGA_SEQ_CLOCK_MODE     0x01
#define EGA_SEQ_MAP_MASK       0x02
#define EGA_SEQ_CHAR_MAP_SEL   0x03
#define EGA_SEQ_MEMORY_MODE    0x04

/* Graphics Controller Registers */
#define EGA_GC_SET_RESET       0x00
#define EGA_GC_ENABLE_SET_RESET 0x01
#define EGA_GC_COLOR_COMPARE   0x02
#define EGA_GC_DATA_ROTATE     0x03
#define EGA_GC_READ_MAP_SEL    0x04
#define EGA_GC_MODE            0x05
#define EGA_GC_MISC            0x06
#define EGA_GC_COLOR_DONT_CARE 0x07
#define EGA_GC_BIT_MASK        0x08

/* Standard 640x350x16 (Mode 0x10) register values (simplified) */
// Actual init will typically push a table of values

void ega_init(void);

#endif
