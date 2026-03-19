#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>

/* Mock implementation of ansi_handler.c's requirements */
#include <kern/ansi_handler.h>

/* Mock callbacks state */
static int mock_width = 80;
static int mock_height = 25;
static int mock_cursor_row = 0;
static int mock_cursor_col = 0;
static uint8_t mock_fg = 7;
static uint8_t mock_bg = 0;
static char output_buffer[1024];
static int output_pos = 0;
static int screen_cleared = 0;
static int erase_display_mode = -1;
static int erase_line_mode = -1;
static int scroll_region_top = -1;
static int scroll_region_bottom = -1;

/* Callback implementations */
static void mock_putc(char c) {
    if (output_pos < 1024 - 1) {
        output_buffer[output_pos++] = c;
        output_buffer[output_pos] = '\0';
    }
}

static void mock_set_color(uint8_t fg, uint8_t bg) {
    mock_fg = fg;
    mock_bg = bg;
}

static void mock_clear_screen(void) {
    screen_cleared = 1;
}

static void mock_erase_display(int mode) {
    erase_display_mode = mode;
}

static void mock_erase_line(int mode) {
    erase_line_mode = mode;
}

static void mock_set_scroll_region(int top, int bottom) {
    scroll_region_top = top;
    scroll_region_bottom = bottom;
}

static void mock_move_cursor(int row, int col) {
    mock_cursor_row = row;
    mock_cursor_col = col;
}

static void mock_get_cursor(int *row, int *col) {
    *row = mock_cursor_row;
    *col = mock_cursor_col;
}

static void mock_get_dimensions(int *width, int *height) {
    *width = mock_width;
    *height = mock_height;
}

static void mock_get_color(uint8_t *fg, uint8_t *bg) {
    *fg = mock_fg;
    *bg = mock_bg;
}

/* Include the source file directly */
#include "../../sys/drivers/console/ansi_handler.c"

/* Test helpers */
static struct ansi_callbacks callbacks = {
    .putc = mock_putc,
    .set_color = mock_set_color,
    .clear_screen = mock_clear_screen,
    .erase_display = mock_erase_display,
    .erase_line = mock_erase_line,
    .move_cursor = mock_move_cursor,
    .get_cursor = mock_get_cursor,
    .get_dimensions = mock_get_dimensions,
    .get_color = mock_get_color,
    .set_scroll_region = mock_set_scroll_region
};

static void reset_mocks() {
    mock_width = 80;
    mock_height = 25;
    mock_cursor_row = 0;
    mock_cursor_col = 0;
    mock_fg = 7;
    mock_bg = 0;
    output_pos = 0;
    output_buffer[0] = '\0';
    screen_cleared = 0;
    erase_display_mode = -1;
    erase_line_mode = -1;
    scroll_region_top = -1;
    scroll_region_bottom = -1;
}

static void run_test(const char *name, void (*test_func)(void)) {
    printf("Running %s... ", name);
    test_func();
    printf("PASSED\n");
}

/* Tests */
static void test_simple_char(void) {
    struct ansi_ctx ctx;
    ansi_init(&ctx);
    reset_mocks();

    ansi_process(&ctx, 'A', &callbacks);
    assert(strcmp(output_buffer, "A") == 0);
    assert(mock_cursor_col == 0); /* Handler doesn't auto-advance cursor in mock */
}

static void test_cursor_movement(void) {
    struct ansi_ctx ctx;
    ansi_init(&ctx);
    reset_mocks();

    /* ESC [ 5 ; 10 H -> Move to row 5, col 10 (1-based) -> (4, 9) 0-based */
    const char *seq = "\x1b[5;10H";
    while (*seq) ansi_process(&ctx, *seq++, &callbacks);

    assert(mock_cursor_row == 4);
    assert(mock_cursor_col == 9);

    /* Test default parameters: ESC [ H -> (1, 1) -> (0, 0) */
    seq = "\x1b[H";
    while (*seq) ansi_process(&ctx, *seq++, &callbacks);
    assert(mock_cursor_row == 0);
    assert(mock_cursor_col == 0);
}

static void test_color_change(void) {
    struct ansi_ctx ctx;
    ansi_init(&ctx);
    reset_mocks();

    /* ESC [ 31 m -> Red FG (1) */
    const char *seq = "\x1b[31m";
    while (*seq) ansi_process(&ctx, *seq++, &callbacks);
    assert(mock_fg == 1);

    /* ESC [ 42 m -> Green BG (2) */
    seq = "\x1b[42m";
    while (*seq) ansi_process(&ctx, *seq++, &callbacks);
    assert(mock_bg == 2);

    /* ESC [ 0 m -> Reset (7, 0) */
    seq = "\x1b[0m";
    while (*seq) ansi_process(&ctx, *seq++, &callbacks);
    assert(mock_fg == 7);
    assert(mock_bg == 0);
}

