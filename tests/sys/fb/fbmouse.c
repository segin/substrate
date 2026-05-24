/*
 * fbmouse — minimal framebuffer + mouse smoke test for substrate.
 *
 * - mmap /dev/fb0 (1024x768x32 BGRA assumed; we read it via
 *   FBIOGET_VSCREENINFO / FBIOGET_FSCREENINFO so a different mode also works)
 * - paint a recognizable dark-blue background plus a center crosshair so we
 *   can see we own the screen even before mouse activity
 * - read input_event records from /dev/input/event0 and walk a cursor
 *   around in response to EV_REL / EV_ABS / EV_KEY
 *
 * cursor: 11x16 white sprite (a thick "+" shape) with a 1-pixel black
 * outline so it stays visible on bright backgrounds.  Erase by saving
 * the pixels under the cursor before drawing, and restoring them
 * before redrawing at the new spot.
 *
 * Press the keyboard ESC (KEY_ESC=1) to exit cleanly.
 *
 * Build: i386-unknown-substrate-gcc -O2 -o fbmouse fbmouse.c
 */

#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* Linux/substrate fb ioctls + structs.  We only need a few fields. */
#define FBIOGET_VSCREENINFO 0x4600
#define FBIOGET_FSCREENINFO 0x4602

struct fb_bitfield { uint32_t offset, length, msb_right; };
struct fb_var_screeninfo {
    uint32_t xres, yres, xres_virtual, yres_virtual, xoffset, yoffset;
    uint32_t bits_per_pixel, grayscale;
    struct fb_bitfield red, green, blue, transp;
    uint32_t nonstd, activate, height, width, accel_flags;
    uint32_t pixclock, left_margin, right_margin, upper_margin, lower_margin;
    uint32_t hsync_len, vsync_len, sync, vmode;
    uint32_t reserved[6];
};
struct fb_fix_screeninfo {
    char id[16];
    unsigned long smem_start;
    uint32_t smem_len, type, type_aux, visual;
    uint16_t xpanstep, ypanstep, ywrapstep;
    uint32_t line_length;
    unsigned long mmio_start;
    uint32_t mmio_len, accel;
    uint16_t reserved[3];
};

/* substrate input event — must match sys/include/sys/input.h. */
struct input_event {
    long sec, usec;
    uint16_t type;
    uint16_t code;
    int32_t value;
};
#define EV_SYN 0x00
#define EV_KEY 0x01
#define EV_REL 0x02
#define EV_ABS 0x03
#define REL_X 0x00
#define REL_Y 0x01
#define ABS_X 0x00
#define ABS_Y 0x01
#define KEY_ESC 1
#define BTN_LEFT 0x110

static uint32_t *fb;
static int W, H, pitch_px;
static int cx, cy;          /* current cursor position */

#define CUR_W 11
#define CUR_H 16
static uint32_t cur_backup[CUR_W * CUR_H];

/* "+" sprite, 1 = white, 2 = black-outline, 0 = transparent. */
static const char cursor_glyph[CUR_H][CUR_W] = {
    {0,0,0,0,2,2,2,0,0,0,0},
    {0,0,0,0,2,1,2,0,0,0,0},
    {0,0,0,0,2,1,2,0,0,0,0},
    {0,0,0,0,2,1,2,0,0,0,0},
    {2,2,2,2,2,1,2,2,2,2,2},
    {2,1,1,1,1,1,1,1,1,1,2},
    {2,2,2,2,2,1,2,2,2,2,2},
    {0,0,0,0,2,1,2,0,0,0,0},
    {0,0,0,0,2,1,2,0,0,0,0},
    {0,0,0,0,2,1,2,0,0,0,0},
    {0,0,0,0,2,2,2,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0,0},
};

static void put_px(int x, int y, uint32_t color) {
    if (x < 0 || y < 0 || x >= W || y >= H) return;
    fb[y * pitch_px + x] = color;
}

static uint32_t get_px(int x, int y) {
    if (x < 0 || y < 0 || x >= W || y >= H) return 0;
    return fb[y * pitch_px + x];
}

static void fill_rect(int x, int y, int w, int h, uint32_t color) {
    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++)
            put_px(x + i, y + j, color);
}

static void crosshair(int x, int y, uint32_t color, int span) {
    for (int i = -span; i <= span; i++) {
        put_px(x + i, y, color);
        put_px(x, y + i, color);
    }
}

static void cursor_save(int x, int y) {
    for (int j = 0; j < CUR_H; j++)
        for (int i = 0; i < CUR_W; i++)
            cur_backup[j * CUR_W + i] = get_px(x + i, y + j);
}

static void cursor_restore(int x, int y) {
    for (int j = 0; j < CUR_H; j++)
        for (int i = 0; i < CUR_W; i++)
            put_px(x + i, y + j, cur_backup[j * CUR_W + i]);
}

static void cursor_draw(int x, int y) {
    for (int j = 0; j < CUR_H; j++) {
        for (int i = 0; i < CUR_W; i++) {
            char p = cursor_glyph[j][i];
            if (p == 1)      put_px(x + i, y + j, 0x00FFFFFFu);
            else if (p == 2) put_px(x + i, y + j, 0x00000000u);
        }
    }
}

#define KDSETMODE   0x4B3A
#define KDSKBMODE   0x4B45
#define KD_GRAPHICS 1
#define KD_TEXT     0
#define K_RAW       0
#define K_XLATE     1

