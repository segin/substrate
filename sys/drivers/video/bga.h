#ifndef _BGA_H
#define _BGA_H

#include <drivers/video/fb.h>

int bga_is_available(void);
int bga_init(fb_info_t *fb_out);

#ifdef HOST_TEST
/* The banked framebuffer window, a buffer the host test provides. */
extern volatile uint8_t *bga_test_bank_window;
#endif

#endif
