/*
 * test_vt_wrap.c — host-side torture test for the VT cursor-advance
 * state machine that lives in sys/drivers/video/fb_console.c (fb
 * backend) and sys/drivers/video/hw_text.c (text-mode backend).
 *
 * Both backends share the same `cb_putc` algorithm — character-driven
 * advancement of (col, row) with deferred-wrap (xenl) semantics.  This
 * test re-implements that algorithm in user space against a fixed
 * 2D character grid, exercises a battery of cursor-motion scenarios
 * (including the exact byte stream zsh's PROMPT_SP emits), and reports
 * pass/fail counts.  When this test passes but the kernel still
 * mis-renders, the bug is somewhere outside the cb_putc state machine
 * (most likely in TIOCGWINSZ-reported width, the ANSI escape handler,
 * or terminfo).
 *
 * Build:
 *   cc -std=c99 -Wall -Wextra -o test_vt_wrap test_vt_wrap.c
 * Run:
 *   ./test_vt_wrap
 *
 * Exit code is 0 if every scenario passes, 1 otherwise.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/* Test grid geometry.  80x25 mirrors the substrate default text
 * console and is also the size zsh sees via TIOCGWINSZ unless the
 * user has resized. */
#define W 80
#define H 25

typedef struct {
    int col, row;
    int autowrap;        /* DECAWM */
    int pending_wrap;    /* xenl state */
    int scroll_top;
    int scroll_bottom;
    /* Visible grid.  `.` for unwritten cells so scenarios can assert
     * exact placement without ambiguity against actual space chars. */
    char buf[H][W];
} host_vt_t;

static void store_cell(host_vt_t *vt, int col, int row, char c) {
    if (row >= 0 && row < H && col >= 0 && col < W)
        vt->buf[row][col] = c;
}

static void scroll_up(host_vt_t *vt, int top, int bot, int n) {
    if (top < 0) top = 0;
    if (bot >= H) bot = H - 1;
    int rows = bot - top + 1;
    if (n >= rows) {
        for (int r = top; r <= bot; r++) memset(vt->buf[r], ' ', W);
        return;
    }
    for (int r = top; r + n <= bot; r++)
        memcpy(vt->buf[r], vt->buf[r + n], W);
    for (int r = bot - n + 1; r <= bot; r++)
        memset(vt->buf[r], ' ', W);
}

static int width(void) { return W; }

/*
 * Verbatim copy of fb_console.c:fb_cb_putc (and hw_text.c:cb_putc;
 * the two are identical apart from the per-backend cell-store/scroll
 * primitives).  Only the struct-type name and helper names change.
 * Keep this function in lock-step with the kernel source.
 */
static void cb_putc(host_vt_t *vt, char c) {
    int bottom = vt->scroll_bottom;

    if (c == '\n') {
        vt->col = 0;
        vt->pending_wrap = 0;
        if (vt->row >= bottom) {
            scroll_up(vt, vt->scroll_top, bottom, 1);
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
        if (vt->col > 0) vt->col--;
        vt->pending_wrap = 0;
        return;
    }
    /* \t skipped — not exercised by the scenarios below. */

    if (vt->pending_wrap && vt->autowrap) {
        vt->col = 0;
        if (vt->row >= bottom) {
            scroll_up(vt, vt->scroll_top, bottom, 1);
        } else {
            vt->row++;
        }
        vt->pending_wrap = 0;
    }

    store_cell(vt, vt->col, vt->row, c);
    if (++vt->col >= width()) {
        if (vt->autowrap) {
            vt->col = width() - 1;
            vt->pending_wrap = 1;
        } else {
            vt->col = width() - 1;
        }
    }
}

static void vt_putn(host_vt_t *vt, char c, int n) {
    while (n-- > 0) cb_putc(vt, c);
}

static void vt_init(host_vt_t *vt) {
    memset(vt, 0, sizeof(*vt));
    vt->autowrap = 1;
    vt->scroll_top = 0;
    vt->scroll_bottom = H - 1;
    for (int r = 0; r < H; r++)
        memset(vt->buf[r], '.', W);
}

/* ------------------------------------------------------------------ */
/* Test framework                                                     */
/* ------------------------------------------------------------------ */

static int passed = 0;
static int failed = 0;

#define CHECK(cond, name) do { \
    if (cond) { passed++; printf("[ OK ] %s\n", name); } \
    else      { failed++; printf("[FAIL] %s\n", name); } \
} while (0)

