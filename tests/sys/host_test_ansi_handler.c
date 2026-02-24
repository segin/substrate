#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <assert.h>

// Mock kernel structures/types if needed
// drivers/video/vga.h might be needed, but we rely on include path to find it or mock it.
// ansi_handler.c uses uint8_t which is fine.

// Include the source file directly
#include "../../sys/drivers/console/ansi_handler.c"

// Test Helper Macros
#define ASSERT_EQ(a, b, msg) do { \
    if ((a) != (b)) { \
        printf("FAIL: %s\n  Expected: %d\n  Actual:   %d\n", msg, (int)(b), (int)(a)); \
        exit(1); \
    } \
} while(0)

#define ASSERT_TRUE(a, msg) do { \
    if (!(a)) { \
        printf("FAIL: %s\n", msg); \
        exit(1); \
    } \
} while(0)

// Mocks for callbacks
static int mock_cursor_row = 0;
static int mock_cursor_col = 0;
static int mock_width = 80;
static int mock_height = 25;
static uint8_t mock_fg = 7;
static uint8_t mock_bg = 0;
static char mock_buffer[1024];
static int mock_buffer_idx = 0;

static void reset_mocks(void) {
    mock_cursor_row = 0;
    mock_cursor_col = 0;
    mock_width = 80;
    mock_height = 25;
    mock_fg = 7;
    mock_bg = 0;
    mock_buffer_idx = 0;
    memset(mock_buffer, 0, sizeof(mock_buffer));
}

static void mock_putc(char c) {
    if (mock_buffer_idx < (int)sizeof(mock_buffer) - 1) {
        mock_buffer[mock_buffer_idx++] = c;
        mock_buffer[mock_buffer_idx] = '\0';
    }
}

static void mock_set_color(uint8_t fg, uint8_t bg) {
    mock_fg = fg;
    mock_bg = bg;
}

