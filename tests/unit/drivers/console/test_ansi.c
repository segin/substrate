#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <kern/ansi_handler.h>

// Mock state
static int mock_putc_calls = 0;
static char mock_last_char = 0;
static int mock_set_color_calls = 0;
static uint8_t mock_fg = 0;
static uint8_t mock_bg = 0;
static int mock_clear_screen_calls = 0;
static int mock_move_cursor_calls = 0;
static int mock_row = 0;
static int mock_col = 0;
static int mock_width = 80;
static int mock_height = 25;

// Mock callbacks
static void mock_putc(char c) {
    mock_putc_calls++;
    mock_last_char = c;
}

static void mock_set_color(uint8_t fg, uint8_t bg) {
    mock_set_color_calls++;
    mock_fg = fg;
    mock_bg = bg;
}

static void mock_clear_screen(void) {
    mock_clear_screen_calls++;
}

static void mock_move_cursor(int row, int col) {
    mock_move_cursor_calls++;
    mock_row = row;
    mock_col = col;
}

static void mock_get_cursor(int *row, int *col) {
    *row = mock_row;
    *col = mock_col;
}

static void mock_get_dimensions(int *width, int *height) {
    *width = mock_width;
    *height = mock_height;
}

static void mock_get_color(uint8_t *fg, uint8_t *bg) {
    *fg = mock_fg;
    *bg = mock_bg;
}

static struct ansi_callbacks callbacks = {
    .putc = mock_putc,
    .set_color = mock_set_color,
    .clear_screen = mock_clear_screen,
    .move_cursor = mock_move_cursor,
    .get_cursor = mock_get_cursor,
    .get_dimensions = mock_get_dimensions,
    .get_color = mock_get_color,
    .scroll = NULL
};

static void reset_mocks(void) {
    mock_putc_calls = 0;
    mock_last_char = 0;
    mock_set_color_calls = 0;
    mock_fg = 7;
    mock_bg = 0;
    mock_clear_screen_calls = 0;
    mock_move_cursor_calls = 0;
    mock_row = 0;
    mock_col = 0;
    mock_width = 80;
    mock_height = 25;
}

static void feed_string(struct ansi_ctx *ctx, const char *str) {
    while (*str) {
        ansi_process(ctx, *str++, &callbacks);
    }
}

bool test_ansi_parsing(void) {
    struct ansi_ctx ctx;

    // Test 1: Normal text
    reset_mocks();
    ansi_init(&ctx);
    feed_string(&ctx, "Hello");
    if (mock_putc_calls != 5) {
        printf("FAIL: Normal text putc calls expected 5, got %d\n", mock_putc_calls);
        return false;
    }
    if (mock_last_char != 'o') {
        printf("FAIL: Last char expected 'o', got '%c'\n", mock_last_char);
        return false;
    }

    // Test 2: Color change (Red foreground) -> \x1b[31m
    reset_mocks();
    ansi_init(&ctx);
    feed_string(&ctx, "\x1b[31m");
    if (mock_set_color_calls != 1) {
        printf("FAIL: Color change calls expected 1, got %d\n", mock_set_color_calls);
        return false;
    }
    if (mock_fg != 1) { // 31 - 30 = 1
        printf("FAIL: Color expected 1, got %d\n", mock_fg);
        return false;
    }

    // Test 3: Cursor move (Up) -> \x1b[A
    reset_mocks();
    mock_row = 10; mock_col = 10;
    ansi_init(&ctx);
    feed_string(&ctx, "\x1b[A");
    if (mock_move_cursor_calls != 1) {
        printf("FAIL: Move cursor calls expected 1, got %d\n", mock_move_cursor_calls);
        return false;
    }
    if (mock_row != 9) {
        printf("FAIL: Row expected 9, got %d\n", mock_row);
        return false;
    }

    // Test 4: Clear screen -> \x1b[2J
    reset_mocks();
    ansi_init(&ctx);
    feed_string(&ctx, "\x1b[2J");
    if (mock_clear_screen_calls != 1) {
        printf("FAIL: Clear screen calls expected 1, got %d\n", mock_clear_screen_calls);
        return false;
    }
    if (mock_move_cursor_calls != 1) {
        printf("FAIL: Move cursor calls expected 1 (after clear), got %d\n", mock_move_cursor_calls);
        return false;
    }
    if (mock_row != 0 || mock_col != 0) {
        printf("FAIL: Cursor pos expected 0,0, got %d,%d\n", mock_row, mock_col);
        return false;
    }

    // Test 5: Multiple parameters -> \x1b[12;34H
    reset_mocks();
    ansi_init(&ctx);
    feed_string(&ctx, "\x1b[12;34H");
    if (mock_move_cursor_calls != 1) {
        printf("FAIL: Move cursor calls expected 1, got %d\n", mock_move_cursor_calls);
        return false;
    }
    if (mock_row != 11 || mock_col != 33) { // 0-based index
        printf("FAIL: Row/Col expected 11,33, got %d,%d\n", mock_row, mock_col);
        return false;
    }

    return true;
}
