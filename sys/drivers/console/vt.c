/*
 * sys/drivers/console/vt.c - Virtual Terminal Logic
 */

#include <sys/vt.h>
#include <drivers/video/hw_text.h>
#include <kern/console.h>
#include <string.h>
#include <sys/tty.h>

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
        vt_states[i].tty = NULL;
        vt_states[i].scrollback_head = 0;
        vt_states[i].scrollback_count = 0;
        vt_states[i].scrollback_view = 0;
        vt_states[i].saved_row = 0;
        vt_states[i].saved_col = 0;
        vt_states[i].saved_color = 0x07;
        vt_states[i].scroll_top = 0;
        vt_states[i].scroll_bottom = VT_DEFAULT_HEIGHT - 2; /* visible rows - 1 */
        vt_states[i].cursor_visible = 1;
        vt_states[i].autowrap = 1;         /* DECAWM on by default */
        vt_states[i].cursor_key_app = 0;   /* Normal cursor keys */
        vt_states[i].origin_mode = 0;      /* Absolute origin */
        vt_states[i].bracketed_paste = 0;
        vt_states[i].alt_screen_active = 0;
        vt_states[i].alt_row = 0;
        vt_states[i].alt_col = 0;
        vt_states[i].alt_color = 0x07;
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
    vt_state_t *vt;

    if (n < 0 || n >= VT_MAX) return;
    if (n == active_vt) return;

    active_vt = n;
    vt = &vt_states[n];
    if (vt->tty) {
        console_set_tty(vt->tty);
    }
    hw_text_redraw_active();
    hw_text_refresh_statusline();
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