#define CHECK_EQ(actual, expected, name) do { \
    if ((actual) == (expected)) { passed++; printf("[ OK ] %s\n", name); } \
    else { failed++; printf("[FAIL] %s: got=%d want=%d\n", name, (int)(actual), (int)(expected)); } \
} while (0)

static int row_is(const host_vt_t *vt, int row, const char *prefix) {
    size_t n = strlen(prefix);
    if ((int)n > W) return 0;
    return memcmp(vt->buf[row], prefix, n) == 0;
}

/* ------------------------------------------------------------------ */
/* Scenarios                                                          */
/* ------------------------------------------------------------------ */

/*
 * Scenario 1: fill exactly the row width with printables.  After
 * the last char lands in column W-1, the cursor should rest with
 * (col=W-1, pending_wrap=1) — NOT advance to the next row.
 */
static void test_fill_no_advance(void) {
    host_vt_t vt; vt_init(&vt);
    for (int i = 0; i < W; i++) cb_putc(&vt, 'a');
    CHECK_EQ(vt.row, 0,             "fill_no_advance: row stays at 0");
    CHECK_EQ(vt.col, W - 1,         "fill_no_advance: col rests at W-1");
    CHECK_EQ(vt.pending_wrap, 1,    "fill_no_advance: pending_wrap is set");
}

/*
 * Scenario 2: after filling the row, a CR clears pending_wrap and
 * returns to col 0 *without* advancing the row.  This is the
 * fundamental promise the xenl state machine has to keep.
 */
static void test_fill_then_cr(void) {
    host_vt_t vt; vt_init(&vt);
    for (int i = 0; i < W; i++) cb_putc(&vt, 'a');
    cb_putc(&vt, '\r');
    CHECK_EQ(vt.row, 0,             "fill_then_cr: row stays at 0");
    CHECK_EQ(vt.col, 0,             "fill_then_cr: col returns to 0");
    CHECK_EQ(vt.pending_wrap, 0,    "fill_then_cr: pending_wrap cleared");
}

/*
 * Scenario 3: after filling the row, the *next printable* finally
 * performs the deferred wrap.  Row should advance exactly once and
 * the new char lands at (1, 0).
 */
static void test_fill_then_printable(void) {
    host_vt_t vt; vt_init(&vt);
    for (int i = 0; i < W; i++) cb_putc(&vt, 'a');
    cb_putc(&vt, 'X');
    CHECK_EQ(vt.row, 1,             "fill_then_printable: row advances once");
    CHECK_EQ(vt.col, 1,             "fill_then_printable: col is 1 after print");
    CHECK_EQ(vt.buf[1][0], 'X',     "fill_then_printable: X lands at (1,0)");
    CHECK(row_is(&vt, 0, "aaaa"),   "fill_then_printable: row 0 preserved");
}

/*
 * Scenario 4: after filling the row, a LF should advance the row
 * exactly once — not twice.  Some buggy state machines treat
 * pending_wrap + \n as "wrap then newline", consuming two rows.
 */
static void test_fill_then_newline(void) {
    host_vt_t vt; vt_init(&vt);
    for (int i = 0; i < W; i++) cb_putc(&vt, 'a');
    cb_putc(&vt, '\n');
    cb_putc(&vt, 'Y');
    CHECK_EQ(vt.row, 1,             "fill_then_newline: row advances exactly once");
    CHECK_EQ(vt.buf[1][0], 'Y',     "fill_then_newline: Y at (1,0)");
    CHECK_EQ(vt.buf[2][0], '.',     "fill_then_newline: row 2 untouched");
}

/*
 * Scenario 5: backspace from pending-wrap state decrements col *and*
 * clears pending_wrap, matching xterm.  The cursor lands at col=W-2,
 * so the next printable overwrites col W-2 (not the very last col).
 */
