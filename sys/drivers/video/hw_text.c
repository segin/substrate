/*
 * hw_text.c - Hardware Text Mode Driver (VGA) & TTY Backend
 */

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <arch/x86-common/io.h>
#include <drivers/console/console.h>
#include <drivers/video/font.h>
#include <drivers/video/hw_text.h>
#include <drivers/video/vga.h>
#include <kern/ansi_handler.h>
#include <kern/cmdline.h>
#include <kern/time.h>
#include <stdio.h>
#include <sys/lock.h>
#include <sys/tty.h>
#include <sys/vt.h>

int hw_text_active = 0;
static uint16_t *vga_buffer = (uint16_t *)0xC00B8000;
static spinlock_t hw_text_lock = SPINLOCK_INIT("hw_text");
static vt_state_t *current_vt_ctx = NULL;

#define HW_TEXT_STATUS_COLOR 0xF0
#define VGA_FONT_MEM_BASE ((volatile uint8_t *)(uintptr_t)0xC00A0000)

static inline uint16_t hw_text_entry(unsigned char uc, uint8_t color) {
    return (uint16_t)uc | (uint16_t)color << 8;
}

static inline size_t hw_text_index(size_t x, size_t y) {
    return y * (size_t)vt_get_width() + x;
}

static void hw_text_apply_tty_winsize(struct tty *tty) {
    if (!tty) {
        return;
    }
    tty->winsize.ws_col = (unsigned short)vt_get_width();
    tty->winsize.ws_row = (unsigned short)vt_get_visible_height();
    tty->winsize.ws_xpixel = 0;
    tty->winsize.ws_ypixel = 0;
}

static uint8_t hw_text_read_seq(uint8_t index) {
    outb(VGA_SEQ_INDEX, index);
    return inb(VGA_SEQ_DATA);
}

static void hw_text_write_seq(uint8_t index, uint8_t value) {
    outb(VGA_SEQ_INDEX, index);
    outb(VGA_SEQ_DATA, value);
}

static uint8_t hw_text_read_gc(uint8_t index) {
    outb(VGA_GC_INDEX, index);
    return inb(VGA_GC_DATA);
}

static void hw_text_write_gc(uint8_t index, uint8_t value) {
    outb(VGA_GC_INDEX, index);
    outb(VGA_GC_DATA, value);
}

static uint8_t hw_text_read_crtc(uint8_t index) {
    outb(VGA_CRTC_INDEX_COLOR, index);
    return inb(VGA_CRTC_DATA_COLOR);
}

static void hw_text_write_crtc(uint8_t index, uint8_t value) {
    outb(VGA_CRTC_INDEX_COLOR, index);
    outb(VGA_CRTC_DATA_COLOR, value);
}

static void hw_text_load_font_plane(const uint8_t *font, size_t glyph_height) {
    uint8_t old_seq_map_mask;
    uint8_t old_seq_mem_mode;
    uint8_t old_gc_read_map;
    uint8_t old_gc_mode;
    uint8_t old_gc_misc;
    volatile uint8_t *font_mem = VGA_FONT_MEM_BASE;
    size_t ch;

    old_seq_map_mask = hw_text_read_seq(VGA_SEQ_MAP_MASK);
    old_seq_mem_mode = hw_text_read_seq(VGA_SEQ_MEM_MODE);
    old_gc_read_map = hw_text_read_gc(VGA_GC_READ_MAP_SEL);
    old_gc_mode = hw_text_read_gc(VGA_GC_MODE);
    old_gc_misc = hw_text_read_gc(VGA_GC_MISC);

    hw_text_write_seq(VGA_SEQ_MAP_MASK, 0x04);
    hw_text_write_seq(VGA_SEQ_MEM_MODE, 0x07);
    hw_text_write_gc(VGA_GC_READ_MAP_SEL, 0x02);
    hw_text_write_gc(VGA_GC_MODE, 0x00);
    hw_text_write_gc(VGA_GC_MISC, 0x04);

    for (ch = 0; ch < 256; ch++) {
        size_t off = ch * 32;
        size_t row;

        for (row = 0; row < 32; row++) {
            font_mem[off + row] = 0;
        }
        for (row = 0; row < glyph_height; row++) {
            font_mem[off + row] = font[ch * glyph_height + row];
        }
    }

    hw_text_write_seq(VGA_SEQ_MAP_MASK, old_seq_map_mask);
    hw_text_write_seq(VGA_SEQ_MEM_MODE, old_seq_mem_mode);
    hw_text_write_gc(VGA_GC_READ_MAP_SEL, old_gc_read_map);
    hw_text_write_gc(VGA_GC_MODE, old_gc_mode);
    hw_text_write_gc(VGA_GC_MISC, old_gc_misc);
}

