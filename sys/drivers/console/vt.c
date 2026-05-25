/*
 * sys/drivers/console/vt.c - Virtual Terminal Logic
 */

#include <sys/vt.h>
#include <sys/vtio.h>           /* K_XLATE / K_RAW / KDSKBMODE */
#include <drivers/video/hw_text.h>
#include <drivers/video/fb_console.h>
#include <drivers/input/keyboard.h>
#include <kern/console.h>
#include <string.h>
#include <sys/tty.h>
#include <sys/lock.h>
#include <arch/i386/intr.h>
#include <kern/time.h>
#include <arch/x86-common/rtc.h>
#include <stdio.h>

static vt_state_t vt_states[VT_MAX];
static int active_vt = 0;
static int vt_width = VT_DEFAULT_WIDTH;
static int vt_height = VT_DEFAULT_HEIGHT;
static int vt_initialized = 0;
static uint16_t vt_buffer_scratch[VT_MAX_BUF_SIZE];

/*
 * Serializes vt_activate() so a switch can't race with concurrent
 * console writes to the previously-active VT.  Held only briefly
 * around the active_vt flip + LED/console handoff; per-VT cell state
 * remains protected by the framebuffer/console layer's own locks.
 */
static spinlock_t vt_switch_lock = SPINLOCK_INIT("vt_switch");

void vt_redraw_active(void);

static size_t vt_cell_count_internal(void) {
    return (size_t)vt_width * (size_t)vt_height;
}

static size_t vt_row_offset(size_t row) {
    return row * (size_t)vt_width;
}

static size_t vt_scrollback_slot(const vt_state_t *vt, size_t logical_index) {
    size_t oldest;

    oldest = (size_t)((vt->scrollback_head + VT_SCROLLBACK_LINES - vt->scrollback_count) %
                      VT_SCROLLBACK_LINES);
    return (oldest + logical_index) % VT_SCROLLBACK_LINES;
}

static void vt_init_tab_stops(vt_state_t *vt, int width) {
    int col;
    int spacing;

    spacing = vt && vt->tab_width ? vt->tab_width : 8;

    memset(vt->tab_stops, 0, sizeof(vt->tab_stops));
    for (col = spacing; col < width; col += spacing) {
        vt->tab_stops[col / 32] |= (uint32_t)1U << (col % 32);
    }
}

static void vt_get_terminal_geometry(const vt_state_t *vt, int *cols, int *rows) {
    int effective_cols = vt_width;
    int effective_rows = vt_height;

    if (vt && vt->id == vt_get_active() &&
        console_get_terminal_size(&effective_cols, &effective_rows) == 0) {
        if (cols) {
            *cols = effective_cols;
        }
        if (rows) {
            *rows = effective_rows;
        }
        return;
    }

    if (effective_cols < 1 || effective_cols > VT_MAX_WIDTH) {
        effective_cols = vt_width;
    }
    if (effective_rows < 2 || effective_rows > VT_MAX_HEIGHT) {
        effective_rows = vt_height;
    }

    if (cols) {
        *cols = effective_cols;
    }
    if (rows) {
        *rows = effective_rows;
    }
}

static int vt_clamp_int(int value, int min_value, int max_value) {
    if (value < min_value) {
        return min_value;
    }
    if (value > max_value) {
        return max_value;
    }
    return value;
}

static void vt_reflow_buffer(uint16_t *buffer,
                             int old_width,
                             int old_height,
                             int new_width,
                             int new_height,
                             uint16_t fill) {
    int copy_rows;
    int copy_cols;
    int row;

    memset(vt_buffer_scratch, 0, sizeof(vt_buffer_scratch));
    for (row = 0; row < new_height; row++) {
        int col;

        for (col = 0; col < new_width; col++) {
            vt_buffer_scratch[(size_t)row * (size_t)new_width + (size_t)col] = fill;
        }
    }

    copy_rows = old_height < new_height ? old_height : new_height;
    copy_cols = old_width < new_width ? old_width : new_width;
    for (row = 0; row < copy_rows; row++) {
        memcpy(&vt_buffer_scratch[(size_t)row * (size_t)new_width],
               &buffer[(size_t)row * (size_t)old_width],
               (size_t)copy_cols * sizeof(uint16_t));
    }

    memcpy(buffer, vt_buffer_scratch, sizeof(vt_buffer_scratch));
}

