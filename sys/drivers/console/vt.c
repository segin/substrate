/*
 * sys/drivers/console/vt.c - Virtual Terminal Logic
 */

#include <stdio.h>
#include <string.h>

#include <arch/i386/intr.h>
#include <arch/x86-common/rtc.h>
#include <drivers/input/keyboard.h>
#include <drivers/video/fb.h>
#include <drivers/video/fb_console.h>
#include <drivers/video/hw_text.h>
#include <kern/console.h>
#include <kern/time.h>
#include <sys/lock.h>
#include <sys/smp.h>
#include <sys/tty.h>
#include <sys/vt.h>
#include <sys/vtio.h>
#include <vm/vm_kmem.h>

static vt_state_t vt_states[VT_MAX];
static int active_vt = 0;
static int vt_width = VT_DEFAULT_WIDTH;
static int vt_height = VT_DEFAULT_HEIGHT;
static int vt_initialized = 0;

/* Per-VT cell buffers are allocated to the live geometry, not statically sized
 * to VT_MAX_*.  These helpers keep the buffer/alt_buffer/scrollback trio in
 * lock-step. */
static size_t vt_buf_bytes(int cols, int rows) {
    return (size_t)cols * (size_t)rows * sizeof(uint16_t);
}
static size_t vt_sb_bytes(int cols) {
    return (size_t)VT_SCROLLBACK_LINES * (size_t)cols * sizeof(uint16_t);
}

/* Allocate the trio for a (cols x rows) geometry, pre-filled with `fill`.
 * All-or-nothing — frees partials and returns -1 on OOM. */
static int vt_alloc_buffers(int cols, int rows, uint16_t fill,
                            uint16_t **out_buf, uint16_t **out_alt,
                            uint16_t **out_sb) {
    size_t cells = (size_t)cols * (size_t)rows;
    size_t sb_cells = (size_t)VT_SCROLLBACK_LINES * (size_t)cols;
    uint16_t *buf = kmalloc(cells * sizeof(uint16_t));
    uint16_t *alt = kmalloc(cells * sizeof(uint16_t));
    uint16_t *sb  = kmalloc(sb_cells * sizeof(uint16_t));
    size_t i;

    if (!buf || !alt || !sb) {
        if (buf) kfree(buf, cells * sizeof(uint16_t));
        if (alt) kfree(alt, cells * sizeof(uint16_t));
        if (sb)  kfree(sb, sb_cells * sizeof(uint16_t));
        return -1;
    }
    for (i = 0; i < cells; i++) { buf[i] = fill; alt[i] = fill; }
    for (i = 0; i < sb_cells; i++) sb[i] = fill;
    *out_buf = buf;
    *out_alt = alt;
    *out_sb = sb;
    return 0;
}

static void vt_free_buffers(vt_state_t *vt, int cols, int rows) {
    if (!vt) return;
    if (vt->buffer)    kfree(vt->buffer, vt_buf_bytes(cols, rows));
    if (vt->alt_buffer) kfree(vt->alt_buffer, vt_buf_bytes(cols, rows));
    if (vt->scrollback) kfree(vt->scrollback, vt_sb_bytes(vt->scrollback_pitch));
    vt->buffer = NULL;
    vt->alt_buffer = NULL;
    vt->scrollback = NULL;
}

/*
 * Serializes vt_activate() so a switch can't race with concurrent
 * console writes to the previously-active VT.  Held only briefly
 * around the active_vt flip + LED/console handoff; per-VT cell state
 * remains protected by the framebuffer/console layer's own locks.
 */
static spinlock_t vt_switch_lock = SPINLOCK_INIT("vt_switch");

