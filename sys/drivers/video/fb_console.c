/*
 * fb_console.c - Framebuffer Console Rendering
 *
 * Handles text rendering, cursor management, and scrolling
 * on top of the framebuffer driver.
 */

#include <string.h>
#include <stdint.h>
#include <kern/console.h>
#include <kern/sched.h>
#include <kern/time.h>
#include <stdio.h>
#include <sys/lock.h>
#include <sys/tty.h>
#include <sys/vt.h>
#include <sys/vtio.h>
#include <sys/copy.h>
#include <sys/errno.h>
#include <arch/i386/intr.h>
#include "fb.h"
#include "fb_console.h"
#include "fb_ops.h"
#include "fb_ops.h"
#include "font.h"

/* ==================== Constants ==================== */

#define FB_FONT_WIDTH   8
#define FB_FONT_HEIGHT  16

#define FB_COLOR_WHITE       0x00FFFFFF
#define FB_COLOR_BLACK       0x00000000

/* ==================== State ==================== */

static int cursor_x = 0;
static int cursor_y = 0;
static int view_y_offset = 0; /* Viewport Y offset for hardware scrolling */
static int cursor_drawn = 0;
static int cursor_draw_x = 0;
static int cursor_draw_y = 0;
static uint8_t cursor_saved[64];
static int cursor_saved_valid = 0;
static uint8_t cursor_blink_phase = 1;
static uint32_t cursor_blink_ticks = 0;
static int dirty_valid = 0;
static int dirty_x1 = 0;
static int dirty_y1 = 0;
static int dirty_x2 = 0;
static int dirty_y2 = 0;
static unsigned int dirty_ticks = 0;
static int fb_console_tty_count = 0;
static vt_state_t *fb_current_vt_ctx = NULL;
static spinlock_t fb_console_lock = SPINLOCK_INIT("fb_console");

/*
 * fb_console_lock is also touched from interrupt context (e.g. tty
 * input echo arriving from the keyboard IRQ flowing through
 * tty_flip_buffer_push -> echo path).  Process-side acquires must
 * disable local interrupts so an IRQ can't recurse into the lock on
 * the same CPU.  Mirror the irq.c pattern: save eflags on entry,
 * restore on exit.
 */
#define FB_LOCK()   uint32_t _fb_flags = intr_disable(); spinlock_acquire(&fb_console_lock)
#define FB_UNLOCK() do { spinlock_release(&fb_console_lock); intr_restore(_fb_flags); } while (0)

#define FB_CONSOLE_FLUSH_HZ 60U

static int fb_console_cursor_should_be_visible(void);
static void fb_console_mark_dirty(int x, int y, int w, int h);
static void fb_console_hide_cursor(void);
static void fb_console_show_cursor(void);
static const struct ansi_callbacks fb_ansi_cb;

static const uint32_t fb_console_palette[16] = {
    0x00000000U, 0x000000AAU, 0x0000AA00U, 0x0000AAAAU,
    0x00AA0000U, 0x00AA00AAU, 0x00AA5500U, 0x00AAAAAAU,
    0x00555555U, 0x005555FFU, 0x0055FF55U, 0x0055FFFFU,
    0x00FF5555U, 0x00FF55FFU, 0x00FFFF55U, 0x00FFFFFFU,
};

static void fb_console_apply_tty_winsize(struct tty *tty) {
    if (!tty) {
        return;
    }

    tty->winsize.ws_col = (unsigned short)vt_get_width();
    tty->winsize.ws_row = (unsigned short)vt_get_visible_height();
    tty->winsize.ws_xpixel = 0;
    tty->winsize.ws_ypixel = 0;
}

static void fb_console_refresh_tty_winsizes(void) {
    int i;

    for (i = 0; i < VT_MAX; i++) {
        vt_state_t *vt = vt_get_state(i);
        if (!vt || !vt->tty) {
            continue;
        }
        fb_console_apply_tty_winsize(vt->tty);
    }
}

static uint8_t fb_console_effective_color(vt_state_t *vt, uint8_t color) {
    uint8_t fg;
    uint8_t bg;
    uint16_t attrs;

    if (!vt) {
        return color;
    }

    fg = color & 0x0FU;
    bg = (uint8_t)((color >> 4) & 0x0FU);
    attrs = vt->attrs;

    if ((attrs & ANSI_ATTR_BOLD) && fg < 8) {
        fg = (uint8_t)(fg + 8);
    }
    if (attrs & ANSI_ATTR_UNDERLINE) {
        if (fg < 8) {
            fg = (uint8_t)(fg + 8);
        } else if (fg == bg) {
            fg ^= 0x01U;
        }
    }
    if (attrs & ANSI_ATTR_REVERSE) {
        uint8_t tmp = fg;
        fg = bg;
        bg = tmp;
    }
    if (attrs & ANSI_ATTR_HIDDEN) {
        fg = bg;
    }

    return (uint8_t)(fg | (uint8_t)(bg << 4));
}

static void fb_console_draw_cell_at(int cell_x,
                                    int cell_y,
                                    unsigned char c,
                                    uint8_t color,
                                    uint16_t attrs) {
    const uint8_t *glyph;
    uint8_t modified[FB_FONT_HEIGHT];
    uint32_t fg;
    uint32_t bg;
    int px;
    int py;
    int y;
    int x;

    if (cell_x < 0 || cell_y < 0 ||
        cell_x >= (int)(fb.width / FB_FONT_WIDTH) ||
        cell_y >= (int)(fb.height / FB_FONT_HEIGHT)) {
        return;
    }

    glyph = &font_8x16[c * 16];
    memcpy(modified, glyph, sizeof(modified));
    fg = fb_console_palette[color & 0x0FU];
    bg = fb_console_palette[(color >> 4) & 0x0FU];

    if (attrs & ANSI_ATTR_REVERSE) {
        uint32_t tmp = fg;
        fg = bg;
        bg = tmp;
    }
    if (attrs & ANSI_ATTR_HIDDEN) {
        fg = bg;
    }
    if (attrs & ANSI_ATTR_BOLD) {
        for (y = 0; y < FB_FONT_HEIGHT; y++) {
            modified[y] |= (uint8_t)(modified[y] >> 1);
        }
    }
    if (attrs & ANSI_ATTR_ITALIC) {
        for (y = 0; y < FB_FONT_HEIGHT; y++) {
            int quarter = y * 4 / FB_FONT_HEIGHT;
            switch (quarter) {
            case 0: modified[y] >>= 2; break;
            case 1: modified[y] >>= 1; break;
            case 2: break;
            case 3: modified[y] <<= 1; break;
            }
        }
    }
    if (attrs & ANSI_ATTR_UNDERLINE) {
        modified[FB_FONT_HEIGHT - 2] = 0xFF;
        modified[FB_FONT_HEIGHT - 1] = 0xFF;
    }
    if (attrs & ANSI_ATTR_DIM) {
        fg = (fg >> 1) & 0x007F7F7FU;
    }
    if (attrs & ANSI_ATTR_BLINK) {
        /* Keep steady rendering; cursor/status blinking is handled separately. */
    }

    px = cell_x * FB_FONT_WIDTH;
    py = cell_y * FB_FONT_HEIGHT + view_y_offset;

    /* Hand the glyph to fb_imageblit — its mono-32bpp fast path
     * detects all-fg / all-bg byte rows and writes 8 dwords at a
     * time instead of 128 putpixel calls per cell. */
    struct fb_image_info glyph_img = {
        .dx = (uint32_t)px,
        .dy = (uint32_t)py,
        .width = FB_FONT_WIDTH,
        .height = FB_FONT_HEIGHT,
        .fg_color = fg,
        .bg_color = bg,
        .type = FB_IMAGE_MONO,
        .data = modified,
    };
    fb_imageblit(&glyph_img);
    (void)x; (void)y;
    fb_console_mark_dirty(px, py, FB_FONT_WIDTH, FB_FONT_HEIGHT);
}

static void fb_console_update_cursor_locked(vt_state_t *vt) {
    if (!vt) {
        return;
    }

    fb_console_hide_cursor();
    cursor_x = vt->col * FB_FONT_WIDTH;
    cursor_y = vt->row * FB_FONT_HEIGHT;
    fb_console_show_cursor();
}

void fb_console_sync_row(vt_state_t *vt, int row) {
    int col;
    size_t base;

    if (!vt || row < 0 || row >= vt_get_height()) {
        return;
    }

    base = (size_t)row * (size_t)vt_get_width();
    for (col = 0; col < vt_get_width(); col++) {
        uint16_t entry = vt->buffer[base + (size_t)col];
        fb_console_draw_cell_at(col, row, (unsigned char)(entry & 0xFFU),
                                (uint8_t)(entry >> 8), 0);
    }
}

int fb_console_active(void) {
    return fb_active;
}

