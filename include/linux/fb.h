/*
 * <linux/fb.h> — Linux-API compat header for substrate.
 *
 * Substrate's framebuffer ioctl surface (defined in <sys/fb.h>) is
 * binary-compatible with Linux's: same struct fb_var_screeninfo /
 * fb_fix_screeninfo layout, same FBIOGET_VSCREENINFO /
 * FBIOPUT_VSCREENINFO / FBIOGET_FSCREENINFO numbers.  Ported
 * software (xorg-server's kdrive/fbdev backend, fbset, ...)
 * expects to find them via this Linux-canonical path.
 */
#ifndef _LINUX_FB_H
#define _LINUX_FB_H

#include <sys/fb.h>
#include <linux/types.h>

/* Visual types — what's at a given pixel.  fbdev's modes only care
 * about MONO01 / PSEUDOCOLOR / TRUECOLOR / DIRECTCOLOR in practice. */
#ifndef FB_VISUAL_MONO01
#define FB_VISUAL_MONO01            0
#define FB_VISUAL_MONO10            1
#define FB_VISUAL_TRUECOLOR         2
#define FB_VISUAL_PSEUDOCOLOR       3
#define FB_VISUAL_DIRECTCOLOR       4
#define FB_VISUAL_STATIC_PSEUDOCOLOR 5
#endif

/* Memory organization — how plane data maps to bytes. */
#ifndef FB_TYPE_PACKED_PIXELS
#define FB_TYPE_PACKED_PIXELS       0
#define FB_TYPE_PLANES              1
#define FB_TYPE_INTERLEAVED_PLANES  2
#define FB_TYPE_TEXT                3
#define FB_TYPE_VGA_PLANES          4
#define FB_TYPE_FOURCC              5
#endif

/* mode-activation flags for fb_var_screeninfo.activate. */
#ifndef FB_ACTIVATE_NOW
#define FB_ACTIVATE_NOW       0
#define FB_ACTIVATE_NXTOPEN   1
#define FB_ACTIVATE_TEST      2
#define FB_ACTIVATE_MASK      15
#define FB_ACTIVATE_VBL       16
#define FB_CHANGE_CMAP_VBL    32
#define FB_ACTIVATE_ALL       64
#endif

/* Sync / vmode bits — mostly informational on substrate. */
#ifndef FB_SYNC_HOR_HIGH_ACT
#define FB_SYNC_HOR_HIGH_ACT   1
#define FB_SYNC_VERT_HIGH_ACT  2
#define FB_SYNC_EXT            4
#define FB_SYNC_COMP_HIGH_ACT  8
#endif

#ifndef FB_VMODE_NONINTERLACED
#define FB_VMODE_NONINTERLACED 0
#define FB_VMODE_INTERLACED    1
#define FB_VMODE_DOUBLE        2
#define FB_VMODE_MASK          255
#endif

/* fb_cmap — used by FBIOGETCMAP / FBIOPUTCMAP.  Substrate doesn't
 * currently implement these ioctls; the struct exists so callers
 * compile, the ioctl call will fail at runtime with ENOTTY which
 * fbdev handles gracefully (no per-pixel colormap support). */
struct fb_cmap {
    uint32_t   start;
    uint32_t   len;
    uint16_t  *red;
    uint16_t  *green;
    uint16_t  *blue;
    uint16_t  *transp;
};

/* Pan / panning ioctl — fbdev uses this for hardware-scroll
 * optimisations on the Linux fb driver.  Substrate doesn't have
 * a kernel-side pan ioctl yet; the call fails at runtime and
 * kdrive falls back to software scrolling. */
#ifndef FBIOPAN_DISPLAY
#define FBIOPAN_DISPLAY   0x4606
#endif
#ifndef FBIOPUTCMAP
#define FBIOPUTCMAP       0x4605
#define FBIOGETCMAP       0x4604
#endif

#endif /* _LINUX_FB_H */