/*
 * Scrollback state lock.
 *
 * The scrollback ring (head/count/view) and the redraw that walks it are
 * reached from two contexts that genuinely run concurrently:
 *
 *   - console output.  Every kprintf and every tty write scrolls the active
 *     VT, pushing lines into the ring and moving head/count.
 *   - the keyboard.  Shift+PgUp/PgDn and Shift+Up/Down move `view` and force
 *     a full redraw, arriving either from the PS/2 IRQ or -- on a machine
 *     whose keyboard is USB -- from the USB-HID poll kthread.
 *
 * keyboard.c already serializes the modifier bits against exactly this pair
 * of contexts (kbd_mod_lock, DRV-22), but the state behind them was left
 * unguarded: process_keycode() runs straight on into vt_scrollback_*() and
 * vt_redraw_active(), which touch far more.  An interleaved update tears the
 * ring indices apart, which is how console output ends up spliced together
 * mid-line and how the renderer walks a half-updated view.
 *
 * Recursive by construction, and it has to be: vt_redraw_active() calls into
 * the framebuffer/hw-text renderer, which calls back out to
 * vt_get_display_cell() and vt_get_scrollback_view() for every cell.  Those
 * accessors are also entry points in their own right (the write path reaches
 * them without going through a redraw), so they cannot simply be left bare --
 * and a plain spinlock_acquire would trip its own recursive-acquire panic on
 * the way back in.  Ownership is tracked by CPU id, which is what makes the
 * depth check safe to read before acquiring: only the owning CPU can ever see
 * its own id there.
 *
 * IRQ-safe: the PS/2 path really does call in from hard IRQ context, so the
 * whole critical section runs with interrupts disabled.
 */
static spinlock_t vt_state_lock = SPINLOCK_INIT("vt_state");
static int        vt_lock_cpu = -1;     /* owning CPU, -1 when free */
static unsigned   vt_lock_depth;        /* recursion count, owner-only */
static uint32_t   vt_lock_flags;        /* saved EFLAGS of the outermost */

static void vt_state_lock_acquire(void) {
    uint32_t flags = intr_disable();
    int cpu = smp_get_cpu_id();

    if (vt_lock_cpu == cpu) {
        /* Already ours; interrupts are off from the outermost acquire. */
        vt_lock_depth++;
        return;
    }
    spinlock_acquire(&vt_state_lock);
    vt_lock_cpu = cpu;
    vt_lock_depth = 1;
    vt_lock_flags = flags;
}

static void vt_state_lock_release(void) {
    if (vt_lock_depth == 0)
        return;                         /* unbalanced; nothing to undo */
    if (--vt_lock_depth == 0) {
        uint32_t flags = vt_lock_flags;
        vt_lock_cpu = -1;               /* clear before releasing */
        spinlock_release(&vt_state_lock);
        intr_restore(flags);
    }
}

void vt_redraw_active(void);
static void vt_redraw_active_locked(void);

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

/* Copy the overlapping region of the old buffer into the new one.  `dst` is
 * already blank-filled by vt_alloc_buffers(), so only surviving cells move. */
static void vt_reflow_buffer(uint16_t *dst, const uint16_t *src,
                             int old_width, int old_height,
                             int new_width, int new_height) {
    int copy_rows = old_height < new_height ? old_height : new_height;
    int copy_cols = old_width < new_width ? old_width : new_width;
    int row;

    for (row = 0; row < copy_rows; row++) {
        memcpy(&dst[(size_t)row * (size_t)new_width],
               &src[(size_t)row * (size_t)old_width],
               (size_t)copy_cols * sizeof(uint16_t));
    }
}

/* Copy each scrollback line's surviving columns into the new ring.  `dst` is
 * blank-filled; `src` has `src_pitch` columns per line. */