static void test_fill_then_backspace(void) {
    host_vt_t vt; vt_init(&vt);
    for (int i = 0; i < W; i++) cb_putc(&vt, 'a');
    cb_putc(&vt, '\b');
    cb_putc(&vt, 'b');
    CHECK_EQ(vt.row, 0,             "fill_then_backspace: row stays at 0");
    CHECK_EQ(vt.pending_wrap, 0,    "fill_then_backspace: pending_wrap cleared");
    CHECK_EQ(vt.buf[0][W - 2], 'b', "fill_then_backspace: b overwrites col W-2");
    CHECK_EQ(vt.buf[0][W - 1], 'a', "fill_then_backspace: original last char intact");
}

/*
 * Scenario 6: autowrap=0 — chars at the right margin are clamped.
 * Pending_wrap should never set, and extra chars overwrite col W-1.
 */
static void test_autowrap_off_clamp(void) {
    host_vt_t vt; vt_init(&vt);
    vt.autowrap = 0;
    for (int i = 0; i < W + 10; i++) cb_putc(&vt, '0' + (i % 10));
    CHECK_EQ(vt.row, 0,             "autowrap_off: row never advances");
    CHECK_EQ(vt.col, W - 1,         "autowrap_off: col clamps at W-1");
    CHECK_EQ(vt.pending_wrap, 0,    "autowrap_off: pending_wrap never set");
}

/*
 * Scenario 7: fill the *last visible row*, then push one more
 * printable.  The deferred wrap should scroll the region exactly
 * once and leave the new char at (bottom, 0).
 */
static void test_fill_bottom_scrolls(void) {
    host_vt_t vt; vt_init(&vt);
    /* fill rows 0..H-2 with row-numbered content. */
    for (int r = 0; r <= H - 2; r++) {
        char marker = 'A' + (r % 26);
        for (int c = 0; c < W; c++) cb_putc(&vt, marker);
        cb_putc(&vt, '\n');
    }
    /* Now sitting at row H-1 col 0.  Fill it. */
    for (int c = 0; c < W; c++) cb_putc(&vt, 'Z');
    CHECK_EQ(vt.row, H - 1,         "fill_bottom_scrolls: at bottom row");
    CHECK_EQ(vt.col, W - 1,         "fill_bottom_scrolls: at right margin");
    CHECK_EQ(vt.pending_wrap, 1,    "fill_bottom_scrolls: pending_wrap set");
    /* The next printable scrolls the region. */
    cb_putc(&vt, '!');
    CHECK_EQ(vt.row, H - 1,         "fill_bottom_scrolls: stays at bottom after scroll");
    CHECK_EQ(vt.buf[H - 1][0], '!', "fill_bottom_scrolls: ! at bottom-left of new row");
    CHECK_EQ(vt.buf[H - 2][0], 'Z', "fill_bottom_scrolls: previous bottom scrolled up to H-2");
}

/*
 * Scenario 8 (the headline): exact byte stream zsh emits for
 * PROMPT_SP with `xenl` set, eolmark="%B%S%#%s%b" (1 visible cell),
 * computed from Src/utils.c:1550:
 *
 *     fprintf(shout, "%*s\r%*s\r",
 *             (int)zterm_columns - w - !hasxn, "",   // W-1-0 = 79 spaces
 *             w, "");                                 // w=1 space
 *
 * Plus the eolmark `%` (the CSI bold/inverse wrappers are consumed
 * by the ANSI escape handler and don't reach cb_putc).
 *
 * Concrete printable byte stream into cb_putc when on a fresh row:
 *   '%'  +  (W-1) spaces  +  '\r'  +  1 space  +  '\r'
 *
 * Expected end state: row=0, col=0, no row advance, row 0 mostly
 * blank (the `%` was overwritten by the post-CR space).
 */