static void hw_text_program_80x25(void) {
    uint8_t max_scan;

    hw_text_load_font_plane(font_8x16, 16);
    max_scan = hw_text_read_crtc(VGA_CRTC_MAX_SCAN);
    hw_text_write_crtc(VGA_CRTC_MAX_SCAN, (uint8_t)((max_scan & 0xE0U) | 0x0FU));
}

static void hw_text_program_80x50(void) {
    uint8_t max_scan;

    hw_text_load_font_plane(font_8x8, 8);
    max_scan = hw_text_read_crtc(VGA_CRTC_MAX_SCAN);
    hw_text_write_crtc(VGA_CRTC_MAX_SCAN, (uint8_t)((max_scan & 0xE0U) | 0x07U));
}

static int hw_text_parse_geometry(const char *value, int *cols, int *rows) {
    int parsed_cols = 0;
    int parsed_rows = 0;
    const char *sep;
    const char *p;

    if (!value || !cols || !rows) {
        return -1;
    }

    sep = strchr(value, 'x');
    if (!sep) {
        sep = strchr(value, 'X');
    }
    if (!sep) {
        return -1;
    }

    for (p = value; p < sep; p++) {
        if (*p < '0' || *p > '9') {
            return -1;
        }
        parsed_cols = parsed_cols * 10 + (*p - '0');
    }
    for (p = sep + 1; *p; p++) {
        if (*p < '0' || *p > '9') {
            return -1;
        }
        parsed_rows = parsed_rows * 10 + (*p - '0');
    }
    if (parsed_cols < 1 || parsed_rows < 2 ||
        parsed_cols > VT_MAX_WIDTH || parsed_rows > VT_MAX_HEIGHT) {
        return -1;
    }

    *cols = parsed_cols;
    *rows = parsed_rows;
    return 0;
}

static int hw_text_get_requested_geometry(int *cols, int *rows) {
    char textmode[32];
    char video[32];

    if (cmdline_get("textmode", textmode, sizeof(textmode)) == 0) {
        return hw_text_parse_geometry(textmode, cols, rows);
    }

    if (cmdline_get("video", video, sizeof(video)) == 0) {
        if (strncmp(video, "text:", 5) == 0) {
            return hw_text_parse_geometry(video + 5, cols, rows);
        }
        if (strcmp(video, "text") == 0) {
            *cols = VT_DEFAULT_WIDTH;
            *rows = VT_DEFAULT_HEIGHT;
            return 0;
        }
    }

    return -1;
}

static int hw_text_requested_geometry_from_bios(void) {
    char flag[8];

    return cmdline_get("textmode_bios", flag, sizeof(flag)) == 0 &&
           strcmp(flag, "1") == 0;
}

static void hw_text_apply_geometry_or_default(int *cols_out, int *rows_out) {
    int requested_cols = VT_DEFAULT_WIDTH;
    int requested_rows = VT_DEFAULT_HEIGHT;
    int actual_cols = VT_DEFAULT_WIDTH;
    int actual_rows = VT_DEFAULT_HEIGHT;
    int have_request = (hw_text_get_requested_geometry(&requested_cols, &requested_rows) == 0);
    int bios_selected = hw_text_requested_geometry_from_bios();

    if (have_request) {
        if (requested_cols == 80 && requested_rows == 25) {
            hw_text_program_80x25();
            actual_cols = 80;
            actual_rows = 25;
        } else if (bios_selected) {
            actual_cols = requested_cols;
            actual_rows = requested_rows;
        } else if (requested_cols == 80 && requested_rows == 50) {
            hw_text_program_80x50();
            actual_cols = 80;
            actual_rows = 50;
        } else {
            kprintf("hw_text: text mode %dx%d requires a BIOS real-mode setup path; using 80x25\n",
                    requested_cols, requested_rows);
        }
    }

    if (vt_set_geometry(actual_cols, actual_rows) != 0) {
        (void)vt_set_geometry(VT_DEFAULT_WIDTH, VT_DEFAULT_HEIGHT);
        actual_cols = VT_DEFAULT_WIDTH;
        actual_rows = VT_DEFAULT_HEIGHT;
    }

    if (cols_out) {
        *cols_out = actual_cols;
    }
    if (rows_out) {
        *rows_out = actual_rows;
    }
}