void fb_console_redraw_active(void) {
    vt_state_t *vt;
    int row;
    int col;

    vt = vt_get_state(vt_get_active());
    if (!fb_console_active() || !vt) {
        return;
    }

    FB_LOCK();
    view_y_offset = 0;
    if (fb.set_viewport) {
        fb.set_viewport(0, 0);
    }
    (void)video_set_viewport(0, 0);
    fb_console_hide_cursor();
    for (row = 0; row < vt_get_visible_height(); row++) {
        for (col = 0; col < vt_get_width(); col++) {
            uint16_t entry = vt_get_display_cell(vt, row, col);

            fb_console_draw_cell_at(col, row, (unsigned char)(entry & 0xFFU),
                                    (uint8_t)(entry >> 8), 0);
        }
    }
    fb_console_sync_row(vt, vt_get_status_row());
    fb_console_update_cursor_locked(vt);
    FB_UNLOCK();
}

void fb_console_draw_statusline(const char *line, int cols, int row) {
    int col;
    vt_state_t *vt;

    vt = vt_get_state(vt_get_active());
    if (!fb_console_active() || !line || !vt) {
        return;
    }
    if (cols < 1) {
        return;
    }
    if (cols > vt_get_width()) {
        cols = vt_get_width();
    }
    if (row < 0 || row >= vt_get_height()) {
        return;
    }
    if (spinlock_is_held(&fb_console_lock)) {
        return;
    }

    FB_LOCK();
    fb_console_hide_cursor();
    for (col = 0; col < cols; col++) {
        fb_console_draw_cell_at(col, row, (unsigned char)line[col], 0x70, 0);
    }
    fb_console_update_cursor_locked(vt);
    FB_UNLOCK();
}

void fb_console_refresh_statusline(void) {
    vt_state_t *vt;

    vt = vt_get_state(vt_get_active());
    if (!fb_console_active() || !vt) {
        return;
    }
    if (spinlock_is_held(&fb_console_lock)) {
        return;
    }

    FB_LOCK();
    fb_console_sync_row(vt, vt_get_status_row());
    fb_console_update_cursor_locked(vt);
    FB_UNLOCK();
}

static void fb_console_store_cell_locked(vt_state_t *vt,
                                         int col,
                                         int row,
                                         unsigned char c,
                                         uint8_t color,
                                         uint16_t attrs) {
    size_t index;
    uint8_t effective;

    if (!vt || col < 0 || row < 0 ||
        col >= vt_get_width() || row >= vt_get_height()) {
        return;
    }

    effective = fb_console_effective_color(vt, color);
    index = (size_t)row * (size_t)vt_get_width() + (size_t)col;
    vt->buffer[index] = (uint16_t)c | (uint16_t)effective << 8;
    if (vt->id == vt_get_active()) {
        fb_console_draw_cell_at(col, row, c, effective, attrs);
    }
}

static void fb_console_fill_row_locked(vt_state_t *vt, int row, char c, uint8_t color) {
    int col;

    for (col = 0; col < vt_get_width(); col++) {
        fb_console_store_cell_locked(vt, col, row, (unsigned char)c, color, 0);
    }
}

static int fb_console_smooth_scroll_fullscreen(uint32_t clear_color) {
    (void)clear_color;
    return 0;
}

/*
 * Pixel-level scroll of a text-row region — moves pixels with
 * fb_copyarea() and clears the vacated band with fb_fillrect()
 * instead of issuing a per-cell glyph redraw for every row in the
 * region (which is what made scrolling look like each character
 * was being repainted individually).  Caller has already shifted
 * the cell state in vt->buffer; we just propagate that move to
 * the framebuffer in one shot.
 *
 * `dir` = -1 for scroll-up (newest at bottom),
 *         +1 for scroll-down.
 */
static void fb_console_pixel_scroll_region_locked(int top, int bottom,
                                                   int n, int dir,
                                                   uint32_t clear_color) {
    int width_px;
    int rows_in_region;
    int rows_to_move;
    int top_px;
    int bottom_px;
    int move_h;
    int clear_h;
    struct fb_copyarea_info area;
    struct fb_fillrect_info clear;

    if (n <= 0 || top > bottom) return;
    rows_in_region = bottom - top + 1;
    if (n >= rows_in_region) {
        /* Whole region cleared. */
        clear.dx = 0;
        clear.dy = (uint32_t)(top * FB_FONT_HEIGHT);
        clear.width = (uint32_t)(vt_get_width() * FB_FONT_WIDTH);
        clear.height = (uint32_t)(rows_in_region * FB_FONT_HEIGHT);
        clear.color = clear_color;
        clear.rop = ROP_COPY;
        fb_fillrect(&clear);
        fb_console_mark_dirty((int)clear.dx, (int)clear.dy,
                              (int)clear.width, (int)clear.height);
        return;
    }
    rows_to_move = rows_in_region - n;

    width_px = vt_get_width() * FB_FONT_WIDTH;
    top_px = top * FB_FONT_HEIGHT;
    bottom_px = (bottom + 1) * FB_FONT_HEIGHT;
    move_h = rows_to_move * FB_FONT_HEIGHT;
    clear_h = n * FB_FONT_HEIGHT;

    fb_console_hide_cursor();

    if (dir < 0) {
        /* Scroll up: source = (top+n)..bottom, dest = top..(bottom-n). */
        area.dx = 0;
        area.dy = (uint32_t)top_px;
        area.sx = 0;
        area.sy = (uint32_t)(top_px + clear_h);
        area.width = (uint32_t)width_px;
        area.height = (uint32_t)move_h;
        fb_copyarea(&area);
        clear.dx = 0;
        clear.dy = (uint32_t)(bottom_px - clear_h);
    } else {
        /* Scroll down: source = top..(bottom-n), dest = (top+n)..bottom. */
        area.dx = 0;
        area.dy = (uint32_t)(top_px + clear_h);
        area.sx = 0;
        area.sy = (uint32_t)top_px;
        area.width = (uint32_t)width_px;
        area.height = (uint32_t)move_h;
        fb_copyarea(&area);
        clear.dx = 0;
        clear.dy = (uint32_t)top_px;
    }

    clear.width = (uint32_t)width_px;
    clear.height = (uint32_t)clear_h;
    clear.color = clear_color;
    clear.rop = ROP_COPY;
    fb_fillrect(&clear);

    fb_console_mark_dirty(0, top_px, width_px,
                          bottom_px - top_px);
    fb_console_show_cursor();
}

static void fb_console_erase_line_segment_locked(vt_state_t *vt,
                                                 int row,
                                                 int start_col,
                                                 int end_col) {
    int col;

    if (!vt) {
        return;
    }
    if (row < 0 || row >= vt_get_visible_height()) {
        return;
    }
    if (start_col < 0) {
        start_col = 0;
    }
    if (end_col > vt_get_width()) {
        end_col = vt_get_width();
    }
    if (start_col >= end_col) {
        return;
    }

    for (col = start_col; col < end_col; col++) {
        fb_console_store_cell_locked(vt, col, row, ' ', vt->color, 0);
    }
}

static void fb_console_erase_display_locked(vt_state_t *vt, int mode) {
    int row;
    int col;
    int width;
    int height;

    if (!vt) {
        return;
    }

    width = vt_get_width();
    height = vt_get_visible_height();
    row = vt->row;
    col = vt->col;

    if (row < 0) row = 0;
    if (row >= height) row = height - 1;
    if (col < 0) col = 0;
    if (col >= width) col = width - 1;

    switch (mode) {
    case 1:
        for (int y = 0; y < row; y++) {
            fb_console_fill_row_locked(vt, y, ' ', vt->color);
        }
        fb_console_erase_line_segment_locked(vt, row, 0, col + 1);
        break;
    case 2:
        for (int y = 0; y < height; y++) {
            fb_console_fill_row_locked(vt, y, ' ', vt->color);
        }
        break;
    case 0:
    default:
        fb_console_erase_line_segment_locked(vt, row, col, width);
        for (int y = row + 1; y < height; y++) {
            fb_console_fill_row_locked(vt, y, ' ', vt->color);
        }
        break;
    }
}

static void fb_console_erase_line_locked(vt_state_t *vt, int mode) {
    int row;
    int col;
    int width;

    if (!vt) {
        return;
    }

    row = vt->row;
    col = vt->col;
    width = vt_get_width();

    if (row < 0 || row >= vt_get_visible_height()) {
        return;
    }
    if (col < 0) col = 0;
    if (col >= width) col = width - 1;

    switch (mode) {
    case 1:
        fb_console_erase_line_segment_locked(vt, row, 0, col + 1);
        break;
    case 2:
        fb_console_erase_line_segment_locked(vt, row, 0, width);
        break;
    case 0:
    default:
        fb_console_erase_line_segment_locked(vt, row, col, width);
        break;
    }
}

