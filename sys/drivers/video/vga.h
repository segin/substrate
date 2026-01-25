#ifndef _DRIVERS_VIDEO_VGA_H
#define _DRIVERS_VIDEO_VGA_H

#include <stdint.h>
#include <drivers/video/fb.h>

/* Memory Map Helpers */
#define VGA_GFX_MEM_BASE    0xA0000
#define VGA_TEXT_MEM_BASE   0xB8000
#define VGA_CGA_MEM_BASE    0xB8000
#define VGA_HERC_MEM_BASE   0xB0000

/* Ports */
#define VGA_MISC_WRITE      0x3C2
#define VGA_MISC_READ       0x3CC
#define VGA_CRTC_INDEX_MONO 0x3B4
#define VGA_CRTC_DATA_MONO  0x3B5
#define VGA_CRTC_INDEX_COLOR 0x3D4
#define VGA_CRTC_DATA_COLOR  0x3D5
#define VGA_SEQ_INDEX       0x3C4
#define VGA_SEQ_DATA        0x3C5
#define VGA_GC_INDEX        0x3CE
#define VGA_GC_DATA         0x3CF
#define VGA_AC_INDEX        0x3C0
#define VGA_AC_WRITE        0x3C0
#define VGA_AC_READ         0x3C1
#define VGA_INPUT_STAT1_MONO 0x3BA
#define VGA_INPUT_STAT1_COLOR 0x3DA

/* Register Bit Definitions */

/* Miscellaneous Output Register */
#define VGA_MISC_IO_ADDR_SEL    (1 << 0) /* 0=3Bx, 1=3Dx */
#define VGA_MISC_RAM_EN         (1 << 1)
#define VGA_MISC_CLK_25MHZ      (0 << 2)
#define VGA_MISC_CLK_28MHZ      (1 << 2) /* Actually 28.322 MHz */
#define VGA_MISC_PAGE_ODD_EVEN  (1 << 5)
#define VGA_MISC_HSYNC_NEG      (1 << 6)
#define VGA_MISC_VSYNC_NEG      (1 << 7)

/* Sequencer Registers */
#define VGA_SEQ_RESET           0x00
#define VGA_SEQ_CLOCK_MODE      0x01
#define VGA_SEQ_MAP_MASK        0x02
#define VGA_SEQ_CHAR_MAP        0x03
#define VGA_SEQ_MEM_MODE        0x04

#define VGA_SEQ_MEM_MODE_CHAIN4 (1 << 3)
#define VGA_SEQ_MEM_MODE_ODD_EVEN (1 << 2)
#define VGA_SEQ_MEM_MODE_EXT_MEM (1 << 1)

/* CRTC Registers (Indices) */
#define VGA_CRTC_H_TOTAL        0x00
#define VGA_CRTC_H_DISP         0x01
#define VGA_CRTC_H_BLANK_START  0x02
#define VGA_CRTC_H_BLANK_END    0x03
#define VGA_CRTC_H_RETRACE_START 0x04
#define VGA_CRTC_H_RETRACE_END  0x05
#define VGA_CRTC_V_TOTAL        0x06
#define VGA_CRTC_OVERFLOW       0x07
#define VGA_CRTC_PRESET_ROW     0x08
#define VGA_CRTC_MAX_SCAN       0x09
#define VGA_CRTC_CURSOR_START   0x0A
#define VGA_CRTC_CURSOR_END     0x0B
#define VGA_CRTC_START_HI       0x0C
#define VGA_CRTC_START_LO       0x0D
#define VGA_CRTC_CURSOR_HI      0x0E
#define VGA_CRTC_CURSOR_LO      0x0F
#define VGA_CRTC_V_RETRACE_START 0x10
#define VGA_CRTC_V_RETRACE_END  0x11
#define VGA_CRTC_V_DISP_END     0x12
#define VGA_CRTC_OFFSET         0x13
#define VGA_CRTC_UNDERLINE      0x14
#define VGA_CRTC_V_BLANK_START  0x15
#define VGA_CRTC_V_BLANK_END    0x16
#define VGA_CRTC_MODE_CONTROL   0x17
#define VGA_CRTC_LINE_COMPARE   0x18

/* Graphics Controller Registers */
#define VGA_GC_SET_RESET        0x00
#define VGA_GC_ENABLE_SET_RESET 0x01
#define VGA_GC_COLOR_COMPARE    0x02
#define VGA_GC_DATA_ROTATE      0x03
#define VGA_GC_READ_MAP_SEL     0x04
#define VGA_GC_MODE             0x05
#define VGA_GC_MISC             0x06
#define VGA_GC_COLOR_DONT_CARE  0x07
#define VGA_GC_BIT_MASK         0x08

/* AC Registers */
#define VGA_AC_MODE_CONTROL     0x10
#define VGA_AC_OVERSCAN         0x11
#define VGA_AC_PLANE_ENABLE     0x12
#define VGA_AC_PANNING          0x13

/* Counts */
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

/* VGA Mode Definition */
typedef struct {
    int width;
    int height;
    int bpp;
    int mode_id;
    const vga_regs_t *regs; 
    void (*init_func)(void);
    void (*putpixel)(int, int, uint32_t);
    uint32_t mem_base;
    uint32_t pitch;
} vga_mode_def_t;

void vga_install(void);

#endif
