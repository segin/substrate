/*
 * fb_console.c - Framebuffer Console Rendering
 *
 * Handles text rendering, cursor management, and scrolling
 * on top of the framebuffer driver.
 */

#include <string.h>
#include <stdint.h>
#include <kern/console.h>
#include "fb.h"
#include "fb_console.h"
#include "font.h"

/* ==================== Constants ==================== */

#define FB_FONT_WIDTH   8
#define FB_FONT_HEIGHT  16

#define FB_COLOR_WHITE       0x00FFFFFF
#define FB_COLOR_BLACK       0x00000000
#define FB_COLOR_TRANSPARENT 0xFFFFFFFF

/* ==================== State ==================== */

static int cursor_x = 0;
static int cursor_y = 0;
static int view_y_offset = 0; /* HW Scrolling Offset */

/* External framebuffer info (from fb.c) */
extern fb_info_t fb;
extern int fb_active;

/* Access to current driver for set_viewport via video_set_viewport() in fb.c */

/* ==================== Console Backend ==================== */

static void fb_console_clear(void) {
    fb_clear(FB_COLOR_BLACK);
    cursor_x = 0;
    cursor_y = 0;
    view_y_offset = 0;
    if (video_set_viewport(0, 0) != 0) {
        // failed or not supported
    }
}

static console_backend_t fb_console_backend = {
    .name = "framebuffer",
    .write = fb_write,
    .putchar = NULL,
    .clear = NULL
};

void fb_console_init(void) {
    fb_console_backend.clear = fb_console_clear;
    console_register(&fb_console_backend);
}

/* ==================== Character Rendering ==================== */

void fb_putc(char c, uint32_t fg, uint32_t bg) {
    if (!fb_active) return;

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
        for (int y = 0; y < FB_FONT_HEIGHT; y++) {
            for (int x = 0; x < FB_FONT_WIDTH; x++) {
                if (glyph[y] & (0x80 >> x)) {
                    fb_putpixel(cursor_x + x, cursor_y + y, fg);
                } else if (bg != FB_COLOR_TRANSPARENT) {
                    fb_putpixel(cursor_x + x, cursor_y + y, bg);
                }
            }
        }
        cursor_x += FB_FONT_WIDTH;
    }

    /* Handle line wrap */
    if (cursor_x >= (int)fb.width) {
        cursor_x = 0;
        cursor_y += FB_FONT_HEIGHT;
    }

    /* Handle scroll */
    /* Visible area: [view_y_offset, view_y_offset + fb.height) */
    /* Cursor Y is the TOP of the current line being written. */
    /* If cursor_y + FB_FONT_HEIGHT > view_y_offset + fb.height */

    int visible_bottom = view_y_offset + (int)fb.height;

    if (cursor_y + FB_FONT_HEIGHT > visible_bottom) {
        /* Need to scroll */

        /* Can we hardware scroll? */
        int can_hw_scroll = (fb.virt_height > fb.height);

        if (can_hw_scroll) {
            /* Increment viewport */
            view_y_offset += FB_FONT_HEIGHT;

            /* Check if we hit the limit of virtual height */
            if (view_y_offset + (int)fb.height > (int)fb.virt_height) {
                /* Wrap around / Reset */
                /* Copy the LAST screen (fb.height) of content to the TOP of the buffer (0) */
                /* Src: fb.addr + (view_y_offset * pitch) ??
                   Actually, we want to copy the currently visible content (minus the new line we just added? No, we haven't added it yet effectively or we are about to)

                   Wait, we are here because cursor_y just advanced past the bottom.
                   So the previous lines [cursor_y - height + font_height ... cursor_y] are what we want to keep.

                   Simpler strategy:
                   Copy the current screen worth of data from (old_view_y + font_height) to 0.
                   Reset view_y_offset to 0.
                   Reset cursor_y to (fb.height - font_height).
                */

                /* Calculate source: The area that WAS visible is [view_y_offset - font_height, ...].
                   We want to preserve the screen content.
                   The content we want to keep is the last (H - font_h) pixels.
                   Src = fb.addr + (view_y_offset * pitch)
                   Dst = fb.addr
                   Size = (fb.height - font_height) * pitch

                   Wait, if we scroll down, the new line is at the bottom.
                   The new viewport will be view_y_offset.
                   But if view_y_offset + height > virt_height, we can't set it.

                   So we must wrap.
                   Copy visible window (old view_y) to 0?
                   We are adding a NEW line at the bottom.
                   So we copy the *previous* (height - font_height) lines to 0.
                   Then we set cursor_y to (height - font_height).
                   Then we clear the new line at cursor_y.
                   Then we set viewport to 0.
                */

                int old_view_y = view_y_offset - FB_FONT_HEIGHT; // The viewport BEFORE we tried to scroll

                void *dst = fb.addr;
                void *src = (void *)((uintptr_t)fb.addr + (old_view_y + FB_FONT_HEIGHT) * fb.pitch);
                size_t copy_size = (fb.height - FB_FONT_HEIGHT) * fb.pitch;

                memcpy(dst, src, copy_size);

                view_y_offset = 0;
                cursor_y = fb.height - FB_FONT_HEIGHT;

                video_set_viewport(0, 0);
            } else {
                /* Just update viewport */
                video_set_viewport(0, view_y_offset);
            }

            /* Clear the new line at cursor_y */
            /* Note: cursor_y is absolute. If we didn't wrap, cursor_y is growing. */
            uint32_t clear_color = (bg != FB_COLOR_TRANSPARENT) ? bg : FB_COLOR_BLACK;
            for (int y = cursor_y; y < cursor_y + FB_FONT_HEIGHT; y++) {
                for (uint32_t x = 0; x < fb.width; x++) {
                    fb_putpixel(x, y, clear_color);
                }
            }

        } else {
            /* Software Fallback (original logic) */
            void *dst = fb.addr;
            void *src = (void *)((uintptr_t)fb.addr + FB_FONT_HEIGHT * fb.pitch);
            size_t size = (fb.height - FB_FONT_HEIGHT) * fb.pitch;
            memcpy(dst, src, size);

            /* Clear bottom line */
            uint32_t clear_color = (bg != FB_COLOR_TRANSPARENT) ? bg : FB_COLOR_BLACK;
            for (uint32_t y = fb.height - FB_FONT_HEIGHT; y < fb.height; y++) {
                for (uint32_t x = 0; x < fb.width; x++) {
                    fb_putpixel(x, y, clear_color);
                }
            }
            cursor_y -= FB_FONT_HEIGHT;
            view_y_offset = 0; // Ensure 0
        }
    }
}

void fb_write(const char *s, size_t n) {
    if (!fb_active) return;
    for (size_t i = 0; i < n; i++) {
        fb_putc(s[i], FB_COLOR_WHITE, FB_COLOR_BLACK);
    }
}
