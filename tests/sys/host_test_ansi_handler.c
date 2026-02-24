#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

// Mock definitions required for ANSI handler
// HOST_TEST is defined in Makefile

// Include the source file directly to test internal logic if needed
// This also avoids needing to link against a separate object file
#include "../../sys/drivers/console/ansi_handler.c"

// Test Helper Macros
#define ASSERT_EQ(a, b, msg) do { \
    if ((a) != (b)) { \
        printf("FAIL: %s\n  Expected: %d\n  Actual:   %d\n", msg, (int)(b), (int)(a)); \
        exit(1); \
    } \
} while(0)

// Mock State variables
static int mock_cursor_row = 0;
static int mock_cursor_col = 0;
static int mock_width = 80;
static int mock_height = 25;
static uint8_t mock_fg = 7;
static uint8_t mock_bg = 0;
static bool mock_screen_cleared = false;

// Mock Callbacks
static void mock_putc(char c) {
    (void)c; // Just consume
}

static void mock_set_color(uint8_t fg, uint8_t bg) {
    mock_fg = fg;
    mock_bg = bg;
}

static void mock_clear_screen(void) {
    mock_screen_cleared = true;
}

static void mock_move_cursor(int row, int col) {
    mock_cursor_row = row;
    mock_cursor_col = col;
}

