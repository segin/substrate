/*
 * sys/sys/vt.h - Virtual Terminal Subsystem
 */

#ifndef _SYS_VT_H
#define _SYS_VT_H

#include <stdint.h>
#include <kern/ansi_handler.h>
struct tty;

#define VT_MAX 12
#define VT_DEFAULT_WIDTH 80
#define VT_DEFAULT_HEIGHT 25
#define VT_MAX_WIDTH 132
#define VT_MAX_HEIGHT 60
#define VT_MAX_BUF_SIZE (VT_MAX_WIDTH * VT_MAX_HEIGHT)
#define VT_SCROLLBACK_LINES 256
#define VT_TABSTOP_WORDS ((VT_MAX_WIDTH + 31) / 32)

/*
 * VT State
 * Stores the state of a virtual terminal when it is not active (not displayed).
 */
typedef struct vt_state {
    int id; // 0..VT_MAX-1
    
    // Video Memory Buffer (Char + Attribute)
    uint16_t buffer[VT_MAX_BUF_SIZE];
    uint16_t scrollback[VT_SCROLLBACK_LINES][VT_MAX_WIDTH];
    uint16_t scrollback_head;
    uint16_t scrollback_count;
    uint16_t scrollback_view;
    
    // Cursor State
    int row;
    int col;
    uint8_t color; // Current attribute
    uint16_t attrs;
    
    // Saved Cursor (DECSC/DECRC / CSI s/u)
    int saved_row;
    int saved_col;
    uint8_t saved_color;
    uint16_t saved_attrs;
    
    // Scroll Region (DECSTBM)
    int scroll_top;    // 0-based inclusive
    int scroll_bottom; // 0-based inclusive (default: height-1)
    
    // Cursor visibility
    int cursor_visible; // 1=visible (default), 0=hidden
    int cursor_blink;   // 1=blinking (default), 0=steady
    uint8_t tab_width; // Horizontal tab stop width in columns
    uint32_t tab_stops[VT_TABSTOP_WORDS];
    
    // DEC modes
    int autowrap;       // DECAWM: 1=wrap at right margin (default), 0=clamp
    int cursor_key_app; // DECCKM: 0=normal (default), 1=application mode
    int origin_mode;    // DECOM: 0=absolute (default), 1=relative to scroll region
    int bracketed_paste;// DECSET 2004: 0=off (default), 1=on
    /*
     * Deferred-wrap flag for the `xenl` ("newline ignored after
     * wrap") terminfo capability — required by every modern
     * terminfo entry substrate ships (linux, xterm, ansi, ...).
     * After printing the Nth (= width-th) character of a row the
     * cursor doesn't immediately advance to the next row; instead
     * this flag is set, and the actual wrap happens only on the
     * NEXT printable character.  Carriage return / explicit
     * cursor movement clears the flag without wrapping.  Without
     * this, zsh's PROMPT_SP heuristic mis-renders (the marker
     * stays visible because zsh's `\r + space + \r` overwrite
     * sequence lands on the wrong row).
     */
    int pending_wrap;
    
    // Alternate screen buffer (DECSET 47/1047/1049)
    int alt_screen_active;
    uint16_t alt_buffer[VT_MAX_BUF_SIZE];
    int alt_row;
    int alt_col;
    uint8_t alt_color;
    uint16_t alt_attrs;
    
    // ANSI Parser State
    struct ansi_ctx ansi;
    
    // Keyboard LED state for this VT (Scroll Lock, Num Lock, Caps Lock)
    uint8_t led_state;

    /*
     * KD_TEXT (0) or KD_GRAPHICS (1).  Set by an X server via the
     * KDSETMODE ioctl when it wants to claim the framebuffer.
     * While in KD_GRAPHICS:
     *   - the FB console backend silently drops writes (no painted
     *     text over X's pixel output)
     *   - the status-bar tick skips its render
     *   - the keyboard line-discipline drops translated chars
     *     (only /dev/input/event0 sees them) unless kbd_mode is
     *     also reset to K_XLATE
     */
    int graphics_mode;

    /*
     * Keyboard processing mode.  K_XLATE (default) = cooked chars
     * into the TTY; anything else suppresses TTY input so only
     * evdev (/dev/input/event0) consumers see the events.
     */
    int kbd_mode;

    /*
     * Process that set this VT into KD_GRAPHICS (typically the X
     * server's PID).  Kept as `void *` to avoid leaking the kernel
     * process_t into this header; the fb_console code casts.  When
     * a process exits, the proc_exit hook walks every VT, and if
     * graphics_owner matches the exiting process, force-restores
     * KD_TEXT — otherwise an X server crash leaves the framebuffer
     * wedged in graphics mode forever.
     */
    void *graphics_owner;

    /*
     * Process that set this VT into a non-K_XLATE kbd_mode (X server
     * calling KDSKBMODE(K_RAW), etc.).  Tracked separately from
     * graphics_owner because the X server clears KD_GRAPHICS at
     * teardown (via KDSETMODE(KD_TEXT)) but DOESN'T restore K_XLATE
     * — leaving tty1 stuck in K_RAW with no way for the line
     * discipline to see keystrokes.  proc_exit's cleanup hook
     * restores K_XLATE for every VT whose kbd_owner is the exiting
     * process.
     */
    void *kbd_owner;

    // Associated TTY (if any)
    struct tty *tty;
} vt_state_t;

// API
void vt_init(void);
void vt_activate(int n);
int vt_get_active(void);
vt_state_t *vt_get_state(int n);

/* Restore KD_TEXT + K_XLATE on every VT owned by the exiting process.
 * Called from proc_exit so a SEGV'd X server doesn't leave the
 * framebuffer wedged in graphics mode and the keyboard in K_RAW. */
void vt_release_graphics_on_exit(void *exiting_process);
struct tty *vt_get_active_tty(void);
int vt_set_geometry(int cols, int rows);
int vt_refresh_geometry_from_terminal(void);
int vt_get_width(void);
int vt_get_height(void);
int vt_get_visible_height(void);
int vt_get_status_row(void);
size_t vt_get_cell_count(void);
int vt_get_scrollback_view(const vt_state_t *vt);
uint16_t vt_get_display_cell(const vt_state_t *vt, int row, int col);
void vt_capture_scrollback_top(vt_state_t *vt);
void vt_scrollback_page_up(void);
void vt_scrollback_page_down(void);
void vt_scrollback_line_up(void);
void vt_scrollback_line_down(void);

// generic tick hooks
void vt_tick_1hz(void);
void vt_render_statusline(vt_state_t *vt);
void vt_redraw_active(void);

#endif /* _SYS_VT_H */