static void fb_console_scroll_region_up_locked(vt_state_t *vt, int top, int bottom, int n) {
    int width;
    int row;
    uint8_t effective;
    uint32_t clear_color;

    if (!vt || top < 0 || bottom >= vt_get_visible_height() || top > bottom || n <= 0) {
        return;
    }

    width = vt_get_width();
    if (n > bottom - top + 1) {
        n = bottom - top + 1;
    }

    for (row = top; row <= bottom - n; row++) {
        memcpy(&vt->buffer[(size_t)row * (size_t)width],
               &vt->buffer[(size_t)(row + n) * (size_t)width],
               (size_t)width * sizeof(uint16_t));
    }
    for (; row <= bottom; row++) {
        int col;
        for (col = 0; col < width; col++) {
            vt->buffer[(size_t)row * (size_t)width + (size_t)col] =
                (uint16_t)' ' | (uint16_t)fb_console_effective_color(vt, vt->color) << 8;
        }
    }
    effective = fb_console_effective_color(vt, vt->color);
    clear_color = fb_console_palette[(effective >> 4) & 0x0FU];
    if (vt->id == vt_get_active() && view_y_offset == 0) {
        /* Pixel-scroll requires a linear FB (any bpp).  fb_copyarea
         * is a no-op for non-linear drivers (planar VGA / Hercules /
         * CGA) — fall back to per-cell redraw on those. */
        if (fb.copyarea || fb.putpixel == linear_fb_putpixel) {
            fb_console_pixel_scroll_region_locked(top, bottom, n, -1, clear_color);
        } else {
            for (row = top; row <= bottom; row++) {
                fb_console_sync_row(vt, row);
            }
        }
        fb_console_sync_row(vt, vt_get_status_row());
    }
    (void)row;
}

static void fb_console_scroll_region_down_locked(vt_state_t *vt, int top, int bottom, int n) {
    int width;
    int row;

    if (!vt || top < 0 || bottom >= vt_get_visible_height() || top > bottom || n <= 0) {
        return;
    }

    width = vt_get_width();
    if (n > bottom - top + 1) {
        n = bottom - top + 1;
    }

    for (row = bottom; row >= top + n; row--) {
        memcpy(&vt->buffer[(size_t)row * (size_t)width],
               &vt->buffer[(size_t)(row - n) * (size_t)width],
               (size_t)width * sizeof(uint16_t));
    }
    for (; row >= top; row--) {
        int col;
        for (col = 0; col < width; col++) {
            vt->buffer[(size_t)row * (size_t)width + (size_t)col] =
                (uint16_t)' ' | (uint16_t)fb_console_effective_color(vt, vt->color) << 8;
        }
    }
    if (vt->id == vt_get_active() && view_y_offset == 0) {
        uint8_t effective = fb_console_effective_color(vt, vt->color);
        uint32_t clear_color = fb_console_palette[(effective >> 4) & 0x0FU];
        if (fb.copyarea || fb.putpixel == linear_fb_putpixel) {
            fb_console_pixel_scroll_region_locked(top, bottom, n, +1, clear_color);
        } else {
            for (row = top; row <= bottom; row++) {
                fb_console_sync_row(vt, row);
            }
        }
    }
    (void)row;
}

static int fb_vt_tty_open(struct tty *tty) {
    (void)tty;
    return 0;
}

static void fb_vt_tty_close(struct tty *tty) {
    /* If the last process holding this VT had put it into KD_GRAPHICS
     * (X server, kmscube, etc.) and exited without restoring KD_TEXT
     * — including the crash path where it doesn't get a chance to —
     * the framebuffer stays in graphics mode and the kernel console
     * silently stops drawing.  The user sees a black screen with only
     * stray new characters appearing at the cursor.
     *
     * Restore text mode + repaint when the tty closes.  This is the
     * same path KDSETMODE(KD_TEXT) takes; doing it from tty close
     * means a SEGV'd X server doesn't leave the console wedged. */
    vt_state_t *vt;

    if (!tty) return;
    vt = (vt_state_t *)tty->driver_data;
    if (!vt) return;
    if (!vt->graphics_mode) return;

    kprintf("fb_vt: tty close in KD_GRAPHICS (vt=%d) — restoring text + redrawing\n",
            vt->id);
    vt->graphics_mode = 0;
    vt->kbd_mode = K_XLATE;
    if (vt->id == vt_get_active()) {
        fb_console_redraw_active();
        vt_render_statusline(vt);
    }
}

static void fb_cb_putc(char c) {
    vt_state_t *vt = fb_current_vt_ctx;
    int bottom;

    if (!vt) {
        return;
    }

    bottom = vt->scroll_bottom;
    /*
     * pending_wrap handling — xenl ("newline ignored after wrap")
     * semantics.  The flag is set after a printable char lands in
     * the last column of a row.  Cursor stays "rest"-ing past the
     * row end until either:
     *   - a CR / explicit cursor move clears the flag (no wrap), or
     *   - the next printable char arrives, at which point we
     *     actually perform the newline-and-wrap and THEN print.
     * Match xterm / linux console behaviour exactly so applications
     * (zsh PROMPT_SP, full-screen editors, …) that depend on the
     * terminfo `xenl` cap render correctly.
     */
    if (c == '\n') {
        vt->col = 0;
        vt->pending_wrap = 0;
        if (vt->row >= bottom) {
            fb_console_scroll_region_up_locked(vt, vt->scroll_top, bottom, 1);
        } else {
            vt->row++;
        }
        return;
    }
    if (c == '\r') {
        vt->col = 0;
        vt->pending_wrap = 0;
        return;
    }
    if (c == '\b') {
        if (vt->col > 0) {
            vt->col--;
        }
        vt->pending_wrap = 0;
        return;
    }
    if (c == '\t') {
        vt->pending_wrap = 0;
        vt->col = vt->col + 1;
        while (vt->col < vt_get_width() &&
               !(vt->tab_stops[vt->col / 32] & ((uint32_t)1U << (vt->col % 32)))) {
            vt->col++;
        }
        if (vt->col >= vt_get_width()) {
            vt->col = 0;
            if (vt->row >= bottom) {
                fb_console_scroll_region_up_locked(vt, vt->scroll_top, bottom, 1);
            } else {
                vt->row++;
            }
        }
        return;
    }

    /* Printable char.  Honour any pending xenl wrap from the
     * previous put: move to the next row first, then print. */
    if (vt->pending_wrap && vt->autowrap) {
        vt->col = 0;
        if (vt->row >= bottom) {
            fb_console_scroll_region_up_locked(vt, vt->scroll_top, bottom, 1);
        } else {
            vt->row++;
        }
        vt->pending_wrap = 0;
    }

    fb_console_store_cell_locked(vt, vt->col, vt->row, (unsigned char)c, vt->color, vt->attrs);
    if (++vt->col >= vt_get_width()) {
        if (vt->autowrap) {
            /* xenl: rest at column == width, defer the actual
             * row advance until the next printable char.  Clamp
             * the recorded col to width-1 so any cursor query
             * stays in-range. */
            vt->col = vt_get_width() - 1;
            vt->pending_wrap = 1;
        } else {
            vt->col = vt_get_width() - 1;
        }
    }
}

static void fb_cb_respond(const char *buf, size_t len) {
    /* Called from inside ansi_process(), which is itself called from
     * fb_vt_tty_write() under tty->lock (taken in tty_write()).  We
     * MUST NOT re-acquire that lock — the spinlock_is_held check in
     * spinlock_acquire panics on recursive acquire, which is exactly
     * how `cat /dev/urandom` used to crash the kernel via random CSI
     * sequences.  Instead, append to raw_buf via the canonical
     * lock-held helper. */
    vt_state_t *vt = fb_current_vt_ctx;
    if (!vt || !buf || len == 0) return;
    struct tty *tty = vt->tty;
    if (!tty) return;
    (void)tty_inject_input_locked(tty, buf, len);
}

static void fb_cb_set_color(uint8_t fg, uint8_t bg) {
    if (fb_current_vt_ctx) {
        fb_current_vt_ctx->color = (uint8_t)(fg | (bg << 4));
    }
}

static void fb_cb_clear_screen(void) {
    vt_state_t *vt = fb_current_vt_ctx;

    if (!vt) {
        return;
    }

    fb_console_erase_display_locked(vt, 2);
    vt->row = 0;
    vt->col = 0;
    vt->pending_wrap = 0;
}

static void fb_cb_erase_display(int mode) {
    if (fb_current_vt_ctx) {
        fb_console_erase_display_locked(fb_current_vt_ctx, mode);
    }
}

static void fb_cb_erase_line(int mode) {
    if (fb_current_vt_ctx) {
        fb_console_erase_line_locked(fb_current_vt_ctx, mode);
    }
}

static void fb_cb_move_cursor(int row, int col) {
    vt_state_t *vt = fb_current_vt_ctx;

    if (!vt) {
        return;
    }
    if (vt->origin_mode) {
        row += vt->scroll_top;
        if (row < vt->scroll_top) row = vt->scroll_top;
        if (row > vt->scroll_bottom) row = vt->scroll_bottom;
    } else {
        if (row < 0) row = 0;
        if (row >= vt_get_visible_height()) row = vt_get_visible_height() - 1;
    }
    if (col < 0) col = 0;
    if (col >= vt_get_width()) col = vt_get_width() - 1;
    vt->row = row;
    vt->col = col;
    /* Any explicit cursor reposition cancels the deferred xenl wrap. */
    vt->pending_wrap = 0;
}