static void vt_reflow_scrollback(vt_state_t *vt, int old_width, int new_width, uint16_t fill) {
    uint16_t scratch_line[VT_MAX_WIDTH];
    size_t copy_cols;
    int i;

    if (!vt || old_width == new_width) {
        return;
    }

    copy_cols = (size_t)(old_width < new_width ? old_width : new_width);
    for (i = 0; i < VT_SCROLLBACK_LINES; i++) {
        memset(scratch_line, 0, sizeof(scratch_line));
        memcpy(scratch_line, vt->scrollback[i], copy_cols * sizeof(uint16_t));
        for (int col = old_width; col < new_width; col++) {
            scratch_line[col] = fill;
        }
        memcpy(vt->scrollback[i], scratch_line, sizeof(scratch_line));
    }
}

static void vt_apply_geometry_to_state(vt_state_t *vt) {
    int visible_rows;

    if (!vt) {
        return;
    }

    visible_rows = vt_get_visible_height();
    if (visible_rows < 1) {
        visible_rows = 1;
    }

    if (vt->row < 0) {
        vt->row = 0;
    } else if (vt->row >= visible_rows) {
        vt->row = visible_rows - 1;
    }
    if (vt->saved_row < 0) {
        vt->saved_row = 0;
    } else if (vt->saved_row >= visible_rows) {
        vt->saved_row = visible_rows - 1;
    }
    if (vt->alt_row < 0) {
        vt->alt_row = 0;
    } else if (vt->alt_row >= visible_rows) {
        vt->alt_row = visible_rows - 1;
    }

    if (vt->col < 0) {
        vt->col = 0;
    } else if (vt->col >= vt_width) {
        vt->col = vt_width - 1;
    }
    if (vt->saved_col < 0) {
        vt->saved_col = 0;
    } else if (vt->saved_col >= vt_width) {
        vt->saved_col = vt_width - 1;
    }
    if (vt->alt_col < 0) {
        vt->alt_col = 0;
    } else if (vt->alt_col >= vt_width) {
        vt->alt_col = vt_width - 1;
    }

    if (vt->scroll_top < 0 || vt->scroll_top >= visible_rows) {
        vt->scroll_top = 0;
    }
    if (vt->scroll_bottom < vt->scroll_top || vt->scroll_bottom >= visible_rows) {
        vt->scroll_bottom = visible_rows - 1;
    }

    if (vt->tab_width == 0) {
        vt->tab_width = 8;
    }
    vt_init_tab_stops(vt, vt_width);

    if (vt->tty) {
        vt->tty->winsize.ws_col = (unsigned short)vt_width;
        vt->tty->winsize.ws_row = (unsigned short)visible_rows;
        vt->tty->winsize.ws_xpixel = 0;
        vt->tty->winsize.ws_ypixel = 0;
    }
}

