#ifndef _DRIVERS_VIDEO_HERCULES_H
#define _DRIVERS_VIDEO_HERCULES_H

#include <drivers/video/fb.h>

#define HERC_MEM_BASE       0xB0000
#define HERC_INDEX_PORT     0x3B4
#define HERC_DATA_PORT      0x3B5
#define HERC_MODE_CONTROL   0x3B8
#define HERC_CONFIG_SWITCH  0x3BF
#define HERC_STATUS_PORT    0x3BA

/* Mode Control Bits */
#define HERC_MODE_GRAPHICS  0x02
#define HERC_MODE_VIDEO_EN  0x08
#define HERC_MODE_BLINK     0x20
#define HERC_MODE_PAGE1     0x80

int herc_init(fb_info_t *fb);

#endif