static void fb_cb_get_cursor(int *row, int *col) {
    if (fb_current_vt_ctx) {
        *row = fb_current_vt_ctx->row;
        *col = fb_current_vt_ctx->col;
    }
}

static void fb_cb_get_dimensions(int *width, int *height) {
    *width = vt_get_width();
    *height = vt_get_visible_height();
}

static void fb_cb_get_color(uint8_t *fg, uint8_t *bg) {
    if (fb_current_vt_ctx) {
        *fg = fb_current_vt_ctx->color & 0x0FU;
        *bg = (fb_current_vt_ctx->color >> 4) & 0x0FU;
    }
}

static void fb_cb_get_attrs(uint16_t *flags) {
    if (!flags) {
        return;
    }
    *flags = fb_current_vt_ctx ? fb_current_vt_ctx->attrs : 0;
}

static void fb_cb_save_cursor(void) {
    vt_state_t *vt = fb_current_vt_ctx;
    if (!vt) return;
    vt->saved_row = vt->row;
    vt->saved_col = vt->col;
    vt->saved_color = vt->color;
    vt->saved_attrs = vt->attrs;
}

static void fb_cb_restore_cursor(void) {
    vt_state_t *vt = fb_current_vt_ctx;
    if (!vt) return;
    vt->row = vt->saved_row;
    vt->col = vt->saved_col;
    vt->color = vt->saved_color;
    vt->attrs = vt->saved_attrs;
}

static void fb_cb_set_tab_stop(void) {
    vt_state_t *vt = fb_current_vt_ctx;
    if (!vt || vt->col < 0 || vt->col >= vt_get_width()) return;
    vt->tab_stops[vt->col / 32] |= (uint32_t)1U << (vt->col % 32);
}

static void fb_cb_clear_tab_stops(int mode) {
    vt_state_t *vt = fb_current_vt_ctx;
    if (!vt) return;
    if (mode == 3) {
        memset(vt->tab_stops, 0, sizeof(vt->tab_stops));
        return;
    }
    if (mode == 0 && vt->col >= 0 && vt->col < vt_get_width()) {
        vt->tab_stops[vt->col / 32] &= ~((uint32_t)1U << (vt->col % 32));
    }
}

static void fb_cb_tab_forward(int count) {
    vt_state_t *vt = fb_current_vt_ctx;
    if (!vt) return;
    while (count-- > 0) {
        int next = vt->col + 1;
        while (next < vt_get_width() &&
               !(vt->tab_stops[next / 32] & ((uint32_t)1U << (next % 32)))) {
            next++;
        }
        vt->col = (next < vt_get_width()) ? next : (vt_get_width() - 1);
    }
}

static void fb_cb_tab_backward(int count) {
    vt_state_t *vt = fb_current_vt_ctx;
    if (!vt) return;
    while (count-- > 0) {
        int prev = vt->col - 1;
        while (prev >= 0 &&
               !(vt->tab_stops[prev / 32] & ((uint32_t)1U << (prev % 32)))) {
            prev--;
        }
        vt->col = (prev >= 0) ? prev : 0;
    }
}

static void fb_cb_set_cursor_visible(int visible) {
    if (fb_current_vt_ctx) {
        fb_current_vt_ctx->cursor_visible = visible ? 1 : 0;
    }
}

static void fb_cb_insert_lines(int n) {
    vt_state_t *vt = fb_current_vt_ctx;
    if (!vt) return;
    if (vt->row >= vt->scroll_top && vt->row <= vt->scroll_bottom) {
        fb_console_scroll_region_down_locked(vt, vt->row, vt->scroll_bottom, n);
    }
}

static void fb_cb_delete_lines(int n) {
    vt_state_t *vt = fb_current_vt_ctx;
    if (!vt) return;
    if (vt->row >= vt->scroll_top && vt->row <= vt->scroll_bottom) {
        fb_console_scroll_region_up_locked(vt, vt->row, vt->scroll_bottom, n);
    }
}

static void fb_cb_insert_chars(int n) {
    vt_state_t *vt = fb_current_vt_ctx;
    int width;
    int col;
    int row;
    uint16_t empty;
    int x;

    if (!vt) return;
    width = vt_get_width();
    row = vt->row;
    col = vt->col;
    if (n > width - col) n = width - col;
    for (x = width - 1; x >= col + n; x--) {
        vt->buffer[row * width + x] = vt->buffer[row * width + x - n];
    }
    empty = (uint16_t)' ' | (uint16_t)fb_console_effective_color(vt, vt->color) << 8;
    for (x = col; x < col + n && x < width; x++) {
        vt->buffer[row * width + x] = empty;
    }
    if (vt->id == vt_get_active()) {
        fb_console_sync_row(vt, row);
    }
}

static void fb_cb_delete_chars(int n) {
    vt_state_t *vt = fb_current_vt_ctx;
    int width;
    int col;
    int row;
    uint16_t empty;
    int x;

    if (!vt) return;
    width = vt_get_width();
    row = vt->row;
    col = vt->col;
    if (n > width - col) n = width - col;
    for (x = col; x < width - n; x++) {
        vt->buffer[row * width + x] = vt->buffer[row * width + x + n];
    }
    empty = (uint16_t)' ' | (uint16_t)fb_console_effective_color(vt, vt->color) << 8;
    for (x = width - n; x < width; x++) {
        vt->buffer[row * width + x] = empty;
    }
    if (vt->id == vt_get_active()) {
        fb_console_sync_row(vt, row);
    }
}

static void fb_cb_erase_chars(int n) {
    vt_state_t *vt = fb_current_vt_ctx;
    int width;
    int col;
    int row;
    int x;

    if (!vt) return;
    width = vt_get_width();
    row = vt->row;
    col = vt->col;
    if (n > width - col) n = width - col;
    for (x = col; x < col + n; x++) {
        fb_console_store_cell_locked(vt, x, row, ' ', vt->color, 0);
    }
}

static void fb_cb_scroll_up(int n) {
    if (fb_current_vt_ctx) {
        fb_console_scroll_region_up_locked(fb_current_vt_ctx,
                                           fb_current_vt_ctx->scroll_top,
                                           fb_current_vt_ctx->scroll_bottom, n);
    }
}

static void fb_cb_scroll_down(int n) {
    if (fb_current_vt_ctx) {
        fb_console_scroll_region_down_locked(fb_current_vt_ctx,
                                             fb_current_vt_ctx->scroll_top,
                                             fb_current_vt_ctx->scroll_bottom, n);
    }
}

static void fb_cb_set_scroll_region(int top, int bottom) {
    if (fb_current_vt_ctx) {
        fb_current_vt_ctx->scroll_top = top;
        fb_current_vt_ctx->scroll_bottom = bottom;
    }
}

static void fb_cb_index_down(void) {
    vt_state_t *vt = fb_current_vt_ctx;
    if (!vt) return;
    if (vt->row >= vt->scroll_bottom) {
        fb_console_scroll_region_up_locked(vt, vt->scroll_top, vt->scroll_bottom, 1);
    } else {
        vt->row++;
    }
}

static void fb_cb_reverse_index(void) {
    vt_state_t *vt = fb_current_vt_ctx;
    if (!vt) return;
    if (vt->row <= vt->scroll_top) {
        fb_console_scroll_region_down_locked(vt, vt->scroll_top, vt->scroll_bottom, 1);
    } else {
        vt->row--;
    }
}

static void fb_cb_reset(void) {
    vt_state_t *vt = fb_current_vt_ctx;
    if (!vt) return;
    vt->row = 0;
    vt->col = 0;
    vt->color = 0x07;
    vt->attrs = 0;
    vt->scroll_top = 0;
    vt->scroll_bottom = vt_get_visible_height() - 1;
    vt->cursor_visible = 1;
    vt->cursor_blink = 1;
    vt->autowrap = 1;
    vt->pending_wrap = 0;
    vt->tab_width = 8;
    memset(vt->tab_stops, 0, sizeof(vt->tab_stops));
    for (int col = 8; col < vt_get_width(); col += 8) {
        vt->tab_stops[col / 32] |= (uint32_t)1U << (col % 32);
    }
    vt->cursor_key_app = 0;
    vt->origin_mode = 0;
    vt->bracketed_paste = 0;
    if (vt->alt_screen_active) {
        vt->alt_screen_active = 0;
    }
    ansi_init(&vt->ansi);
    fb_console_erase_display_locked(vt, 2);
}

static void fb_cb_set_attrs(uint16_t flags) {
    if (fb_current_vt_ctx) {
        fb_current_vt_ctx->attrs = flags;
    }
}