static void vt_reflow_scrollback(uint16_t *dst, int dst_pitch,
                                 const uint16_t *src, int src_pitch,
                                 int old_width, int new_width) {
    int copy_cols = old_width < new_width ? old_width : new_width;
    int i;

    for (i = 0; i < VT_SCROLLBACK_LINES; i++) {
        memcpy(&dst[(size_t)i * (size_t)dst_pitch],
               &src[(size_t)i * (size_t)src_pitch],
               (size_t)copy_cols * sizeof(uint16_t));
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

/* Re-clamp cursor / scroll-region state after a geometry change.  The buffers
 * have already been reallocated and reflowed by vt_set_geometry(). */
static void vt_reconfigure_state(vt_state_t *vt,
                                 int old_height,
                                 int new_width,
                                 int new_height) {
    int old_visible;
    int new_visible;

    if (!vt) {
        return;
    }

    old_visible = old_height - 1;
    new_visible = new_height - 1;

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
    uint16_t blank = (uint16_t)' ' | (uint16_t)0x07U << 8;
    uint16_t *nbuf[VT_MAX];
    uint16_t *nalt[VT_MAX];
    uint16_t *nsb[VT_MAX];
    int old_width;
    int old_height;
    int i;

    if (cols < 1 || cols > VT_MAX_WIDTH || rows < 2 || rows > VT_MAX_HEIGHT) {
        return -1;
    }

    /* Before vt_init() the cell buffers don't exist yet — just record the
     * geometry; vt_init() allocates the buffers for it. */
    if (!vt_initialized) {
        vt_width = cols;
        vt_height = rows;
        return 0;
    }

    if (cols == vt_width && rows == vt_height) {
        return 0;
    }

    old_width = vt_width;
    old_height = vt_height;

    /* Allocate every VT's new buffers up front so a mid-resize OOM leaves the
     * existing geometry intact rather than half-resized. */
    for (i = 0; i < VT_MAX; i++) {
        if (vt_alloc_buffers(cols, rows, blank, &nbuf[i], &nalt[i], &nsb[i]) != 0) {
            for (int k = 0; k < i; k++) {
                kfree(nbuf[k], vt_buf_bytes(cols, rows));
                kfree(nalt[k], vt_buf_bytes(cols, rows));
                kfree(nsb[k], vt_sb_bytes(cols));
            }
            return -1;
        }
    }

    /* Reflow surviving content into the new buffers, drop the old, swap in the
     * new.  (The status row stays blank from the allocation.) */
    for (i = 0; i < VT_MAX; i++) {
        vt_state_t *vt = &vt_states[i];

        if (vt->buffer) {
            vt_reflow_buffer(nbuf[i], vt->buffer, old_width, old_height - 1,
                             cols, rows - 1);
            vt_reflow_buffer(nalt[i], vt->alt_buffer, old_width, old_height - 1,
                             cols, rows - 1);
            vt_reflow_scrollback(nsb[i], cols, vt->scrollback,
                                 vt->scrollback_pitch, old_width, cols);
            vt_free_buffers(vt, old_width, old_height);
        }
        vt->buffer = nbuf[i];
        vt->alt_buffer = nalt[i];
        vt->scrollback = nsb[i];
        vt->scrollback_pitch = cols;
    }

    vt_width = cols;
    vt_height = rows;

    for (i = 0; i < VT_MAX; i++) {
        vt_reconfigure_state(&vt_states[i], old_height, cols, rows);
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

        /* Allocate the cell buffers for the current geometry (default 80x25,
         * or whatever vt_set_geometry() recorded before init).  Pre-filled
         * with blank, so no separate clear loop is needed.  Early-boot kmalloc
         * (kmem_init runs long before the console) does not fail in practice. */
        vt_states[i].scrollback_pitch = vt_width;
        vt_states[i].buffer = NULL;
        vt_states[i].alt_buffer = NULL;
        vt_states[i].scrollback = NULL;
        (void)vt_alloc_buffers(vt_width, vt_height, 0x0720,
                               &vt_states[i].buffer, &vt_states[i].alt_buffer,
                               &vt_states[i].scrollback);

        // Initialize ansi state
        ansi_init(&vt_states[i].ansi);

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

    /* Framebuffer ownership: when leaving a graphics VT, push the X server
     * offscreen (its /dev/fb0 mmap is redirected to a shadow buffer) so its
     * drawing can't bleed onto the now-foreground text console; when
     * entering a graphics VT, bring it back onscreen and present its last
     * rendered frame.  No-op when no X framebuffer mapping is registered.
     * Done before the redraw below so the text console owns the screen. */
    {
        vt_state_t *new_vt = vt_get_state(n);
        fb_set_offscreen(new_vt && new_vt->graphics_mode ? 0 : 1);
    }

    /* Redraw outside the lock — fb_console takes its own lock and we
     * don't want to nest console locks under vt_switch_lock. */
    vt_redraw_active();
}

static int vt_get_scrollback_view_locked(const vt_state_t *vt) {
    if (!vt) {
        return 0;
    }
    return vt->scrollback_view;
}

static uint16_t vt_get_display_cell_locked(const vt_state_t *vt, int row, int col) {
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
        return vt->scrollback[slot * (size_t)vt->scrollback_pitch + (size_t)col];
    }

    seq_index -= history_count;
    if (seq_index >= visible_rows) {
        return 0x0720;
    }

    return vt->buffer[vt_row_offset((size_t)seq_index) + (size_t)col];
}

static void vt_capture_scrollback_top_locked(vt_state_t *vt) {
    size_t row_offset;
    size_t width;

    if (!vt) {
        return;
    }

    width = (size_t)vt_get_width();
    row_offset = vt_row_offset(0);
    memcpy(&vt->scrollback[(size_t)vt->scrollback_head * (size_t)vt->scrollback_pitch],
           &vt->buffer[row_offset], width * sizeof(uint16_t));
    vt->scrollback_head = (uint16_t)((vt->scrollback_head + 1) % VT_SCROLLBACK_LINES);
    if (vt->scrollback_count < VT_SCROLLBACK_LINES) {
        vt->scrollback_count++;
    }
}

static void vt_scrollback_page_up_locked(void) {
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
    vt_redraw_active_locked();
}

static void vt_scrollback_page_down_locked(void) {
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
    vt_redraw_active_locked();
}

static void vt_scrollback_line_up_locked(void) {
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
    vt_redraw_active_locked();
}

static void vt_scrollback_line_down_locked(void) {
    vt_state_t *vt;

    vt = vt_get_state(vt_get_active());
    if (!vt || vt->scrollback_view == 0) {
        return;
    }

    vt->scrollback_view--;
    vt_redraw_active_locked();
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

static void vt_redraw_active_locked(void) {
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

/*
 * Public entry points.  Each takes the scrollback state lock and defers to the
 * _locked body above; the lock is recursive, so a redraw that re-enters
 * vt_get_display_cell()/vt_get_scrollback_view() from the renderer is fine.
 */
int vt_get_scrollback_view(const vt_state_t *vt) {
    int r;
    vt_state_lock_acquire();
    r = vt_get_scrollback_view_locked(vt);
    vt_state_lock_release();
    return r;
}

uint16_t vt_get_display_cell(const vt_state_t *vt, int row, int col) {
    uint16_t r;
    vt_state_lock_acquire();
    r = vt_get_display_cell_locked(vt, row, col);
    vt_state_lock_release();
    return r;
}

void vt_capture_scrollback_top(vt_state_t *vt) {
    vt_state_lock_acquire();
    vt_capture_scrollback_top_locked(vt);
    vt_state_lock_release();
}

void vt_scrollback_page_up(void) {
    vt_state_lock_acquire();
    vt_scrollback_page_up_locked();
    vt_state_lock_release();
}

void vt_scrollback_page_down(void) {
    vt_state_lock_acquire();
    vt_scrollback_page_down_locked();
    vt_state_lock_release();
}

void vt_scrollback_line_up(void) {
    vt_state_lock_acquire();
    vt_scrollback_line_up_locked();
    vt_state_lock_release();
}

void vt_scrollback_line_down(void) {
    vt_state_lock_acquire();
    vt_scrollback_line_down_locked();
    vt_state_lock_release();
}

void vt_redraw_active(void) {
    vt_state_lock_acquire();
    vt_redraw_active_locked();
    vt_state_lock_release();
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

    /* Drop any framebuffer-client registration this process held so a
     * later VT switch never touches its torn-down address space, and the
     * framebuffer reverts to onscreen for the text console. */
    fb_client_clear(exiting_process);

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