static void mock_scroll(void) {
    // Scroll
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

static const struct ansi_callbacks callbacks = {
    .putc = mock_putc,
    .set_color = mock_set_color,
    .clear_screen = mock_clear_screen,
    .move_cursor = mock_move_cursor,
    .scroll = mock_scroll,
    .get_cursor = mock_get_cursor,
    .get_dimensions = mock_get_dimensions,
    .get_color = mock_get_color
};

void reset_mocks(void) {
    mock_cursor_row = 0;
    mock_cursor_col = 0;
    mock_width = 80;
    mock_height = 25;
    mock_fg = 7;
    mock_bg = 0;
    mock_screen_cleared = false;
}

void process_string(struct ansi_ctx *ctx, const char *s) {
    while (*s) {
        ansi_process(ctx, *s++, &callbacks);
    }
}

void test_cursor_movement(void) {
    struct ansi_ctx ctx;
    ansi_init(&ctx);
    reset_mocks();

    // Initial position (0,0)
    mock_cursor_row = 10;
    mock_cursor_col = 10;

    // Up (A) -> (9, 10)
    process_string(&ctx, "\x1b[A");
    ASSERT_EQ(mock_cursor_row, 9, "Cursor Up (A)");

    // Down (B) -> (10, 10)
    process_string(&ctx, "\x1b[B");
    ASSERT_EQ(mock_cursor_row, 10, "Cursor Down (B)");

    // Forward (C) -> (10, 11)
    process_string(&ctx, "\x1b[C");
    ASSERT_EQ(mock_cursor_col, 11, "Cursor Forward (C)");

    // Back (D) -> (10, 10)
    process_string(&ctx, "\x1b[D");
    ASSERT_EQ(mock_cursor_col, 10, "Cursor Back (D)");

    // Multiple steps
    // Up 5 -> (5, 10)
    process_string(&ctx, "\x1b[5A");
    ASSERT_EQ(mock_cursor_row, 5, "Cursor Up 5 (5A)");

    // Bounds check: Up 10 -> (0, 10) (clamped at 0)
    process_string(&ctx, "\x1b[10A");
    ASSERT_EQ(mock_cursor_row, 0, "Cursor Up Bound (10A -> 0)");

    // Down 100 -> (24, 10) (clamped at height-1=24)
    process_string(&ctx, "\x1b[100B");
    ASSERT_EQ(mock_cursor_row, 24, "Cursor Down Bound (100B -> 24)");
}

void test_cursor_position(void) {
    struct ansi_ctx ctx;
    ansi_init(&ctx);
    reset_mocks();

    // H - Cursor Position
    // Row 5, Col 10 -> (4, 9) because 1-based to 0-based
    process_string(&ctx, "\x1b[5;10H");
    ASSERT_EQ(mock_cursor_row, 4, "Cursor Position Row (H)");
    ASSERT_EQ(mock_cursor_col, 9, "Cursor Position Col (H)");

    // f - Same as H
    process_string(&ctx, "\x1b[2;2f");
    ASSERT_EQ(mock_cursor_row, 1, "Cursor Position Row (f)");
    ASSERT_EQ(mock_cursor_col, 1, "Cursor Position Col (f)");

    // Default (1,1) -> (0,0)
    process_string(&ctx, "\x1b[H");
    ASSERT_EQ(mock_cursor_row, 0, "Cursor Position Default (H)");
    ASSERT_EQ(mock_cursor_col, 0, "Cursor Position Default (H)");
}

void test_clear_screen(void) {
    struct ansi_ctx ctx;
    ansi_init(&ctx);
    reset_mocks();

    process_string(&ctx, "\x1b[2J");
    ASSERT_EQ(mock_screen_cleared, 1, "Clear Screen (2J)");
    ASSERT_EQ(mock_cursor_row, 0, "Clear Screen Reset Cursor Row");
    ASSERT_EQ(mock_cursor_col, 0, "Clear Screen Reset Cursor Col");

    mock_screen_cleared = false;
    // J without params usually defaults to 0 (clear from cursor to end), but handler only implements 2J?
    // Code check: if (ctx->param_count > 0 && ctx->params[0] == 2 && cb->clear_screen)
    process_string(&ctx, "\x1b[J");
    ASSERT_EQ(mock_screen_cleared, 0, "Clear Screen (J default) - Not Implemented or different");
}

void test_colors(void) {
    struct ansi_ctx ctx;
    ansi_init(&ctx);
    reset_mocks();

    // Reset (0) -> fg=7, bg=0
    process_string(&ctx, "\x1b[0m");
    ASSERT_EQ(mock_fg, 7, "Color Reset (0m)");
    ASSERT_EQ(mock_bg, 0, "Color Reset Bg (0m)");

    // Red fg (31) -> fg=1 (based on p-30 logic seen in code)
    // Note: Standard ANSI Red is 31. The handler does `cur_fg = p - 30`.
    // So 31-30=1.
    process_string(&ctx, "\x1b[31m");
    ASSERT_EQ(mock_fg, 1, "Color Red (31m)");

    // Green bg (42) -> bg=2 (p-40 -> 42-40=2)
    process_string(&ctx, "\x1b[42m");
    ASSERT_EQ(mock_bg, 2, "Color Bg Green (42m)");

    // Bold (1) -> if fg < 8, fg += 8.
    // Current fg is 1. 1+8 = 9.
    process_string(&ctx, "\x1b[1m");
    ASSERT_EQ(mock_fg, 9, "Color Bold (1m)");

    // Multiple params: Reset, Red fg, Green bg
    // \x1b[0;31;42m
    // 0 -> fg=7, bg=0
    // 31 -> fg=1
    // 42 -> bg=2
    process_string(&ctx, "\x1b[0;31;42m");
    ASSERT_EQ(mock_fg, 1, "Multi Color Fg");
    ASSERT_EQ(mock_bg, 2, "Multi Color Bg");
}

void test_state_machine_resilience(void) {
    struct ansi_ctx ctx;
    ansi_init(&ctx);
    reset_mocks();

    // Split sequence across calls
    // \x1b [ 3 1 m
    ansi_process(&ctx, '\x1b', &callbacks);
    ansi_process(&ctx, '[', &callbacks);
    ansi_process(&ctx, '3', &callbacks);
    ansi_process(&ctx, '1', &callbacks);
    ansi_process(&ctx, 'm', &callbacks);

    ASSERT_EQ(mock_fg, 1, "Split Sequence Parsing");

    // Interrupted sequence?
    // \x1b [ 3 1 X (invalid char X resets state?)
    // Code: else if (c >= 0x40 && c <= 0x7E) -> handle_csi
    // X is 0x58, so it is handled by handle_csi.
    // But handle_csi switch only handles m, J, H, f, A, B, C, D, K.
    // Default case does nothing.
    // So it consumes X and returns to normal.

    process_string(&ctx, "\x1b[31X");
    // fg should be unchanged (default 7)
    reset_mocks();
    process_string(&ctx, "\x1b[31X");
    ASSERT_EQ(mock_fg, 7, "Invalid Sequence Ignored");
}

int main(void) {
    printf("Running ANSI Handler Tests\n");
    test_cursor_movement();
    test_cursor_position();
    test_clear_screen();
    test_colors();
    test_state_machine_resilience();
    printf("All ANSI Tests Passed\n");
    return 0;
}