static void fb_cb_set_autowrap(int on) {
    if (fb_current_vt_ctx) {
        fb_current_vt_ctx->autowrap = on ? 1 : 0;
    }
}

static void fb_cb_set_cursor_key_app(int on) {
    if (fb_current_vt_ctx) {
        fb_current_vt_ctx->cursor_key_app = on ? 1 : 0;
    }
}

static void fb_cb_set_origin_mode(int on) {
    if (fb_current_vt_ctx) {
        fb_current_vt_ctx->origin_mode = on ? 1 : 0;
    }
}

static void fb_cb_set_alt_screen(int on) {
    vt_state_t *vt = fb_current_vt_ctx;
    int visible_rows;
    int width;
    size_t cells;

    if (!vt) return;
    visible_rows = vt_get_visible_height();
    width = vt_get_width();
    cells = (size_t)visible_rows * (size_t)width;
    if (on && !vt->alt_screen_active) {
        vt->alt_row = vt->row;
        vt->alt_col = vt->col;
        vt->alt_color = vt->color;
        vt->alt_attrs = vt->attrs;
        memcpy(vt->alt_buffer, vt->buffer, cells * sizeof(uint16_t));
        vt->alt_screen_active = 1;
        fb_console_erase_display_locked(vt, 2);
    } else if (!on && vt->alt_screen_active) {
        memcpy(vt->buffer, vt->alt_buffer, cells * sizeof(uint16_t));
        vt->row = vt->alt_row;
        vt->col = vt->alt_col;
        vt->color = vt->alt_color;
        vt->attrs = vt->alt_attrs;
        vt->alt_screen_active = 0;
        if (vt->id == vt_get_active()) {
            for (int row = 0; row < visible_rows; row++) {
                fb_console_sync_row(vt, row);
            }
        }
    }
}

static void fb_cb_set_bracketed_paste(int on) {
    if (fb_current_vt_ctx) {
        fb_current_vt_ctx->bracketed_paste = on ? 1 : 0;
    }
}

static void fb_console_write_vt_locked(vt_state_t *vt, const char *buf, size_t len) {
    size_t i;

    if (!vt || !buf || len == 0) {
        return;
    }

    fb_console_hide_cursor();
    fb_current_vt_ctx = vt;
    for (i = 0; i < len; i++) {
        ansi_process(&vt->ansi, buf[i], &fb_ansi_cb);
    }
    fb_console_update_cursor_locked(vt);
    fb_current_vt_ctx = NULL;
}

static const struct ansi_callbacks fb_ansi_cb = {
    .putc = fb_cb_putc,
    .respond = fb_cb_respond,
    .set_color = fb_cb_set_color,
    .clear_screen = fb_cb_clear_screen,
    .erase_display = fb_cb_erase_display,
    .erase_line = fb_cb_erase_line,
    .move_cursor = fb_cb_move_cursor,
    .get_cursor = fb_cb_get_cursor,
    .get_dimensions = fb_cb_get_dimensions,
    .get_color = fb_cb_get_color,
    .get_attrs = fb_cb_get_attrs,
    .save_cursor = fb_cb_save_cursor,
    .restore_cursor = fb_cb_restore_cursor,
    .set_tab_stop = fb_cb_set_tab_stop,
    .clear_tab_stops = fb_cb_clear_tab_stops,
    .tab_forward = fb_cb_tab_forward,
    .tab_backward = fb_cb_tab_backward,
    .set_cursor_visible = fb_cb_set_cursor_visible,
    .insert_lines = fb_cb_insert_lines,
    .delete_lines = fb_cb_delete_lines,
    .insert_chars = fb_cb_insert_chars,
    .delete_chars = fb_cb_delete_chars,
    .erase_chars = fb_cb_erase_chars,
    .scroll_up = fb_cb_scroll_up,
    .scroll_down = fb_cb_scroll_down,
    .set_scroll_region = fb_cb_set_scroll_region,
    .index_down = fb_cb_index_down,
    .reverse_index = fb_cb_reverse_index,
    .reset = fb_cb_reset,
    .set_attrs = fb_cb_set_attrs,
    .set_autowrap = fb_cb_set_autowrap,
    .set_cursor_key_app = fb_cb_set_cursor_key_app,
    .set_origin_mode = fb_cb_set_origin_mode,
    .set_alt_screen = fb_cb_set_alt_screen,
    .set_bracketed_paste = fb_cb_set_bracketed_paste,
};

static int fb_vt_tty_install(struct tty_driver *driver, struct tty *tty) {
    int idx;
    vt_state_t *vt;

    (void)driver;
    if (!tty) {
        return -1;
    }

    idx = tty->index;
    if (idx < 0 || idx >= VT_MAX) {
        return -1;
    }

    vt = vt_get_state(idx);
    if (!vt) {
        return -1;
    }
    if (vt->tty && vt->tty != tty) {
        return -1;
    }

    vt->tty = tty;
    tty->driver_data = vt;
    fb_console_apply_tty_winsize(tty);
    ansi_init(&vt->ansi);
    return 0;
}

static void fb_vt_tty_remove(struct tty_driver *driver, struct tty *tty) {
    vt_state_t *vt;

    (void)driver;
    if (!tty) {
        return;
    }

    vt = (vt_state_t *)tty->driver_data;
    if (vt && vt->tty == tty) {
        vt->tty = NULL;
    }
    tty->driver_data = NULL;
}

static int fb_vt_tty_write(struct tty *tty, const unsigned char *buf, int count) {
    vt_state_t *vt;

    if (!tty || !buf || count <= 0) {
        return 0;
    }

    vt = (vt_state_t *)tty->driver_data;
    if (!vt) {
        return -1;
    }

    FB_LOCK();
    fb_console_write_vt_locked(vt, (const char *)buf, (size_t)count);
    FB_UNLOCK();
    return count;
}

static int fb_vt_tty_put_char(struct tty *tty, unsigned char c) {
    return fb_vt_tty_write(tty, &c, 1);
}

static int fb_vt_tty_write_room(struct tty *tty) {
    (void)tty;
    return 2048;
}