static void vt_reconfigure_state(vt_state_t *vt,
                                 int old_width,
                                 int old_height,
                                 int new_width,
                                 int new_height) {
    uint16_t blank;
    int old_visible;
    int new_visible;
    int status_row;

    if (!vt) {
        return;
    }

    blank = (uint16_t)' ' | (uint16_t)0x07U << 8;
    old_visible = old_height - 1;
    new_visible = new_height - 1;

    vt_reflow_buffer(vt->buffer, old_width, old_visible, new_width, new_visible, blank);
    vt_reflow_buffer(vt->alt_buffer, old_width, old_visible, new_width, new_visible, blank);
    vt_reflow_scrollback(vt, old_width, new_width, blank);

    status_row = new_height - 1;
    for (int col = 0; col < new_width; col++) {
        vt->buffer[(size_t)status_row * (size_t)new_width + (size_t)col] = blank;
    }

    vt->row = vt_clamp_int(vt->row, 0, new_visible - 1);
    vt->saved_row = vt_clamp_int(vt->saved_row, 0, new_visible - 1);
    vt->alt_row = vt_clamp_int(vt->alt_row, 0, new_visible - 1);
    vt->col = vt_clamp_int(vt->col, 0, new_width - 1);
    vt->saved_col = vt_clamp_int(vt->saved_col, 0, new_width - 1);
    vt->alt_col = vt_clamp_int(vt->alt_col, 0, new_width - 1);
    vt->scrollback_view = (uint16_t)vt_clamp_int(vt->scrollback_view, 0, vt->scrollback_count);

    /* If the scroll region was the full visible screen before the
     * resize, grow it to the full visible screen after.  Without
     * this, a default 80x25 -> 1024x768 transition leaves the scroll
     * region clamped to rows 0..23 because scroll_bottom=23 is still
     * "in range" of the new 47-row visible area — output past row 23
     * never scrolls and the terminal acts 80x24. */
    if (vt->scroll_top == 0 && vt->scroll_bottom == old_visible - 1) {
        vt->scroll_bottom = new_visible - 1;
    }

    vt_apply_geometry_to_state(vt);
}

int vt_set_geometry(int cols, int rows) {
    int old_width;
    int old_height;

    if (cols < 1 || cols > VT_MAX_WIDTH || rows < 2 || rows > VT_MAX_HEIGHT) {
        return -1;
    }

    old_width = vt_width;
    old_height = vt_height;
    if (cols == old_width && rows == old_height) {
        return 0;
    }

    vt_width = cols;
    vt_height = rows;
    if (!vt_initialized) {
        return 0;
    }

    for (int i = 0; i < VT_MAX; i++) {
        vt_reconfigure_state(&vt_states[i], old_width, old_height, cols, rows);
    }
    return 0;
}

int vt_refresh_geometry_from_terminal(void) {
    int cols;
    int rows;

    if (console_get_terminal_size(&cols, &rows) != 0) {
        return -1;
    }

    return vt_set_geometry(cols, rows);
}

int vt_get_width(void) {
    return vt_width;
}

int vt_get_height(void) {
    return vt_height;
}

int vt_get_visible_height(void) {
    return vt_height - 1;
}

int vt_get_status_row(void) {
    return vt_height - 1;
}

size_t vt_get_cell_count(void) {
    return vt_cell_count_internal();
}

void vt_init(void) {
    for (int i = 0; i < VT_MAX; i++) {
        vt_states[i].id = i;
        vt_states[i].row = 0;
        vt_states[i].col = 0;
        vt_states[i].color = 0x07; // Light Grey on Black
        vt_states[i].attrs = 0;
        vt_states[i].tty = NULL;
        vt_states[i].scrollback_head = 0;
        vt_states[i].scrollback_count = 0;
        vt_states[i].scrollback_view = 0;
        vt_states[i].saved_row = 0;
        vt_states[i].saved_col = 0;
        vt_states[i].saved_color = 0x07;
        vt_states[i].saved_attrs = 0;
        vt_states[i].scroll_top = 0;
        vt_states[i].scroll_bottom = vt_get_visible_height() - 1;
        vt_states[i].cursor_visible = 1;
        vt_states[i].cursor_blink = 1;
        vt_states[i].tab_width = 8;
        vt_states[i].autowrap = 1;         /* DECAWM on by default */
        /* kbd_mode: K_XLATE is the canonical default (cooked
         * keystrokes feed the TTY).  K_RAW happens to be 0 in the
         * Linux convention, so leaving this zero-initialized would
         * collide with an explicit KDSKBMODE(K_RAW) from userland
         * and break the gate that decides "feed the TTY or not". */
        vt_states[i].kbd_mode = K_XLATE;
        vt_states[i].cursor_key_app = 0;   /* Normal cursor keys */
        vt_states[i].origin_mode = 0;      /* Absolute origin */
        vt_states[i].bracketed_paste = 0;
        vt_states[i].alt_screen_active = 0;
        vt_states[i].alt_row = 0;
        vt_states[i].alt_col = 0;
        vt_states[i].alt_color = 0x07;
        vt_states[i].alt_attrs = 0;
        memset(vt_states[i].alt_buffer, 0, sizeof(vt_states[i].alt_buffer));
        
        // Initialize ansi state
        ansi_init(&vt_states[i].ansi);
        
        // Clear buffer
        for (int j = 0; j < VT_MAX_BUF_SIZE; j++) {
            vt_states[i].buffer[j] = 0x0720; // Space with default attr
        }
        vt_apply_geometry_to_state(&vt_states[i]);
    }
    
    // VT 0 is active by default. 
    // We assume the bootloader/kernel already cleared screen or we just inherit.
    // For consistency, we might want to clear or sync initial state.
    // But we'll leave it as is for now.
    active_vt = 0;
    vt_initialized = 1;
}

