/*
 * sys/drivers/console/vt.c - Virtual Terminal Logic
 */

#include <sys/vt.h>
#include <drivers/video/hw_text.h>
#include <drivers/video/fb_console.h>
#include <drivers/video/fb_console.h>
#include <drivers/input/keyboard.h>
#include <kern/console.h>
#include <string.h>
#include <sys/tty.h>
#include <kern/sched.h>
#include <kern/time.h>
#include <arch/x86-common/rtc.h>
#include <stdio.h>
#include <sys/kthread.h>

static vt_state_t vt_states[VT_MAX];
static int active_vt = 0;
static int vt_width = VT_DEFAULT_WIDTH;
static int vt_height = VT_DEFAULT_HEIGHT;

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

    memset(vt->tab_stops, 0, sizeof(vt->tab_stops));
    for (col = 8; col < width; col += 8) {
        vt->tab_stops[col / 32] |= (uint32_t)1U << (col % 32);
    }
}

int vt_set_geometry(int cols, int rows) {
    if (cols < 1 || cols > VT_MAX_WIDTH || rows < 2 || rows > VT_MAX_HEIGHT) {
        return -1;
    }

    vt_width = cols;
    vt_height = rows;
    return 0;
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
        vt_init_tab_stops(&vt_states[i], vt_width);
        vt_states[i].autowrap = 1;         /* DECAWM on by default */
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
    }
    
    // VT 0 is active by default. 
    // We assume the bootloader/kernel already cleared screen or we just inherit.
    // For consistency, we might want to clear or sync initial state.
    // But we'll leave it as is for now.
    active_vt = 0;
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

    if (n < 0 || n >= VT_MAX) return;
    if (n == active_vt) return;

    old_vt = &vt_states[active_vt];
    old_vt->led_state = keyboard_get_led_state();

    active_vt = n;
    vt = &vt_states[n];
    keyboard_set_led_state(vt->led_state);
    if (vt->tty) {
        console_set_tty(vt->tty);
    }
    hw_text_redraw_active();
    vt_render_statusline(vt);
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
    hw_text_redraw_active();
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
    hw_text_redraw_active();
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
    hw_text_redraw_active();
}

void vt_scrollback_line_down(void) {
    vt_state_t *vt;

    vt = vt_get_state(vt_get_active());
    if (!vt || vt->scrollback_view == 0) {
        return;
    }

    vt->scrollback_view--;
    hw_text_redraw_active();
}

static volatile uint32_t vt_status_epoch = 0;
static int vt_status_thread_started = 0;

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

void vt_render_statusline(vt_state_t *vt) {
    char left[32];
    char right[32];
    char line[VT_MAX_WIDTH];
    size_t left_len;
    size_t right_len;
    int cols;
    int row;
    int x;

    if (!vt) return;

    cols = vt_get_width();
    row = vt_get_status_row();
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

    /* 0x70 = Light Grey on pure grey/white background (STATUS COLOR) */
    uint16_t status_color = 0x70;

    for (x = 0; x < cols; x++) {
        size_t index = (size_t)row * (size_t)cols + (size_t)x;
        uint16_t entry = (uint16_t)((unsigned char)line[x]) | (status_color << 8);

        if (vt->buffer[index] != entry) {
            vt->buffer[index] = entry;
            if (vt->id == vt_get_active()) {
                /* We notify both backend drivers that the cell updated. Only the active driver draws. */
#ifdef _SYS_DRIVERS_VIDEO_HW_TEXT_H
                hw_text_console_write_shim_cell((size_t)x, (size_t)row, entry); /* Stub or we rely on full line sync */
#endif
            }
        }
    }
    
    /* Since we updated the vt->buffer directly above, we trigger a row resync */
    if (vt->id == vt_get_active()) {
        /* Call out to hardcoded drivers to refresh row if applicable. NOTE: fb_console might use its own redraw queue */
        hw_text_redraw_active(); /* this redrawing the whole screen isn't ideal but we can optimize later with row syncs */
#ifdef _SYS_DRIVERS_VIDEO_FB_CONSOLE_H
        fb_console_sync_row(vt, row);
#endif
    }
}

static void vt_statusline_task(void *arg) {
    uint32_t seen = 0;
    (void)arg;

    for (;;) {
        while (seen == vt_status_epoch) {
            sched_sleep((void *)&vt_status_epoch);
        }
        seen = vt_status_epoch;
        vt_state_t *vt = vt_get_state(vt_get_active());
        vt_render_statusline(vt);
    }
}

void vt_tick_1hz(void) {
    if (!vt_status_thread_started) {
        thread_t *td;
        if (kthread_create(vt_statusline_task, NULL, &td, "vtstatus") == 0) {
            vt_status_thread_started = 1;
        } else {
            /* Fallback to inline processing if kthread fails */
            vt_state_t *vt = vt_get_state(vt_get_active());
            vt_render_statusline(vt);
            return;
        }
    }

    vt_status_epoch++;
    sched_wakeup((void *)&vt_status_epoch);
}