static int fb_vt_tty_ioctl(struct tty *tty, uint32_t cmd, unsigned long arg) {
    vt_state_t *vt;
    int val;

    /* NOTE: do NOT reject arg==0 up front.  KDSETMODE and KDSKBMODE
     * pass the mode as the value of arg (not as a pointer), and
     * KD_TEXT=0 / K_RAW=0 are perfectly valid.  Each case below
     * validates arg as needed (pointer-ioctls check arg != 0
     * themselves before dereferencing). */
    if (!tty) {
        return -EINVAL;
    }

    vt = (vt_state_t *)tty->driver_data;
    if (!vt) {
        return -EINVAL;
    }

    switch (cmd) {
    case VTIOCGTABW:
        val = (int)(vt->tab_width ? vt->tab_width : 8);
        if (copyout(&val, (void *)arg, sizeof(int)) != 0) {
            return -EFAULT;
        }
        return 0;
    case VTIOCSTABW:
        if (copyin((void *)arg, &val, sizeof(int)) != 0) {
            return -EFAULT;
        }
        if (val < 1 || val > 32) {
            return -EINVAL;
        }
        vt->tab_width = (uint8_t)val;
        memset(vt->tab_stops, 0, sizeof(vt->tab_stops));
        for (int col = (int)vt->tab_width; col < vt_get_width(); col += (int)vt->tab_width) {
            vt->tab_stops[col / 32] |= (uint32_t)1U << (col % 32);
        }
        return 0;
    case VTIOCGCURSOR:
        val = vt->cursor_visible ? 1 : 0;
        if (copyout(&val, (void *)arg, sizeof(int)) != 0) {
            return -EFAULT;
        }
        return 0;
    case VTIOCSCURSOR:
        if (copyin((void *)arg, &val, sizeof(int)) != 0) {
            return -EFAULT;
        }
        vt->cursor_visible = val ? 1 : 0;
        if (vt->id == vt_get_active()) {
            fb_console_update_cursor_locked(vt);
        }
        return 0;
    case VTIOCGCURBLINK:
        val = vt->cursor_blink ? 1 : 0;
        if (copyout(&val, (void *)arg, sizeof(int)) != 0) {
            return -EFAULT;
        }
        return 0;
    case VTIOCSCURBLINK:
        if (copyin((void *)arg, &val, sizeof(int)) != 0) {
            return -EFAULT;
        }
        vt->cursor_blink = val ? 1 : 0;
        if (!vt->cursor_blink) {
            cursor_blink_phase = 1;
        }
        if (vt->id == vt_get_active()) {
            fb_console_update_cursor_locked(vt);
        }
        return 0;
    case KDGETMODE:
        val = vt->graphics_mode ? KD_GRAPHICS : KD_TEXT;
        if (copyout(&val, (void *)arg, sizeof(int)) != 0) {
            return -EFAULT;
        }
        return 0;
    case KDSETMODE:
        /* Linux KDSETMODE / KDSKBMODE pass the value directly as the
         * ioctl argument (not as a pointer to an int).  This is the
         * common asymmetry in the KD ioctl family: GET takes a
         * pointer-to-int (out), SET takes int (in by value). */
        val = (int)arg;
        if (val != KD_TEXT && val != KD_GRAPHICS) {
            return -EINVAL;
        }
        if (val == KD_GRAPHICS && !vt->graphics_mode) {
            /* Entering graphics mode: hide our cursor (so it doesn't
             * linger over X's first frame), stop fb writes via the
             * graphics_mode gate, and CRITICALLY reset the hardware
             * scroll viewport back to (0,0).  After a normal boot
             * the kernel console has scrolled, so fb.set_viewport
             * is currently pointing at some y=N*16 into the virtual
             * canvas.  Userland's mmap of /dev/fb0 maps to row 0 of
             * that canvas — if we don't reset the viewport, X paints
             * to rows that are OFFSCREEN above the visible region.
             * fbmouse-as-init worked because the viewport hadn't
             * scrolled; Xfbdev launched from a shell did not. */
            vt->graphics_mode = 1;
            FB_LOCK();
            fb_console_hide_cursor();
            view_y_offset = 0;
            if (fb.set_viewport) {
                fb.set_viewport(0, 0);
            }
            (void)video_set_viewport(0, 0);
            FB_UNLOCK();
        } else if (val == KD_TEXT && vt->graphics_mode) {
            /* Leaving graphics mode: drop the gate, then repaint
             * everything (cells + status bar + cursor) since the
             * framebuffer now belongs to whatever X drew last. */
            vt->graphics_mode = 0;
            if (vt->id == vt_get_active()) {
                fb_console_redraw_active();
                vt_render_statusline(vt);
            }
        }
        return 0;
    case KDGKBMODE:
        val = vt->kbd_mode ? vt->kbd_mode : K_XLATE;
        if (copyout(&val, (void *)arg, sizeof(int)) != 0) {
            return -EFAULT;
        }
        return 0;
    case KDSKBMODE:
        /* Same value-by-arg convention as KDSETMODE. */
        val = (int)arg;
        if (val != K_RAW && val != K_XLATE &&
            val != K_MEDIUMRAW && val != K_UNICODE && val != K_OFF) {
            return -EINVAL;
        }
        vt->kbd_mode = val;
        return 0;
    case VT_OPENQRY: {
        /* Linux returns "first unallocated VT >= 1" — substrate
         * doesn't ration VTs so we just return 1 (the active one
         * the caller opened /dev/tty0 to get to). */
        int free_vt = vt->id + 1;
        if (copyout(&free_vt, (void *)arg, sizeof(int)) != 0) {
            return -EFAULT;
        }
        return 0;
    }
    case VT_GETMODE: {
        struct vt_mode mode;
        memset(&mode, 0, sizeof(mode));
        mode.mode = VT_AUTO;   /* substrate runs in auto VT-switch mode */
        if (copyout(&mode, (void *)arg, sizeof(mode)) != 0) {
            return -EFAULT;
        }
        return 0;
    }
    case VT_SETMODE:
        /* Accept any vt_mode and ignore — substrate doesn't deliver
         * the relsig/acqsig signal protocol but the caller proceeds
         * as if it took.  KDSETMODE(KD_GRAPHICS) is the real switch. */
        return 0;
    case VT_GETSTATE: {
        struct vt_stat st;
        memset(&st, 0, sizeof(st));
        st.v_active = (unsigned short)(vt_get_active() + 1);
        st.v_state = (unsigned short)((1u << (vt->id + 1)));
        if (copyout(&st, (void *)arg, sizeof(st)) != 0) {
            return -EFAULT;
        }
        return 0;
    }
    case VT_ACTIVATE:
    case VT_WAITACTIVE:
    case VT_RELDISP:
    case VT_DISALLOCATE:
        /* Accept-and-ignore — see VT_SETMODE comment. */
        return 0;
    default:
        return -ENOTTY;
    }
}

static struct tty_driver fb_vt_driver = {
    .driver_name = "fb_vt",
    .name = "tty",
    .major = 4,
    .minor_start = 1,
    .install = fb_vt_tty_install,
    .remove = fb_vt_tty_remove,
    .open = fb_vt_tty_open,
    .close = fb_vt_tty_close,
    .write = fb_vt_tty_write,
    .put_char = fb_vt_tty_put_char,
    .write_room = fb_vt_tty_write_room,
    .ioctl = fb_vt_tty_ioctl,
};

static int fb_console_get_terminal_size(int *cols, int *rows) {
    if (!fb_active || !cols || !rows || fb.width == 0 || fb.height == 0) {
        return -1;
    }

    *cols = (int)(fb.width / FB_FONT_WIDTH);
    *rows = (int)(fb.height / FB_FONT_HEIGHT);
    if (*cols < 1) {
        *cols = 1;
    }
    if (*rows < 2) {
        *rows = 2;
    }
    return 0;
}

static void fb_console_init_ttys_once(void) {
    struct tty *tty;
    vt_state_t *vt;
    char name[16];
    int i;

    if (fb_console_tty_count >= VT_MAX) {
        return;
    }

    for (i = 0; i < VT_MAX; i++) {
        vt = vt_get_state(i);
        if (!vt) {
            continue;
        }
        if (vt->tty) {
            if (vt->tty->driver != &fb_vt_driver) {
                vt->tty->driver = &fb_vt_driver;
                if (fb_vt_tty_install(&fb_vt_driver, vt->tty) != 0) {
                    continue;
                }
                if (i == vt_get_active()) {
                    console_set_tty(vt->tty);
                }
            }
            if (fb_console_tty_count < i + 1) {
                fb_console_tty_count = i + 1;
            }
            continue;
        }

        tty = tty_alloc(&fb_vt_driver, i);
        if (!tty) {
            continue;
        }
        snprintf(name, sizeof(name), "tty%d", i + 1);
        tty_register_device(tty, name);
        fb_console_tty_count++;
        if (i == vt_get_active()) {
            console_set_tty(tty);
        }
    }
}

/* Helper for faster framebuffer copies */
static void optimized_memcpy(void *dst, const void *src, size_t n) {
    uint32_t *d = (uint32_t *)dst;
    const uint32_t *s = (const uint32_t *)src;
    size_t n_dwords = n / 4;

    /* 8-way unroll */
    while (n_dwords >= 8) {
        d[0] = s[0]; d[1] = s[1]; d[2] = s[2]; d[3] = s[3];
        d[4] = s[4]; d[5] = s[5]; d[6] = s[6]; d[7] = s[7];
        d += 8; s += 8;
        n_dwords -= 8;
    }
    while (n_dwords--) {
        *d++ = *s++;
    }
}

static void fb_console_mark_dirty(int x, int y, int w, int h) {
    int x2;
    int y2;

    if (w <= 0 || h <= 0) {
        return;
    }

    x2 = x + w;
    y2 = y + h;
    if (!dirty_valid) {
        dirty_x1 = x;
        dirty_y1 = y;
        dirty_x2 = x2;
        dirty_y2 = y2;
        dirty_valid = 1;
        dirty_ticks = 0;
        return;
    }

    if (x < dirty_x1) dirty_x1 = x;
    if (y < dirty_y1) dirty_y1 = y;
    if (x2 > dirty_x2) dirty_x2 = x2;
    if (y2 > dirty_y2) dirty_y2 = y2;
}

static size_t fb_console_cursor_row_bytes(void) {
    switch (fb.bpp) {
    case 32:
        return 32;
    case 24:
        return 24;
    case 16:
    case 15:
        return 16;
    case 8:
        return 8;
    case 4:
        return 4;
    case 2:
        return 2;
    case 1:
        return 1;
    default:
        return 0;
    }
}

static uint8_t *fb_console_pixel_addr(int x, int y) {
    uintptr_t addr;

    if (x < 0 || y < 0 || x >= (int)fb.width || y >= (int)fb.height) {
        return NULL;
    }

    addr = (uintptr_t)fb.addr + (uintptr_t)y * fb.pitch;
    switch (fb.bpp) {
    case 32:
        return (uint8_t *)(addr + (uintptr_t)x * 4U);
    case 24:
        return (uint8_t *)(addr + (uintptr_t)x * 3U);
    case 16:
    case 15:
        return (uint8_t *)(addr + (uintptr_t)x * 2U);
    case 8:
        return (uint8_t *)(addr + (uintptr_t)x);
    case 4:
        return (uint8_t *)(addr + (uintptr_t)(x / 2));
    case 2:
        return (uint8_t *)(addr + (uintptr_t)(x / 4));
    case 1:
        return (uint8_t *)(addr + (uintptr_t)(x / 8));
    default:
        return NULL;
    }
}