static void hw_text_write_cell_locked(vt_state_t *vt, size_t x, size_t y,
                                      char c, uint8_t color) {
    size_t index;

    if (!vt || x >= (size_t)vt_get_width() || y >= (size_t)vt_get_height()) {
        return;
    }

    index = hw_text_index(x, y);
    vt->buffer[index] = hw_text_entry((unsigned char)c, color);
    if (vt->id == vt_get_active()) {
        vga_buffer[index] = vt->buffer[index];
    }
}

static void hw_text_fill_row_locked(vt_state_t *vt, size_t row, char c, uint8_t color) {
    size_t x;

    for (x = 0; x < (size_t)vt_get_width(); x++) {
        hw_text_write_cell_locked(vt, x, row, c, color);
    }
}

static void hw_text_update_cursor_locked(vt_state_t *vt) {
    uint16_t pos;
    int row;
    int col;

    if (!vt || vt->id != vt_get_active()) {
        return;
    }

    row = vt->row;
    col = vt->col;
    if (row < 0) {
        row = 0;
    }
    if (row >= vt_get_visible_height()) {
        row = vt_get_visible_height() - 1;
    }
    if (col < 0) {
        col = 0;
    }
    if (col >= vt_get_width()) {
        col = vt_get_width() - 1;
    }

    pos = (uint16_t)(row * vt_get_width() + col);
    outb(0x3D4, 0x0E);
    outb(0x3D5, (pos >> 8) & 0xFF);
    outb(0x3D4, 0x0F);
    outb(0x3D5, pos & 0xFF);
}

static void hw_text_scroll_locked(vt_state_t *vt) {
    size_t line_size;
    size_t scroll_size;
    uint16_t empty;
    uint16_t *last_line;
    int visible_rows;
    size_t x;

    if (!vt) {
        return;
    }

    visible_rows = vt_get_visible_height();
    if (visible_rows <= 1) {
        hw_text_fill_row_locked(vt, 0, ' ', vt->color);
        return;
    }

    line_size = (size_t)vt_get_width() * sizeof(uint16_t);
    scroll_size = (size_t)(visible_rows - 1) * line_size;
    memcpy(vt->buffer, (void *)((uintptr_t)vt->buffer + line_size), scroll_size);

    empty = hw_text_entry(' ', vt->color);
    last_line = vt->buffer + ((size_t)(visible_rows - 1) * (size_t)vt_get_width());
    for (x = 0; x < (size_t)vt_get_width(); x++) {
        last_line[x] = empty;
    }

    if (vt->id == vt_get_active()) {
        memcpy(vga_buffer, vt->buffer, vt_get_cell_count() * sizeof(uint16_t));
    }
}

static void hw_text_putentryat_locked(vt_state_t *vt, char c, uint8_t color, size_t x, size_t y) {
    hw_text_write_cell_locked(vt, x, y, c, color);
}

static void hw_text_civil_from_days(int64_t z, int *year, unsigned *month, unsigned *day) {
    int64_t era;
    unsigned doe;
    unsigned yoe;
    unsigned doy;
    unsigned mp;

    z += 719468;
    era = (z >= 0 ? z : z - 146096) / 146097;
    doe = (unsigned)(z - era * 146097);
    yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    *year = (int)(yoe + era * 400);
    doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    mp = (5 * doy + 2) / 153;
    *day = doy - (153 * mp + 2) / 5 + 1;
    *month = mp + (mp < 10 ? 3 : -9);
    *year += (*month <= 2);
}

static void hw_text_format_iso8601(char *buf, size_t buf_len, time_t when) {
    int year;
    unsigned month;
    unsigned day;
    uint64_t secs;
    uint64_t days;
    uint64_t sod;
    unsigned hour;
    unsigned minute;
    unsigned second;

    if (!buf || buf_len == 0) {
        return;
    }

    if (when < 0) {
        when = 0;
    }
    secs = (uint64_t)when;
    days = secs / 86400ULL;
    sod = secs % 86400ULL;
    hour = (unsigned)(sod / 3600ULL);
    minute = (unsigned)((sod % 3600ULL) / 60ULL);
    second = (unsigned)(sod % 60ULL);
    hw_text_civil_from_days((int64_t)days, &year, &month, &day);

    snprintf(buf, buf_len, "%04d-%02u-%02uT%02u:%02u:%02uZ",
             year, month, day, hour, minute, second);
}