int main(void) {
    /* Take the VT into KD_GRAPHICS so the kernel fb_console stops
     * blitting text on top of us, and put the keyboard into K_RAW so
     * keystrokes don't echo onto the framebuffer behind us. */
    int ttyfd = open("/dev/tty0", O_RDWR);
    if (ttyfd >= 0) {
        ioctl(ttyfd, KDSETMODE, KD_GRAPHICS);
        ioctl(ttyfd, KDSKBMODE, K_RAW);
    }

    int fbfd = open("/dev/fb0", O_RDWR);
    if (fbfd < 0) { perror("open /dev/fb0"); return 1; }

    struct fb_var_screeninfo vi;
    struct fb_fix_screeninfo fi;
    if (ioctl(fbfd, FBIOGET_VSCREENINFO, &vi) < 0) { perror("FBIOGET_VSCREENINFO"); return 2; }
    if (ioctl(fbfd, FBIOGET_FSCREENINFO, &fi) < 0) { perror("FBIOGET_FSCREENINFO"); return 3; }

    W = vi.xres;
    H = vi.yres;
    pitch_px = fi.line_length / (vi.bits_per_pixel / 8);

    size_t sz = (size_t)fi.line_length * (size_t)H;
    fb = mmap(0, sz, PROT_READ | PROT_WRITE, MAP_SHARED, fbfd, 0);
    if (fb == (void *)-1) { perror("mmap"); return 4; }

    /* Dark blue background so the cursor is obvious. */
    fill_rect(0, 0, W, H, 0x00000033u);

    /* Center fiducial + corner markers — proof we own all corners. */
    crosshair(W/2, H/2, 0x00FF0000u, 25);          /* red center */
    fill_rect(0,     0,     32, 32, 0x00FFFF00u);  /* yellow TL */
    fill_rect(W-32,  0,     32, 32, 0x00FF00FFu);  /* magenta TR */
    fill_rect(0,     H-32,  32, 32, 0x0000FFFFu);  /* cyan BL */
    fill_rect(W-32,  H-32,  32, 32, 0x0000FF00u);  /* green BR */

    /* Initial cursor at center. */
    cx = W / 2 - CUR_W / 2;
    cy = H / 2 - CUR_H / 2;
    cursor_save(cx, cy);
    cursor_draw(cx, cy);

    int evfd = open("/dev/input/event0", O_RDONLY);
    if (evfd < 0) {
        /* No input — sit and let a screendump prove the framebuffer
         * write path itself is working. */
        for (;;) pause();
    }

    /* Trace what comes off the wire so we can diagnose sign issues. */
    int logfd = open("/tmp/fbmouse.log", O_WRONLY | O_CREAT | O_TRUNC, 0644);

    struct input_event ev[16];
    int dx_acc = 0, dy_acc = 0;
    for (;;) {
        ssize_t n = read(evfd, ev, sizeof(ev));
        if (n <= 0) continue;
        int nevs = (int)(n / (ssize_t)sizeof(struct input_event));
        for (int i = 0; i < nevs; i++) {
            uint16_t t = ev[i].type, c = ev[i].code;
            int32_t v = ev[i].value;
            /* Log only motion / button / sync to keep the file small,
             * but ALWAYS fsync each line so a kill -9 still preserves
             * the trace. */
            if (logfd >= 0 && (t == EV_REL || t == EV_KEY || t == EV_SYN)) {
                char buf[128];
                int len = snprintf(buf, sizeof(buf),
                    "ev t=%u c=%u v=%d (raw=0x%08x) acc=(%d,%d) cur=(%d,%d)\n",
                    (unsigned)t, (unsigned)c, (int)v, (unsigned)v,
                    dx_acc, dy_acc, cx, cy);
                if (len > 0) {
                    write(logfd, buf, (size_t)len);
                    fsync(logfd);
                }
            }
            if (t == EV_REL) {
                if (c == REL_X) dx_acc += v;
                else if (c == REL_Y) dy_acc += v;
            } else if (t == EV_ABS) {
                /* USB tablet sends 0..32767 absolute coords.  Map to screen. */
                if (c == ABS_X) cx = (int)((int64_t)v * (W - CUR_W) / 32767);
                else if (c == ABS_Y) cy = (int)((int64_t)v * (H - CUR_H) / 32767);
            } else if (t == EV_KEY) {
                if (c == KEY_ESC && v) {
                    cursor_restore(cx, cy);
                    return 0;
                }
                if (c == BTN_LEFT && v) {
                    /* "click": leave a small dot at the cursor centre. */
                    put_px(cx + CUR_W/2, cy + CUR_H/2, 0x00FF00FFu);
                }
            } else if (t == EV_SYN) {
                /* End of event packet — apply accumulated motion + repaint. */
                cursor_restore(cx, cy);
                if (dx_acc || dy_acc) {
                    cx += dx_acc; cy += dy_acc;
                    dx_acc = dy_acc = 0;
                }
                if (cx < 0) cx = 0; if (cx > W - CUR_W) cx = W - CUR_W;
                if (cy < 0) cy = 0; if (cy > H - CUR_H) cy = H - CUR_H;
                cursor_save(cx, cy);
                cursor_draw(cx, cy);
            }
        }
    }
}