static void fb_console_save_cursor_background(int x, int y) {
    size_t row_bytes;
    int py;

    row_bytes = fb_console_cursor_row_bytes();
    if (row_bytes == 0) {
        cursor_saved_valid = 0;
        return;
    }

    for (py = 0; py < 2; py++) {
        uint8_t *src = fb_console_pixel_addr(x, y + FB_FONT_HEIGHT - 2 + py);
        if (!src) {
            cursor_saved_valid = 0;
            return;
        }
        memcpy(cursor_saved + (size_t)py * row_bytes, src, row_bytes);
    }
    cursor_saved_valid = 1;
}

static void fb_console_restore_cursor_background(void) {
    size_t row_bytes;
    int py;

    if (!cursor_saved_valid) {
        return;
    }

    row_bytes = fb_console_cursor_row_bytes();
    if (row_bytes == 0) {
        cursor_saved_valid = 0;
        return;
    }

    for (py = 0; py < 2; py++) {
        uint8_t *dst = fb_console_pixel_addr(cursor_draw_x,
            cursor_draw_y + FB_FONT_HEIGHT - 2 + py);
        if (!dst) {
            continue;
        }
        memcpy(dst, cursor_saved + (size_t)py * row_bytes, row_bytes);
    }
    fb_console_mark_dirty(cursor_draw_x, cursor_draw_y + FB_FONT_HEIGHT - 2,
        FB_FONT_WIDTH, 2);
    cursor_saved_valid = 0;
}

static void fb_console_xor_pixel(int x, int y) {
    uintptr_t addr;
    uint8_t *pixel;
    uint8_t shift;
    uint8_t mask;

    if (x < 0 || y < 0 || x >= (int)fb.width || y >= (int)fb.height) {
        return;
    }

    addr = (uintptr_t)fb.addr + (uintptr_t)y * fb.pitch;
    switch (fb.bpp) {
    case 32:
        *(uint32_t *)(addr + (uintptr_t)x * 4U) ^= 0xFFFFFFFFU;
        break;
    case 24:
        pixel = (uint8_t *)(addr + (uintptr_t)x * 3U);
        pixel[0] ^= 0xFFU;
        pixel[1] ^= 0xFFU;
        pixel[2] ^= 0xFFU;
        break;
    case 16:
    case 15:
        *(uint16_t *)(addr + (uintptr_t)x * 2U) ^= 0xFFFFU;
        break;
    case 8:
        *(uint8_t *)(addr + (uintptr_t)x) ^= 0xFFU;
        break;
    case 4:
        pixel = (uint8_t *)(addr + (uintptr_t)(x / 2));
        shift = (uint8_t)((1 - (x % 2)) * 4);
        mask = (uint8_t)(0x0FU << shift);
        *pixel ^= mask;
        break;
    case 2:
        pixel = (uint8_t *)(addr + (uintptr_t)(x / 4));
        shift = (uint8_t)((3 - (x % 4)) * 2);
        mask = (uint8_t)(0x03U << shift);
        *pixel ^= mask;
        break;
    case 1:
        pixel = (uint8_t *)(addr + (uintptr_t)(x / 8));
        mask = (uint8_t)(1U << (7 - (x % 8)));
        *pixel ^= mask;
        break;
    default:
        break;
    }
}

static void fb_console_toggle_cursor_at(int x, int y) {
    int py;
    int px;

    for (py = FB_FONT_HEIGHT - 2; py < FB_FONT_HEIGHT; py++) {
        for (px = 0; px < FB_FONT_WIDTH; px++) {
            fb_console_xor_pixel(x + px, y + py);
        }
    }
    fb_console_mark_dirty(x, y + FB_FONT_HEIGHT - 2, FB_FONT_WIDTH, 2);
}

static void fb_console_hide_cursor(void) {
    if (!cursor_drawn) {
        return;
    }

    fb_console_restore_cursor_background();
    cursor_drawn = 0;
}

static void fb_console_show_cursor(void) {
    if (!fb_console_cursor_should_be_visible()) {
        cursor_drawn = 0;
        cursor_saved_valid = 0;
        return;
    }

    cursor_draw_x = cursor_x;
    cursor_draw_y = cursor_y + view_y_offset;
    fb_console_save_cursor_background(cursor_draw_x, cursor_draw_y);
    fb_console_toggle_cursor_at(cursor_draw_x, cursor_draw_y);
    cursor_drawn = 1;
}

static unsigned int fb_console_flush_period_ticks(void) {
    unsigned int hz = get_hz();
    unsigned int period = (hz + FB_CONSOLE_FLUSH_HZ - 1U) / FB_CONSOLE_FLUSH_HZ;

    return period > 0 ? period : 1;
}

static unsigned int fb_console_blink_period_ticks(void) {
    unsigned int hz = get_hz();
    unsigned int period = hz / 2U;

    return period > 0 ? period : 1;
}

static int fb_console_cursor_should_be_visible(void) {
    vt_state_t *vt = vt_get_state(vt_get_active());

    if (!vt) {
        return 1;
    }
    if (!vt->cursor_visible) {
        return 0;
    }
    if (!vt->cursor_blink) {
        return 1;
    }
    return cursor_blink_phase ? 1 : 0;
}

static void fb_console_flush_dirty_internal(void) {
    int x;
    int y;
    int w;
    int h;

    if (!dirty_valid) {
        return;
    }

    fb_console_get_dirty_rect(&x, &y, &w, &h);
    if (fb.flush) {
        fb.flush(x, y, w, h);
    }
    fb_console_reset_dirty();
}

/* External framebuffer info (from fb.c) */

/* Access to current driver for set_viewport via video_set_viewport() in fb.c */

/* ==================== Console Backend ==================== */

static void fb_console_clear(void) {
    FB_LOCK();
    cursor_drawn = 0;
    cursor_saved_valid = 0;
    cursor_blink_phase = 1;
    cursor_blink_ticks = 0;
    if (fb.set_viewport) {
        view_y_offset = 0;
        fb.set_viewport(0, 0);
    }
    cursor_x = 0;
    cursor_y = 0;
    fb_clear(FB_COLOR_BLACK);
    cursor_x = 0;
    cursor_y = 0;
    view_y_offset = 0;
    dirty_valid = 0;
    dirty_ticks = 0;
    if (video_set_viewport(0, 0) != 0) {
        // failed or not supported
    }
    fb_console_mark_dirty(0, 0, (int)fb.width, (int)fb.height);
    fb_console_show_cursor();
    FB_UNLOCK();
}

static void fb_console_backend_write(const char *data, size_t len) {
    vt_state_t *vt;

    if (!fb_console_active() || !data || len == 0) {
        return;
    }

    vt = vt_get_state(vt_get_active());
    if (!vt) {
        return;
    }

    /* X server (or any framebuffer-owning userland) has put this VT
     * into KD_GRAPHICS mode.  Drop console writes silently so we
     * don't paint text over its pixel output.  Output still goes to
     * the UART backend via console_write's backend iteration, so
     * serial logging continues to work. */
    if (vt->graphics_mode) {
        return;
    }

    /* If the current CPU is already inside an FB-locked section,
     * re-entering would tripwire spinlock_acquire's deadlock check
     * (substrate's spinlocks are non-recursive).  This happens any
     * time something under FB_LOCK calls kprint — directly, or
     * indirectly via a panic emitter.  Drop the message rather than
     * deadlock; the kernel keeps making progress and the operator
     * can see the missing print in the serial log. */
    if (spinlock_is_held(&fb_console_lock)) {
        return;
    }

    FB_LOCK();
    fb_console_write_vt_locked(vt, data, len);
    FB_UNLOCK();
}

static console_backend_t fb_console_backend = {
    .name = "framebuffer",
    .write = fb_console_backend_write,
    .putchar = NULL,
    .clear = NULL,
    .get_terminal_size = fb_console_get_terminal_size,
};

void fb_console_init(void) {
    fb_console_backend.clear = fb_console_clear;
    console_register(&fb_console_backend);

    (void)vt_refresh_geometry_from_terminal();

    fb_console_init_ttys_once();
    fb_console_refresh_tty_winsizes();
    fb_console_redraw_active();
    vt_render_statusline(vt_get_state(vt_get_active()));
}

/* ==================== Character Rendering ==================== */