static void test_bright_color_change(void) {
    struct ansi_ctx ctx;
    ansi_init(&ctx);
    reset_mocks();

    /* ESC [ 97 ; 104 m -> bright white fg, bright blue bg */
    const char *seq = "\x1b[97;104m";
    while (*seq) {
        ansi_process(&ctx, *seq++, &callbacks);
    }

    assert(mock_fg == 15);
    assert(mock_bg == 12);
}

static void test_clear_screen(void) {
    struct ansi_ctx ctx;
    ansi_init(&ctx);
    reset_mocks();

    /* ESC [ 2 J -> Clear Screen */
    const char *seq = "\x1b[2J";
    while (*seq) ansi_process(&ctx, *seq++, &callbacks);

    assert(screen_cleared == 1);
    assert(erase_display_mode == -1);
}

static void test_erase_display_default_mode(void) {
    struct ansi_ctx ctx;
    ansi_init(&ctx);
    reset_mocks();

    mock_cursor_row = 4;
    mock_cursor_col = 9;

    const char *seq = "\x1b[J";
    while (*seq) ansi_process(&ctx, *seq++, &callbacks);

    assert(screen_cleared == 0);
    assert(erase_display_mode == 0);
    assert(mock_cursor_row == 4);
    assert(mock_cursor_col == 9);
}

static void test_erase_line_modes(void) {
    struct ansi_ctx ctx;
    const char *seq;

    ansi_init(&ctx);
    reset_mocks();
    seq = "\x1b[K";
    while (*seq) ansi_process(&ctx, *seq++, &callbacks);
    assert(erase_line_mode == 0);

    ansi_init(&ctx);
    reset_mocks();
    seq = "\x1b[1K";
    while (*seq) ansi_process(&ctx, *seq++, &callbacks);
    assert(erase_line_mode == 1);

    ansi_init(&ctx);
    reset_mocks();
    seq = "\x1b[2K";
    while (*seq) ansi_process(&ctx, *seq++, &callbacks);
    assert(erase_line_mode == 2);
}

static void test_set_scroll_region(void) {
    struct ansi_ctx ctx;
    const char *seq;

    ansi_init(&ctx);
    reset_mocks();
    mock_cursor_row = 7;
    mock_cursor_col = 9;

    seq = "\x1b[2;20r";
    while (*seq) {
        ansi_process(&ctx, *seq++, &callbacks);
    }

    assert(scroll_region_top == 1);
    assert(scroll_region_bottom == 19);
    assert(mock_cursor_row == 0);
    assert(mock_cursor_col == 0);
}

static void test_relative_moves(void) {
    struct ansi_ctx ctx;
    ansi_init(&ctx);
    reset_mocks();

    /* Start at (10, 10) */
    mock_cursor_row = 10;
    mock_cursor_col = 10;

    /* ESC [ A -> Up 1 -> (9, 10) */
    const char *seq = "\x1b[A";
    while (*seq) ansi_process(&ctx, *seq++, &callbacks);
    assert(mock_cursor_row == 9);

    /* ESC [ 2 B -> Down 2 -> (11, 10) */
    seq = "\x1b[2B";
    while (*seq) ansi_process(&ctx, *seq++, &callbacks);
    assert(mock_cursor_row == 11);

    /* ESC [ C -> Forward 1 -> (11, 11) */
    seq = "\x1b[C";
    while (*seq) ansi_process(&ctx, *seq++, &callbacks);
    assert(mock_cursor_col == 11);

    /* ESC [ 2 D -> Back 2 -> (11, 9) */
    seq = "\x1b[2D";
    while (*seq) ansi_process(&ctx, *seq++, &callbacks);
    assert(mock_cursor_col == 9);
}

static void test_partial_sequence(void) {
    struct ansi_ctx ctx;
    ansi_init(&ctx);
    reset_mocks();

    ansi_process(&ctx, '\x1b', &callbacks);
    assert(ctx.state == ANSI_ESC);

    ansi_process(&ctx, '[', &callbacks);
    assert(ctx.state == ANSI_CSI);

    ansi_process(&ctx, '3', &callbacks);
    assert(ctx.state == ANSI_PARAM);

    ansi_process(&ctx, '1', &callbacks); // param is 31

    ansi_process(&ctx, 'm', &callbacks); // Finish
    assert(ctx.state == ANSI_NORMAL);
    assert(mock_fg == 1);
}

int main(void) {
    run_test("Simple Char", test_simple_char);
    run_test("Cursor Movement", test_cursor_movement);
    run_test("Color Change", test_color_change);
    run_test("Bright Color Change", test_bright_color_change);
    run_test("Clear Screen", test_clear_screen);
    run_test("Erase Display Default", test_erase_display_default_mode);
    run_test("Erase Line Modes", test_erase_line_modes);
    run_test("Set Scroll Region", test_set_scroll_region);
    run_test("Relative Moves", test_relative_moves);
    run_test("Partial Sequence", test_partial_sequence);

    return 0;
}