int vt_get_active(void) {
    return active_vt;
}

vt_state_t *vt_get_state(int n) {
    if (n < 0 || n >= VT_MAX) return NULL;
    return &vt_states[n];
}

struct tty *vt_get_active_tty(void) {
    vt_state_t *vt = vt_get_state(active_vt);
    if (!vt) {
        return NULL;
    }
    return vt->tty;
}

void vt_activate(int n) {
    vt_state_t *old_vt;
    vt_state_t *vt;
    uint32_t flags;

    if (n < 0 || n >= VT_MAX) return;

    flags = intr_disable();
    spinlock_acquire(&vt_switch_lock);

    /* Re-check inside the critical section: another CPU may have
     * already activated this VT. */
    if (n == active_vt) {
        spinlock_release(&vt_switch_lock);
        intr_restore(flags);
        return;
    }

    old_vt = &vt_states[active_vt];
    old_vt->led_state = keyboard_get_led_state();

    active_vt = n;
    vt = &vt_states[n];
    keyboard_set_led_state(vt->led_state);
    if (vt->tty) {
        console_set_tty(vt->tty);
    }

    spinlock_release(&vt_switch_lock);
    intr_restore(flags);

    /* Redraw outside the lock — fb_console takes its own lock and we
     * don't want to nest console locks under vt_switch_lock. */
    vt_redraw_active();
}

int vt_get_scrollback_view(const vt_state_t *vt) {
    if (!vt) {
        return 0;
    }
    return vt->scrollback_view;
}

uint16_t vt_get_display_cell(const vt_state_t *vt, int row, int col) {
    int visible_rows;
    int history_count;
    int view;
    int display_start;
    int seq_index;
    size_t slot;

    if (!vt || row < 0 || col < 0 || row >= vt_get_visible_height() || col >= vt_get_width()) {
        return 0x0720;
    }

    visible_rows = vt_get_visible_height();
    history_count = vt->scrollback_count;
    view = vt->scrollback_view;
    if (view > history_count) {
        view = history_count;
    }

    display_start = history_count - view;
    seq_index = display_start + row;
    if (seq_index < history_count) {
        slot = vt_scrollback_slot(vt, (size_t)seq_index);
        return vt->scrollback[slot][col];
    }

    seq_index -= history_count;
    if (seq_index >= visible_rows) {
        return 0x0720;
    }

    return vt->buffer[vt_row_offset((size_t)seq_index) + (size_t)col];
}

void vt_capture_scrollback_top(vt_state_t *vt) {
    size_t row_offset;
    size_t width;

    if (!vt) {
        return;
    }

    width = (size_t)vt_get_width();
    row_offset = vt_row_offset(0);
    memcpy(vt->scrollback[vt->scrollback_head], &vt->buffer[row_offset],
           width * sizeof(uint16_t));
    vt->scrollback_head = (uint16_t)((vt->scrollback_head + 1) % VT_SCROLLBACK_LINES);
    if (vt->scrollback_count < VT_SCROLLBACK_LINES) {
        vt->scrollback_count++;
    }
}

