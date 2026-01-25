#ifndef _DRIVERS_VIDEO_VGA_H
#define _DRIVERS_VIDEO_VGA_H

#include <stdint.h>
#include <drivers/video/fb.h>

#define VGA_GFX_MEM_BASE    0xA0000
#define VGA_TEXT_MEM_BASE   0xB8000

/* Ports */
#define VGA_MISC_WRITE      0x3C2
#define VGA_MISC_READ       0x3CC
#define VGA_CRTC_INDEX      0x3D4
#define VGA_CRTC_DATA       0x3D5
#define VGA_SEQ_INDEX       0x3C4
#define VGA_SEQ_DATA        0x3C5
#define VGA_GC_INDEX        0x3CE
#define VGA_GC_DATA         0x3CF
#define VGA_AC_INDEX        0x3C0
#define VGA_AC_WRITE        0x3C0
#define VGA_AC_READ         0x3C1
#define VGA_INPUT_STAT1     0x3DA

/* Register Counts */
#define VGA_NUM_SEQ_REGS    5
#define VGA_NUM_CRTC_REGS   25
#define VGA_NUM_GC_REGS     9
#define VGA_NUM_AC_REGS     21

/* VGA Register State Structure */
typedef struct vga_regs {
    uint8_t misc;
    uint8_t seq[VGA_NUM_SEQ_REGS];
    uint8_t crtc[VGA_NUM_CRTC_REGS];
    uint8_t gc[VGA_NUM_GC_REGS];
    uint8_t ac[VGA_NUM_AC_REGS];
} vga_regs_t;

void vga_install(void);

#endif
