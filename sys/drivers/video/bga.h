#ifndef _BGA_H
#define _BGA_H

#include <drivers/video/fb.h>

int bga_is_available(void);
int bga_init(fb_info_t *fb_out);

#endif