void vt_scrollback_page_up(void) {
    vt_state_t *vt;
    int step;
    int new_view;

    vt = vt_get_state(vt_get_active());
    if (!vt || vt->scrollback_count == 0) {
        return;
    }

    step = vt_get_visible_height() - 1;
    if (step < 1) {
        step = 1;
    }
    new_view = vt->scrollback_view + step;
    if (new_view > vt->scrollback_count) {
        new_view = vt->scrollback_count;
    }
    if (new_view == vt->scrollback_view) {
        return;
    }
    vt->scrollback_view = (uint16_t)new_view;
    vt_redraw_active();
}

void vt_scrollback_page_down(void) {
    vt_state_t *vt;
    int step;
    int new_view;

    vt = vt_get_state(vt_get_active());
    if (!vt || vt->scrollback_view == 0) {
        return;
    }

    step = vt_get_visible_height() - 1;
    if (step < 1) {
        step = 1;
    }
    new_view = vt->scrollback_view - step;
    if (new_view < 0) {
        new_view = 0;
    }
    if (new_view == vt->scrollback_view) {
        return;
    }
    vt->scrollback_view = (uint16_t)new_view;
    vt_redraw_active();
}

void vt_scrollback_line_up(void) {
    vt_state_t *vt;
    int new_view;

    vt = vt_get_state(vt_get_active());
    if (!vt || vt->scrollback_count == 0) {
        return;
    }

    new_view = vt->scrollback_view + 1;
    if (new_view > vt->scrollback_count) {
        new_view = vt->scrollback_count;
    }
    if (new_view == vt->scrollback_view) {
        return;
    }
    vt->scrollback_view = (uint16_t)new_view;
    vt_redraw_active();
}

void vt_scrollback_line_down(void) {
    vt_state_t *vt;

    vt = vt_get_state(vt_get_active());
    if (!vt || vt->scrollback_view == 0) {
        return;
    }

    vt->scrollback_view--;
    vt_redraw_active();
}