static void test_zsh_promptsp_xenl(void) {
    host_vt_t vt; vt_init(&vt);
    cb_putc(&vt, '%');
    vt_putn(&vt, ' ', W - 1);
    cb_putc(&vt, '\r');
    cb_putc(&vt, ' ');
    cb_putc(&vt, '\r');
    CHECK_EQ(vt.row, 0,             "promptsp_xenl: NO row advance");
    CHECK_EQ(vt.col, 0,             "promptsp_xenl: cursor at col 0");
    CHECK_EQ(vt.pending_wrap, 0,    "promptsp_xenl: pending_wrap cleared");
    CHECK_EQ(vt.buf[0][0], ' ',     "promptsp_xenl: % overwritten by space");
    CHECK_EQ(vt.buf[1][0], '.',     "promptsp_xenl: row 1 untouched");
}

/*
 * Scenario 9: same as above but at the *bottom* row.  This is the
 * one that actually shows up as "blank line between commands" — a
 * row advance here scrolls everything up.
 */
static void test_zsh_promptsp_at_bottom(void) {
    host_vt_t vt; vt_init(&vt);
    vt.row = H - 1;
    /* Stamp a marker in row 0 so we can detect scrolling. */
    cb_putc(&vt, '\r');  /* col=0 */
    int saved_row = vt.row;
    vt.row = 0;
    cb_putc(&vt, 'M');
    vt.row = saved_row;
    vt.col = 0;
    /* Now run the PROMPT_SP sequence at row H-1. */
    cb_putc(&vt, '%');
    vt_putn(&vt, ' ', W - 1);
    cb_putc(&vt, '\r');
    cb_putc(&vt, ' ');
    cb_putc(&vt, '\r');
    CHECK_EQ(vt.row, H - 1,         "promptsp_at_bottom: stays at bottom");
    CHECK_EQ(vt.buf[0][0], 'M',     "promptsp_at_bottom: row 0 marker intact (no scroll)");
}

/*
 * Scenario 10: what zsh emits if hasxn is *unset* — adds one MORE
 * space.  This SHOULD scroll exactly once on a real terminal (because
 * the extra space triggers the autowrap), and our state machine has
 * to do the same.  This is the failure mode you see if substrate's
 * terminfo entry forgets to declare `xenl`.
 */
static void test_zsh_promptsp_no_xenl(void) {
    host_vt_t vt; vt_init(&vt);
    cb_putc(&vt, '%');
    vt_putn(&vt, ' ', W);        /* one extra space vs xenl path */
    cb_putc(&vt, '\r');
    cb_putc(&vt, ' ');
    cb_putc(&vt, '\r');
    CHECK_EQ(vt.row, 1,             "promptsp_no_xenl: row advances once (expected for !hasxn)");
}

/*
 * Scenario 11: zsh sends a wider width than the terminal actually
 * has.  If TIOCGWINSZ returns W+1 (off-by-one), the PROMPT_SP
 * sequence becomes:
 *   '%' + W spaces + '\r' + ' ' + '\r'
 * which DOES trigger a row advance on real hardware.  This shows
 * up identically to the no-xenl case and is the symptom you'd see
 * if substrate's window-size advertisement disagrees with the
 * actual console geometry.
 */
static void test_promptsp_width_off_by_one(void) {
    host_vt_t vt; vt_init(&vt);
    cb_putc(&vt, '%');
    vt_putn(&vt, ' ', W);       /* zsh thinks term is W+1 wide */
    cb_putc(&vt, '\r');
    cb_putc(&vt, ' ');
    cb_putc(&vt, '\r');
    CHECK_EQ(vt.row, 1,             "promptsp_width_off_by_one: row advances (zsh overshoots width)");
}

int main(void) {
    test_fill_no_advance();
    test_fill_then_cr();
    test_fill_then_printable();
    test_fill_then_newline();
    test_fill_then_backspace();
    test_autowrap_off_clamp();
    test_fill_bottom_scrolls();
    test_zsh_promptsp_xenl();
    test_zsh_promptsp_at_bottom();
    test_zsh_promptsp_no_xenl();
    test_promptsp_width_off_by_one();

    printf("\n=== test_vt_wrap: %d passed, %d failed ===\n", passed, failed);
    return failed == 0 ? 0 : 1;
}