void fb_putc(char c, uint32_t fg, uint32_t bg) {
    if (!fb_active) return;

    fb_console_hide_cursor();

    if (c == '\n') {
        cursor_x = 0;
        cursor_y += FB_FONT_HEIGHT;
    } else if (c == '\r') {
        cursor_x = 0;
    } else {
        /* Draw Character */
        /* Adjusted for view_y_offset?
           No, putpixel coords are absolute in FB memory.
           cursor_y tracks absolute Y in FB memory.
           view_y_offset tracks where the screen starts displaying.
        */
        const uint8_t *glyph = &font_8x16[(unsigned char)c * 16];
        int draw_y = cursor_y + view_y_offset;

        for (int y = 0; y < FB_FONT_HEIGHT; y++) {
            for (int x = 0; x < FB_FONT_WIDTH; x++) {
                if (glyph[y] & (0x80 >> x)) {
                    fb_putpixel(cursor_x + x, draw_y + y, fg);
                } else if (bg != FB_COLOR_TRANSPARENT) {
                    fb_putpixel(cursor_x + x, draw_y + y, bg);
                }
            }
        }
        fb_console_mark_dirty(cursor_x, draw_y, FB_FONT_WIDTH, FB_FONT_HEIGHT);
        cursor_x += FB_FONT_WIDTH;
    }

    /* Handle line wrap */
    if (cursor_x >= (int)fb.width) {
        cursor_x = 0;
        cursor_y += FB_FONT_HEIGHT;
    }

    /* Handle scroll */
    if (cursor_y + FB_FONT_HEIGHT > (int)fb.height) {
        if (fb_console_smooth_scroll_fullscreen((bg != FB_COLOR_TRANSPARENT) ? bg : FB_COLOR_BLACK)) {
            cursor_y -= FB_FONT_HEIGHT;
        } else {
            /* Software scroll keeps the status row decoupled from the viewport. */
            void *dst = fb.addr;
            void *src = (void *)((uintptr_t)fb.addr + FB_FONT_HEIGHT * fb.pitch);
            size_t size = (fb.height - FB_FONT_HEIGHT) * fb.pitch;
            optimized_memcpy(dst, src, size);

            /* Clear bottom line */
            uint32_t clear_color = (bg != FB_COLOR_TRANSPARENT) ? bg : FB_COLOR_BLACK;
            for (uint32_t y = fb.height - FB_FONT_HEIGHT; y < fb.height; y++) {
                for (uint32_t x = 0; x < fb.width; x++) {
                    fb_putpixel(x, y, clear_color);
                }
            }
            fb_console_mark_dirty(0, 0, (int)fb.width, (int)fb.height);
            cursor_y -= FB_FONT_HEIGHT;
        }
    }

    fb_console_show_cursor();
}

void fb_write(const char *s, size_t n) {
    if (!fb_active) return;
    for (size_t i = 0; i < n; i++) {
        fb_putc(s[i], FB_COLOR_WHITE, FB_COLOR_BLACK);
    }
}

/* ==================== Attributed Character Rendering ==================== */

void fb_putc_attr(char c, uint32_t fg, uint32_t bg, uint8_t attr) {
    if (!fb_active) return;

    fb_console_hide_cursor();

    /* Handle reverse video: swap fg and bg */
    if (attr & FB_ATTR_REVERSE) {
        uint32_t tmp = fg;
        fg = bg;
        bg = tmp;
    }

    if (c == '\n' || c == '\r') {
        /* Control characters delegate to normal fb_putc (no glyph) */
        fb_putc(c, fg, bg);
        return;
    }

    /* Get base glyph */
    const uint8_t *glyph = &font_8x16[(unsigned char)c * 16];
    int draw_y = cursor_y + view_y_offset;

    /* Build a modified glyph with attributes applied */
    uint8_t modified[FB_FONT_HEIGHT];
    for (int y = 0; y < FB_FONT_HEIGHT; y++)
        modified[y] = glyph[y];

    /* Bold: shift glyph right by 1 and OR with original (double-strike) */
    if (attr & FB_ATTR_BOLD) {
        for (int y = 0; y < FB_FONT_HEIGHT; y++)
            modified[y] |= (modified[y] >> 1);
    }

    /* Italic: shear transform — shift upper rows right, lower rows left.
     * The shear is 2 pixels over the full glyph height:
     *   top quarter: shift right 2
     *   second quarter: shift right 1
     *   third quarter: no shift
     *   bottom quarter: shift left 1 (but clamp to avoid losing bits)
     */
    if (attr & FB_ATTR_ITALIC) {
        for (int y = 0; y < FB_FONT_HEIGHT; y++) {
            int quarter = y * 4 / FB_FONT_HEIGHT;
            switch (quarter) {
            case 0: modified[y] >>= 2; break;
            case 1: modified[y] >>= 1; break;
            case 2: break;  /* no shift */
            case 3: modified[y] <<= 1; break;
            }
        }
    }

    /* Underline: set bottom two rows to solid */
    if (attr & FB_ATTR_UNDERLINE) {
        modified[FB_FONT_HEIGHT - 2] = 0xFF;
        modified[FB_FONT_HEIGHT - 1] = 0xFF;
    }

    /* Strikethrough: set middle row to solid */
    if (attr & FB_ATTR_STRIKETHROUGH) {
        modified[FB_FONT_HEIGHT / 2] = 0xFF;
        modified[FB_FONT_HEIGHT / 2 + 1] = 0xFF;
    }

    /* Render the modified glyph */
    for (int y = 0; y < FB_FONT_HEIGHT; y++) {
        for (int x = 0; x < FB_FONT_WIDTH; x++) {
            if (modified[y] & (0x80 >> x)) {
                fb_putpixel(cursor_x + x, draw_y + y, fg);
            } else if (bg != FB_COLOR_TRANSPARENT) {
                fb_putpixel(cursor_x + x, draw_y + y, bg);
            }
        }
    }
    fb_console_mark_dirty(cursor_x, draw_y, FB_FONT_WIDTH, FB_FONT_HEIGHT);
    cursor_x += FB_FONT_WIDTH;

    /* Handle line wrap */
    if (cursor_x >= (int)fb.width) {
        cursor_x = 0;
        cursor_y += FB_FONT_HEIGHT;
    }

    /* Handle scroll — reuse the same logic from fb_putc */
    if (cursor_y + FB_FONT_HEIGHT > (int)fb.height) {
        if (fb_console_smooth_scroll_fullscreen((bg != FB_COLOR_TRANSPARENT) ? bg : FB_COLOR_BLACK)) {
            cursor_y -= FB_FONT_HEIGHT;
        } else {
            void *dst = fb.addr;
            void *src = (void *)((uintptr_t)fb.addr + FB_FONT_HEIGHT * fb.pitch);
            size_t size = (fb.height - FB_FONT_HEIGHT) * fb.pitch;
            optimized_memcpy(dst, src, size);
            uint32_t clear_color = (bg != FB_COLOR_TRANSPARENT) ? bg : FB_COLOR_BLACK;
            for (uint32_t fy = fb.height - FB_FONT_HEIGHT; fy < fb.height; fy++) {
                for (uint32_t fx = 0; fx < fb.width; fx++) {
                    fb_putpixel(fx, fy, clear_color);
                }
            }
            fb_console_mark_dirty(0, 0, (int)fb.width, (int)fb.height);
            cursor_y -= FB_FONT_HEIGHT;
        }
    }

    fb_console_show_cursor();
}

int fb_console_dirty_pending(void) {
    return dirty_valid;
}

void fb_console_get_dirty_rect(int *x, int *y, int *w, int *h) {
    if (!dirty_valid) {
        if (x) *x = 0;
        if (y) *y = 0;
        if (w) *w = 0;
        if (h) *h = 0;
        return;
    }

    if (x) *x = dirty_x1;
    if (y) *y = dirty_y1;
    if (w) *w = dirty_x2 - dirty_x1;
    if (h) *h = dirty_y2 - dirty_y1;
}

void fb_console_reset_dirty(void) {
    dirty_valid = 0;
    dirty_x1 = 0;
    dirty_y1 = 0;
    dirty_x2 = 0;
    dirty_y2 = 0;
    dirty_ticks = 0;
}

void fb_console_tick(void) {
    vt_state_t *vt = vt_get_state(vt_get_active());

    if (spinlock_is_held(&fb_console_lock)) {
        return;
    }
    /* In KD_GRAPHICS the framebuffer belongs to a userland consumer
     * (X server, fbmouse).  Suppress the cursor blink and the dirty-
     * flush — both reach into the framebuffer and would paint a
     * blinking text-mode cursor block on top of the user's pixels. */
    if (vt && vt->graphics_mode) {
        return;
    }
    FB_LOCK();
    if (vt && vt->cursor_visible) {
        if (!vt->cursor_blink) {
            cursor_blink_ticks = 0;
            if (!cursor_blink_phase) {
                cursor_blink_phase = 1;
                fb_console_show_cursor();
            }
        } else {
            cursor_blink_ticks++;
            if (cursor_blink_ticks >= fb_console_blink_period_ticks()) {
                cursor_blink_ticks = 0;
                cursor_blink_phase ^= 1U;
                if (cursor_blink_phase) {
                    fb_console_show_cursor();
                } else {
                    fb_console_hide_cursor();
                }
            }
        }
    } else {
        cursor_blink_ticks = 0;
        cursor_blink_phase = 1;
        fb_console_hide_cursor();
    }

    if (!dirty_valid) {
        FB_UNLOCK();
        return;
    }

    dirty_ticks++;
    if (dirty_ticks >= fb_console_flush_period_ticks()) {
        fb_console_flush_dirty_internal();
    }
    FB_UNLOCK();
}