static void vt_civil_from_days(int64_t z, int *year, unsigned *month, unsigned *day) {
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

static void vt_format_iso8601(char *buf, size_t buf_len, time_t when) {
    int year;
    unsigned month;
    unsigned day;
    uint64_t secs;
    uint64_t days;
    uint64_t sod;
    unsigned hour;
    unsigned minute;
    unsigned second;

    if (!buf || buf_len == 0) return;
    if (when < 0) when = 0;
    
    secs = (uint64_t)when;
    days = secs / 86400ULL;
    sod = secs % 86400ULL;
    hour = (unsigned)(sod / 3600ULL);
    minute = (unsigned)((sod % 3600ULL) / 60ULL);
    second = (unsigned)(sod % 60ULL);
    vt_civil_from_days((int64_t)days, &year, &month, &day);

    snprintf(buf, buf_len, "%04d-%02u-%02uT%02u:%02u:%02uZ",
             year, month, day, hour, minute, second);
}

static time_t vt_status_time(void) {
    int64_t now = rtc_read_time();
    if (now > 0) return (time_t)now;
    return get_time();
}

static void vt_compose_statusline(vt_state_t *vt) {
    char left[32];
    char right[32];
    char line[VT_MAX_WIDTH];
    size_t left_len;
    size_t right_len;
    int cols;
    int rows;
    int row;
    uint16_t status_color = 0x70;
    int x;

    if (!vt) {
        return;
    }

    vt_get_terminal_geometry(vt, &cols, &rows);
    if (cols < 1 || cols > vt_get_width()) {
        cols = vt_get_width();
    }
    if (rows < 2 || rows > vt_get_height()) {
        rows = vt_get_height();
    }

    if (vt_get_scrollback_view(vt) != 0) {
        snprintf(left, sizeof(left), " VT%d +%u ", vt->id + 1, vt->scrollback_view);
    } else {
        snprintf(left, sizeof(left), " VT%d ", vt->id + 1);
    }
    vt_format_iso8601(right, sizeof(right), vt_status_time());
    left_len = strlen(left);
    right_len = strlen(right);

    memset(line, ' ', sizeof(line));

    for (x = 0; x < cols && (size_t)x < left_len; x++) {
        line[x] = left[x];
    }

    if ((int)right_len < cols) {
        size_t start = (size_t)(cols - (int)right_len);
        for (x = 0; x < (int)right_len; x++) {
            line[start + (size_t)x] = right[x];
        }
    }

    row = rows - 1;
    for (x = 0; x < cols; x++) {
        size_t index = (size_t)row * (size_t)vt_get_width() + (size_t)x;
        vt->buffer[index] = (uint16_t)((unsigned char)line[x]) | (status_color << 8);
    }

}

void vt_render_statusline(vt_state_t *vt) {
    if (!vt) {
        return;
    }

    /* In KD_GRAPHICS mode an X server owns the framebuffer; the
     * status bar would paint over its output.  Skip silently. */
    if (vt->graphics_mode) {
        return;
    }

    vt_compose_statusline(vt);

    if (vt->id == vt_get_active()) {
        if (fb_console_active()) {
            fb_console_refresh_statusline();
        } else {
            hw_text_refresh_statusline();
        }
    }
}

void vt_redraw_active(void) {
    vt_state_t *vt = vt_get_state(vt_get_active());

    if (!vt) {
        return;
    }

    /* Same gate as vt_render_statusline — don't draw cells over the
     * X server's framebuffer output. */
    if (vt->graphics_mode) {
        return;
    }

    vt_compose_statusline(vt);
    if (fb_console_active()) {
        fb_console_redraw_active();
    } else {
        hw_text_redraw_active();
    }
}

void vt_tick_1hz(void) {
    vt_render_statusline(vt_get_state(vt_get_active()));
}

/*
 * Process-exit cleanup hook.  Called from proc_exit before any of the
 * exiting process's fds are closed.  If the process owns a VT in
 * KD_GRAPHICS mode (i.e. it was the X server that called KDSETMODE),
 * we tear the graphics state down so the console returns to text mode
 * and the keyboard returns to K_XLATE — regardless of whether other
 * processes still hold open file descriptors on the VT.
 *
 * Without this, a SEGV'd X server leaves the framebuffer wedged in
 * graphics mode and the keyboard in K_RAW: tty1 looks black and won't
 * receive any keyboard input, even though sibling shells on other VTs
 * are perfectly alive.
 */
void vt_release_graphics_on_exit(void *exiting_process) {
    if (!exiting_process) return;
    for (int i = 0; i < VT_MAX; i++) {
        vt_state_t *vt = &vt_states[i];
        int touched = 0;

        if (vt->graphics_mode && vt->graphics_owner == exiting_process) {
            vt->graphics_mode = 0;
            vt->graphics_owner = NULL;
            touched = 1;
        }
        /* Restore K_XLATE independently of graphics_mode: a clean
         * Xfbdev exit (zap) calls LinuxDisable which clears
         * KD_GRAPHICS but DOESN'T touch KDSKBMODE, leaving the VT
         * stuck in K_RAW with all keystrokes routed to /dev/input/
         * event0 instead of the line discipline. */
        if (vt->kbd_mode != K_XLATE && vt->kbd_owner == exiting_process) {
            vt->kbd_mode = K_XLATE;
            vt->kbd_owner = NULL;
            touched = 1;
        }

        if (touched && vt->id == vt_get_active()) {
            if (fb_console_active()) {
                fb_console_redraw_active();
            } else {
                hw_text_redraw_active();
            }
            vt_render_statusline(vt);
        }
    }
}