static void hw_text_render_statusline_locked(vt_state_t *vt) {
    char left[16];
    char right[32];
    size_t left_len;
    size_t right_len;
    int cols;
    int row;
    int x;

    if (!vt) {
        return;
    }

    cols = vt_get_width();
    row = vt_get_status_row();
    snprintf(left, sizeof(left), " VT%d ", vt->id + 1);
    hw_text_format_iso8601(right, sizeof(right), get_time());
    left_len = strlen(left);
    right_len = strlen(right);

    hw_text_fill_row_locked(vt, (size_t)row, ' ', HW_TEXT_STATUS_COLOR);

    for (x = 0; x < cols && (size_t)x < left_len; x++) {
        hw_text_write_cell_locked(vt, (size_t)x, (size_t)row, left[x], HW_TEXT_STATUS_COLOR);
    }

    if ((int)right_len < cols) {
        size_t start = (size_t)(cols - (int)right_len);
        for (x = 0; x < (int)right_len; x++) {
            hw_text_write_cell_locked(vt, start + (size_t)x, (size_t)row,
                                      right[x], HW_TEXT_STATUS_COLOR);
        }
    }
}

static void cb_putc(char c) {
    vt_state_t *vt = current_vt_ctx;
    int visible_rows;

    if (!vt) {
        return;
    }

    visible_rows = vt_get_visible_height();
    if (c == '\n') {
        vt->col = 0;
        if (++vt->row == visible_rows) {
            vt->row--;
            hw_text_scroll_locked(vt);
        }
        return;
    }
    if (c == '\r') {
        vt->col = 0;
        return;
    }
    if (c == '\b') {
        if (vt->col > 0) {
            vt->col--;
        }
        return;
    }
    if (c == '\t') {
        vt->col = (vt->col + 8) & ~7;
        if (vt->col >= vt_get_width()) {
            vt->col = 0;
            if (++vt->row == visible_rows) {
                vt->row--;
                hw_text_scroll_locked(vt);
            }
        }
        return;
    }

    hw_text_putentryat_locked(vt, c, vt->color, (size_t)vt->col, (size_t)vt->row);
    if (++vt->col == vt_get_width()) {
        vt->col = 0;
        if (++vt->row == visible_rows) {
            vt->row--;
            hw_text_scroll_locked(vt);
        }
    }
}

static void cb_set_color(uint8_t fg, uint8_t bg) {
    if (current_vt_ctx) {
        current_vt_ctx->color = (uint8_t)(fg | (bg << 4));
    }
}

static void cb_clear_screen(void) {
    vt_state_t *vt = current_vt_ctx;
    size_t y;
    size_t x;

    if (!vt) {
        return;
    }

    for (y = 0; y < (size_t)vt_get_visible_height(); y++) {
        for (x = 0; x < (size_t)vt_get_width(); x++) {
            hw_text_putentryat_locked(vt, ' ', vt->color, x, y);
        }
    }
    vt->row = 0;
    vt->col = 0;
    hw_text_render_statusline_locked(vt);
}

static void cb_move_cursor(int row, int col) {
    vt_state_t *vt = current_vt_ctx;

    if (!vt) {
        return;
    }

    if (row < 0) {
        row = 0;
    }
    if (row >= vt_get_visible_height()) {
        row = vt_get_visible_height() - 1;
    }
    if (col < 0) {
        col = 0;
    }
    if (col >= vt_get_width()) {
        col = vt_get_width() - 1;
    }

    vt->row = row;
    vt->col = col;
    hw_text_update_cursor_locked(vt);
}

static void cb_get_cursor(int *row, int *col) {
    if (current_vt_ctx) {
        *row = current_vt_ctx->row;
        *col = current_vt_ctx->col;
    }
}

static void cb_get_dimensions(int *width, int *height) {
    *width = vt_get_width();
    *height = vt_get_visible_height();
}

static void cb_get_color(uint8_t *fg, uint8_t *bg) {
    if (current_vt_ctx) {
        *fg = current_vt_ctx->color & 0x0F;
        *bg = (current_vt_ctx->color >> 4) & 0x0F;
    }
}