static void mock_clear_screen(void) {
    // Just a flag or counter could be used, but for now empty is fine
    // Maybe verify side effects if needed, but ansi_handler just calls it.
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

void test_basic_chars(void) {
    struct ansi_ctx ctx;
    ansi_init(&ctx);
    reset_mocks();

    ansi_process(&ctx, 'A', &callbacks);
    ansi_process(&ctx, 'B', &callbacks);

    ASSERT_EQ(mock_buffer_idx, 2, "Buffer length");
    ASSERT_EQ(mock_buffer[0], 'A', "Char A");
    ASSERT_EQ(mock_buffer[1], 'B', "Char B");

    printf("test_basic_chars: PASS\n");
}

void test_cursor_movement(void) {
    struct ansi_ctx ctx;
    ansi_init(&ctx);
    reset_mocks();

    // Start at 10, 10
    mock_cursor_row = 10;
    mock_cursor_col = 10;

    // Up (A)
    const char *seq = "\x1b[A";
    for (int i = 0; seq[i]; i++) ansi_process(&ctx, seq[i], &callbacks);
    ASSERT_EQ(mock_cursor_row, 9, "Cursor Up");
    ASSERT_EQ(mock_cursor_col, 10, "Cursor Up Col check");

    // Down (B)
    seq = "\x1b[B";
    for (int i = 0; seq[i]; i++) ansi_process(&ctx, seq[i], &callbacks);
    ASSERT_EQ(mock_cursor_row, 10, "Cursor Down");

    // Forward (C)
    seq = "\x1b[C";
    for (int i = 0; seq[i]; i++) ansi_process(&ctx, seq[i], &callbacks);
    ASSERT_EQ(mock_cursor_col, 11, "Cursor Forward");

    // Back (D)
    seq = "\x1b[D";
    for (int i = 0; seq[i]; i++) ansi_process(&ctx, seq[i], &callbacks);
    ASSERT_EQ(mock_cursor_col, 10, "Cursor Back");

    // Multiple steps: \x1b[5A (Up 5)
    seq = "\x1b[5A";
    for (int i = 0; seq[i]; i++) ansi_process(&ctx, seq[i], &callbacks);
    ASSERT_EQ(mock_cursor_row, 5, "Cursor Up 5");

    printf("test_cursor_movement: PASS\n");
}

void test_cursor_absolute(void) {
    struct ansi_ctx ctx;
    ansi_init(&ctx);
    reset_mocks();

    // Start at 0,0
    mock_cursor_row = 0;
    mock_cursor_col = 0;

    // \x1b[5;10H -> Row 5, Col 10 (1-based input, 0-based storage)
    const char *seq = "\x1b[5;10H";
    for (int i = 0; seq[i]; i++) ansi_process(&ctx, seq[i], &callbacks);

    // Expected: Row 4, Col 9
    ASSERT_EQ(mock_cursor_row, 4, "Cursor Abs Row");
    ASSERT_EQ(mock_cursor_col, 9, "Cursor Abs Col");

    // Default params: \x1b[H -> 1,1 -> 0,0
    seq = "\x1b[H";
    for (int i = 0; seq[i]; i++) ansi_process(&ctx, seq[i], &callbacks);
    ASSERT_EQ(mock_cursor_row, 0, "Cursor Home Row");
    ASSERT_EQ(mock_cursor_col, 0, "Cursor Home Col");

    printf("test_cursor_absolute: PASS\n");
}

void test_colors(void) {
    struct ansi_ctx ctx;
    ansi_init(&ctx);
    reset_mocks();

    // Default 7, 0
    ASSERT_EQ(mock_fg, 7, "Default FG");
    ASSERT_EQ(mock_bg, 0, "Default BG");

    // Red FG (31)
    const char *seq = "\x1b[31m";
    for (int i = 0; seq[i]; i++) ansi_process(&ctx, seq[i], &callbacks);
    // ansi_handler maps 30-37 to 0-7. So 31 -> 1.
    ASSERT_EQ(mock_fg, 1, "FG Red (31 -> 1)");

    // Blue BG (44) -> 44-40 = 4.
    seq = "\x1b[44m";
    for (int i = 0; seq[i]; i++) ansi_process(&ctx, seq[i], &callbacks);
    ASSERT_EQ(mock_bg, 4, "BG Blue (44 -> 4)");

    // Reset (0)
    seq = "\x1b[0m";
    for (int i = 0; seq[i]; i++) ansi_process(&ctx, seq[i], &callbacks);
    ASSERT_EQ(mock_fg, 7, "Reset FG");
    ASSERT_EQ(mock_bg, 0, "Reset BG");

    // Bold/Bright (1)
    // First set to standard color (e.g. 31 -> 1)
    seq = "\x1b[31m";
    for (int i = 0; seq[i]; i++) ansi_process(&ctx, seq[i], &callbacks);
    ASSERT_EQ(mock_fg, 1, "FG Red before bold");

    // Apply bold
    seq = "\x1b[1m";
    for (int i = 0; seq[i]; i++) ansi_process(&ctx, seq[i], &callbacks);
    // 1 -> adds 8 if < 8. So 1 + 8 = 9.
    ASSERT_EQ(mock_fg, 9, "FG Red Bold (1 -> 9)");

    // Multiple params: \x1b[32;1m -> Green (2) + Bold (+8) = 10
    seq = "\x1b[32;1m";
    for (int i = 0; seq[i]; i++) ansi_process(&ctx, seq[i], &callbacks);
    ASSERT_EQ(mock_fg, 10, "FG Green Bold (2+8=10)");

    printf("test_colors: PASS\n");
}

void test_clear_screen(void) {
    struct ansi_ctx ctx;
    ansi_init(&ctx);
    reset_mocks();

    // Clear screen (2J)
    // We can't easily check if clear_screen was called with just empty mock,
    // but we can check if cursor moved if ansi_handler moves it (it does move to 0,0 for 2J).

    mock_cursor_row = 10;
    mock_cursor_col = 10;

    const char *seq = "\x1b[2J";
    for (int i = 0; seq[i]; i++) ansi_process(&ctx, seq[i], &callbacks);

    // ansi_handler.c: if (ctx->params[0] == 2 && cb->clear_screen) { cb->clear_screen(); if (cb->move_cursor) cb->move_cursor(0, 0); }
    ASSERT_EQ(mock_cursor_row, 0, "Clear Screen Moves Cursor Row");
    ASSERT_EQ(mock_cursor_col, 0, "Clear Screen Moves Cursor Col");

    printf("test_clear_screen: PASS\n");
}

void test_partial_sequences(void) {
    struct ansi_ctx ctx;
    ansi_init(&ctx);
    reset_mocks();

    // Send ESC
    ansi_process(&ctx, '\x1b', &callbacks);
    ASSERT_EQ(ctx.state, ANSI_ESC, "State ESC");

    // Send [
    ansi_process(&ctx, '[', &callbacks);
    ASSERT_EQ(ctx.state, ANSI_CSI, "State CSI");

    // Send 3
    ansi_process(&ctx, '3', &callbacks);
    ASSERT_EQ(ctx.state, ANSI_PARAM, "State PARAM");

    // Send 1
    ansi_process(&ctx, '1', &callbacks);

    // Send m
    ansi_process(&ctx, 'm', &callbacks);
    ASSERT_EQ(ctx.state, ANSI_NORMAL, "State NORMAL");

    ASSERT_EQ(mock_fg, 1, "FG Red (31)");

    printf("test_partial_sequences: PASS\n");
}

int main(void) {
    printf("Running ANSI Handler Tests (Host)\n");
    test_basic_chars();
    test_cursor_movement();
    test_cursor_absolute();
    test_colors();
    test_clear_screen();
    test_partial_sequences();
    printf("All ANSI Tests Passed\n");
    return 0;
}