static const struct ansi_callbacks ansi_cb = {
    .putc = cb_putc,
    .set_color = cb_set_color,
    .clear_screen = cb_clear_screen,
    .move_cursor = cb_move_cursor,
    .get_cursor = cb_get_cursor,
    .get_dimensions = cb_get_dimensions,
    .get_color = cb_get_color,
};

static int vt_tty_open(struct tty *tty) {
    int idx = tty->index;
    vt_state_t *vt;

    if (idx < 0 || idx >= VT_MAX) {
        return -1;
    }

    vt = vt_get_state(idx);
    vt->tty = tty;
    tty->driver_data = vt;
    hw_text_apply_tty_winsize(tty);
    return 0;
}

static void vt_tty_close(struct tty *tty) {
    vt_state_t *vt = (vt_state_t *)tty->driver_data;
    if (vt) {
        vt->tty = NULL;
    }
}

static int vt_tty_write(struct tty *tty, const unsigned char *buf, int count) {
    vt_state_t *vt = (vt_state_t *)tty->driver_data;
    int i;

    if (!vt) {
        return 0;
    }

    spinlock_acquire(&hw_text_lock);
    current_vt_ctx = vt;
    for (i = 0; i < count; i++) {
        ansi_process(&vt->ansi, buf[i], &ansi_cb);
    }
    hw_text_render_statusline_locked(vt);
    hw_text_update_cursor_locked(vt);
    current_vt_ctx = NULL;
    spinlock_release(&hw_text_lock);
    return count;
}

static int vt_tty_put_char(struct tty *tty, unsigned char c) {
    return vt_tty_write(tty, &c, 1);
}

static int vt_tty_write_room(struct tty *tty) {
    (void)tty;
    return 2048;
}

static int vt_tty_ioctl(struct tty *tty, uint32_t cmd, unsigned long arg) {
    (void)tty;
    (void)cmd;
    (void)arg;
    return -1;
}

static struct tty_driver vt_driver = {
    .driver_name = "vga_vt",
    .name = "tty",
    .major = 4,
    .minor_start = 1,
    .open = vt_tty_open,
    .close = vt_tty_close,
    .write = vt_tty_write,
    .put_char = vt_tty_put_char,
    .write_room = vt_tty_write_room,
    .ioctl = vt_tty_ioctl,
};

void hw_text_console_write_shim(const char *data, size_t len) {
    vt_state_t *vt = vt_get_state(vt_get_active());
    size_t i;

    if (!vt) {
        return;
    }

    spinlock_acquire(&hw_text_lock);
    current_vt_ctx = vt;
    for (i = 0; i < len; i++) {
        ansi_process(&vt->ansi, data[i], &ansi_cb);
    }
    hw_text_render_statusline_locked(vt);
    hw_text_update_cursor_locked(vt);
    current_vt_ctx = NULL;
    spinlock_release(&hw_text_lock);
}

static console_backend_t vt_kprint_backend = {
    .name = "vga_vt",
    .write = hw_text_console_write_shim,
};

void hw_text_refresh_statusline(void) {
    vt_state_t *vt = vt_get_state(vt_get_active());

    if (!hw_text_active || !vt) {
        return;
    }

    spinlock_acquire(&hw_text_lock);
    hw_text_render_statusline_locked(vt);
    hw_text_update_cursor_locked(vt);
    spinlock_release(&hw_text_lock);
}

void hw_text_tick_1hz(void) {
    hw_text_refresh_statusline();
}

void hw_text_set_color(uint8_t fg, uint8_t bg) {
    vt_state_t *vt = vt_get_state(vt_get_active());
    if (vt) {
        vt->color = (uint8_t)(fg | (bg << 4));
    }
}

void hw_text_init(void) {
    int cols;
    int rows;
    struct tty *tty;

    hw_text_apply_geometry_or_default(&cols, &rows);
    vt_init();

    tty = tty_alloc(&vt_driver, 0);
    if (tty) {
        char name[16];

        snprintf(name, sizeof(name), "tty1");
        (void)name;
        hw_text_apply_tty_winsize(tty);
        console_set_tty(tty);
    }

    hw_text_active = 1;
    console_register(&vt_kprint_backend);

    hw_text_refresh_statusline();
    kprintf("VGA VT Driver Initialized (%dx%d physical, %d usable rows).\n",
            cols, rows, vt_get_visible_height());
}
