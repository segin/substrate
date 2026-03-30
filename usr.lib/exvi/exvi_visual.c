#include <exvi.h>
#include "exvi_internal.h"

#include <ctype.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/select.h>

typedef enum {
    VI_REPEAT_NONE = 0,
    VI_REPEAT_X,
    VI_REPEAT_X_BACK,
    VI_REPEAT_R,
    VI_REPEAT_TILDE,
    VI_REPEAT_DD,
    VI_REPEAT_DW,
    VI_REPEAT_D_FIND,
    VI_REPEAT_D_EOL,
    VI_REPEAT_J,
    VI_REPEAT_P,
    VI_REPEAT_P_BEFORE,
} vi_repeat_kind_t;

enum {
    VI_KEY_UP = 0x100,
    VI_KEY_DOWN,
    VI_KEY_RIGHT,
    VI_KEY_LEFT,
    VI_KEY_CTRL_RIGHT,
    VI_KEY_CTRL_LEFT,
    VI_KEY_HOME,
    VI_KEY_END,
    VI_KEY_PGUP,
    VI_KEY_PGDN,
    VI_KEY_DELETE,
    VI_KEY_RESIZE,
};

typedef struct {
    struct termios saved_tio;
    int raw_active;
    int rows;
    int cols;
    int top_line;
    int left_col;
    int cursor_col;
    int pending_g;
    int pending_z;
    int pending_big_z;
    int pending_op;
    int pending_count;
    int pending_reg;
    int last_search_forward;
    int last_find_char;
    int last_find_forward;
    int last_find_till;
    int insert_mode;
    int replace_mode;
    line_t *insert_anchor_line;
    int insert_anchor_col;
    vi_repeat_kind_t last_change;
    int last_change_count;
    int last_change_char;
} vi_visual_t;

static vi_visual_t *active_visual = NULL;
static volatile sig_atomic_t vi_resize_pending = 0;

static int vi_first_nonblank_col(line_t *cur);
static void vi_move_word_forward_count(buffer_t *b, vi_visual_t *vis, int count);
static void vi_move_word_backward_count(buffer_t *b, vi_visual_t *vis, int count);
static void vi_page_scroll(buffer_t *b, vi_visual_t *vis, int direction);
static void vi_delete_char(buffer_t *b, vi_visual_t *vis);
static int vi_prompt_input(buffer_t *b, vi_visual_t *vis, char prefix, char *buf,
    size_t buf_size);
static char *vi_current_word_pattern(buffer_t *b, vi_visual_t *vis);

static void
vi_set_insert_anchor(buffer_t *b, vi_visual_t *vis)
{
    vis->insert_anchor_line = b->cur ? b->cur : b->head;
    vis->insert_anchor_col = vis->cursor_col;
}

static void
vi_restore_terminal(void)
{
    if (active_visual && active_visual->raw_active) {
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &active_visual->saved_tio);
        active_visual->raw_active = 0;
        write(STDOUT_FILENO, "\x1b[?25h\x1b[0m\x1b[2J\x1b[H", 18);
    }
}

static void
vi_handle_winch(int sig)
{
    (void)sig;
    vi_resize_pending = 1;
}

static int
vi_enable_raw(vi_visual_t *vis)
{
    struct termios raw;

    if (tcgetattr(STDIN_FILENO, &vis->saved_tio) != 0) {
        perror("tcgetattr");
        return -1;
    }
    raw = vis->saved_tio;
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw.c_oflag &= ~(OPOST);
    raw.c_cflag |= CS8;
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) != 0) {
        perror("tcsetattr");
        return -1;
    }
    vis->raw_active = 1;
    active_visual = vis;
    atexit(vi_restore_terminal);
    return 0;
}

static void
vi_update_size(vi_visual_t *vis)
{
    struct winsize ws;

    vis->rows = 24;
    vis->cols = 80;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0) {
        if (ws.ws_row > 0) {
            vis->rows = ws.ws_row;
        }
        if (ws.ws_col > 0) {
            vis->cols = ws.ws_col;
        }
    }
    if (vis->rows < 3) {
        vis->rows = 3;
    }
    if (vis->cols < 20) {
        vis->cols = 20;
    }
}

static void
vi_clamp_cursor(buffer_t *b, vi_visual_t *vis)
{
    line_t *cur = b->cur ? b->cur : b->head;

    if (!cur) {
        vis->cursor_col = 0;
        return;
    }
    b->cur = cur;
    if ((size_t)vis->cursor_col > cur->len) {
        vis->cursor_col = (int)cur->len;
    }
    if (vis->cursor_col < 0) {
        vis->cursor_col = 0;
    }
}

static void
vi_clamp_left_col(vi_visual_t *vis)
{
    if (vis->left_col < 0) {
        vis->left_col = 0;
    }
}

static int
vi_char_display_width(unsigned char c, int col)
{
    if (c == '\t') {
        if (option_list) {
            return 2;
        }
        return option_tabstop - (col % option_tabstop);
    }
    if (isprint(c)) {
        return 1;
    }
    return 2;
}

static int
vi_display_col_for_index(line_t *cur, int idx)
{
    int col = 0;
    size_t i;

    if (!cur || idx <= 0) {
        return 0;
    }
    if ((size_t)idx > cur->len) {
        idx = (int)cur->len;
    }
    for (i = 0; i < (size_t)idx; i++) {
        col += vi_char_display_width((unsigned char)cur->text[i], col);
    }
    return col;
}

static int
vi_index_for_display_col(line_t *cur, int target_col)
{
    int col = 0;
    size_t i;

    if (!cur || target_col <= 0) {
        return 0;
    }
    for (i = 0; i < cur->len; i++) {
        int width = vi_char_display_width((unsigned char)cur->text[i], col);

        if (col >= target_col) {
            return (int)i;
        }
        if (col + width > target_col) {
            return (int)i;
        }
        col += width;
    }
    return (int)cur->len;
}

static int
vi_normalize_left_col(line_t *cur, int left_col)
{
    int col = 0;
    size_t i;

    if (!cur || left_col <= 0) {
        return 0;
    }
    for (i = 0; i < cur->len; i++) {
        int width = vi_char_display_width((unsigned char)cur->text[i], col);

        if (col + width > left_col) {
            break;
        }
        col += width;
    }
    return col;
}

static int
vi_next_left_col(line_t *cur, int left_col)
{
    int col = 0;
    size_t i;

    if (!cur || left_col < 0) {
        return 0;
    }
    for (i = 0; i < cur->len; i++) {
        int width = vi_char_display_width((unsigned char)cur->text[i], col);

        if (col >= left_col) {
            return col + width;
        }
        if (col + width > left_col) {
            return col + width;
        }
        col += width;
    }
    return col;
}

static int
vi_text_cols(vi_visual_t *vis)
{
    int cols = vis->cols - (option_number ? 8 : 0);

    if (cols < 1) {
        cols = 1;
    }
    return cols;
}

static void
vi_scroll_into_view(buffer_t *b, vi_visual_t *vis)
{
    line_t *cur = b->cur ? b->cur : b->head;
    int cur_line = buf_current_line(b);
    int cursor_disp;
    int visible_rows = vis->rows - 1;
    int text_cols = vi_text_cols(vis);

    if (cur_line < 1) {
        cur_line = 1;
    }
    if (vis->top_line < 1) {
        vis->top_line = 1;
    }
    if (cur_line < vis->top_line) {
        vis->top_line = cur_line;
    }
    if (cur_line >= vis->top_line + visible_rows) {
        vis->top_line = cur_line - visible_rows + 1;
    }
    if (vis->top_line < 1) {
        vis->top_line = 1;
    }
    vi_clamp_left_col(vis);
    if (!cur) {
        vis->left_col = 0;
        return;
    }
    cursor_disp = vi_display_col_for_index(cur, vis->cursor_col);
    if (cursor_disp < vis->left_col) {
        vis->left_col = cursor_disp;
    }
    if (cursor_disp >= vis->left_col + text_cols) {
        vis->left_col = cursor_disp - text_cols + 1;
    }
    vi_clamp_left_col(vis);
    vis->left_col = vi_normalize_left_col(cur, vis->left_col);
    while (cursor_disp >= vis->left_col + text_cols) {
        int next_left = vi_next_left_col(cur, vis->left_col);

        if (next_left <= vis->left_col) {
            break;
        }
        vis->left_col = next_left;
    }
}

static void
vi_draw_line(const char *text, int cols, int number, int line_no, int left_col)
{
    int used = 0;
    int display_col = 0;

    if (number) {
        used += printf("%6d  ", line_no);
    }
    while (*text && display_col < left_col) {
        display_col += vi_char_display_width((unsigned char)*text, display_col);
        text++;
    }
    while (*text && used < cols) {
        unsigned char c = (unsigned char)*text++;
        int width = vi_char_display_width(c, display_col);

        if (c == '\t') {
            if (option_list) {
                if (used + 1 >= cols) {
                    break;
                }
                putchar('^');
                putchar('I');
                used += 2;
            } else {
                int spaces = width;

                while (spaces-- > 0 && used < cols) {
                    putchar(' ');
                    used++;
                }
            }
        } else if (isprint(c)) {
            putchar((int)c);
            used++;
        } else {
            if (used + 1 >= cols) {
                break;
            }
            putchar('^');
            putchar((c == 127) ? '?' : (char)(c + 64));
            used += 2;
        }
        display_col += width;
    }
}

static void
vi_render(buffer_t *b, vi_visual_t *vis, char prompt_prefix, const char *prompt)
{
    int cur_line = buf_current_line(b);
    int cursor_disp = vi_display_col_for_index(b->cur ? b->cur : b->head, vis->cursor_col);
    int row;
    int cursor_row;
    int cursor_col;

    vi_update_size(vis);
    vi_clamp_cursor(b, vis);
    vi_scroll_into_view(b, vis);

    printf("\x1b[?25l\x1b[H");
    for (row = 0; row < vis->rows - 1; row++) {
        int line_no = vis->top_line + row;
        line_t *line = buf_get_line(b, line_no);

        printf("\x1b[K");
        if (line) {
            vi_draw_line(line->text, vis->cols, option_number, line_no,
                vi_normalize_left_col(line, vis->left_col));
        } else {
            putchar('~');
        }
        if (row < vis->rows - 2) {
            fputs("\r\n", stdout);
        }
    }

    printf("\x1b[K\r\n\x1b[7m");
    if (prompt) {
        printf("%c%s", prompt_prefix, prompt);
    } else if (vis->replace_mode) {
        printf("-- REPLACE --");
    } else if (vis->insert_mode) {
        printf("-- INSERT --");
    } else {
        printf("\"%s\"%s  line %d/%d",
            b->filename ? b->filename : "[No Name]",
            b->modified ? " [Modified]" : "",
            cur_line > 0 ? cur_line : 1,
            b->line_count);
    }
    printf("\x1b[K\x1b[m");

    cursor_row = cur_line - vis->top_line + 1;
    if (cursor_row < 1) {
        cursor_row = 1;
    }
    if (cursor_row > vis->rows - 1) {
        cursor_row = vis->rows - 1;
    }
    cursor_col = (cursor_disp - vis->left_col) + 1 + (option_number ? 8 : 0);
    if (cursor_col < 1 + (option_number ? 8 : 0)) {
        cursor_col = 1 + (option_number ? 8 : 0);
    }
    if (cursor_col > vis->cols) {
        cursor_col = vis->cols;
    }
    printf("\x1b[%d;%dH\x1b[?25h", cursor_row, cursor_col);
    fflush(stdout);
}

static int
vi_read_timed_byte(char *out, int timeout_ms)
{
    fd_set rfds;
    struct timeval tv;

    FD_ZERO(&rfds);
    FD_SET(STDIN_FILENO, &rfds);
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;

    if (select(STDIN_FILENO + 1, &rfds, NULL, NULL, &tv) <= 0) {
        return 0;
    }
    return read(STDIN_FILENO, out, 1) == 1;
}

static int
vi_read_key(void)
{
    char c;

    if (read(STDIN_FILENO, &c, 1) != 1) {
        if (errno == EINTR && vi_resize_pending) {
            return VI_KEY_RESIZE;
        }
        return -1;
    }
    if (c == '\x1b') {
        char seq[2];

        if (!vi_read_timed_byte(&seq[0], 50)) {
            return '\x1b';
        }
        if (!vi_read_timed_byte(&seq[1], 50)) {
            return '\x1b';
        }
        if (seq[0] == '[' || seq[0] == 'O') {
            switch (seq[1]) {
            case 'A': return VI_KEY_UP;
            case 'B': return VI_KEY_DOWN;
            case 'C': return VI_KEY_RIGHT;
            case 'D': return VI_KEY_LEFT;
            case 'H': return VI_KEY_HOME;
            case 'F': return VI_KEY_END;
            default:
                break;
            }
        }
        if (seq[0] == '[' && seq[1] >= '0' && seq[1] <= '9') {
            char seq2;

            if (!vi_read_timed_byte(&seq2, 50)) {
                return '\x1b';
            }
            if (seq2 == ';') {
                char mod;
                char final;

                if (!vi_read_timed_byte(&mod, 50) ||
                    !vi_read_timed_byte(&final, 50)) {
                    return '\x1b';
                }
                if (mod == '5') {
                    switch (final) {
                    case 'C': return VI_KEY_CTRL_RIGHT;
                    case 'D': return VI_KEY_CTRL_LEFT;
                    default: return '\x1b';
                    }
                }
                return '\x1b';
            }
            if (seq2 == '~') {
                switch (seq[1]) {
                case '1':
                case '7':
                    return VI_KEY_HOME;
                case '3':
                    return VI_KEY_DELETE;
                case '4':
                case '8':
                    return VI_KEY_END;
                case '5':
                    return VI_KEY_PGUP;
                case '6':
                    return VI_KEY_PGDN;
                default:
                    return '\x1b';
                }
            }
            if (seq[1] == '5') {
                switch (seq2) {
                case 'C': return VI_KEY_CTRL_RIGHT;
                case 'D': return VI_KEY_CTRL_LEFT;
                default: return '\x1b';
                }
            }
        }
        if (seq[0] == '[') {
            switch (seq[1]) {
            default: return '\x1b';
            }
        }
        return '\x1b';
    }
    return (unsigned char)c;
}

static int
vi_read_visual_key(buffer_t *b, vi_visual_t *vis, char prompt_prefix, const char *prompt)
{
    int key;

    for (;;) {
        key = vi_read_key();
        if (key != VI_KEY_RESIZE) {
            return key;
        }
        vi_resize_pending = 0;
        vi_render(b, vis, prompt_prefix, prompt);
    }
}

static void
vi_append_count(vi_visual_t *vis, int digit)
{
    if (vis->pending_count > 99999999) {
        vis->pending_count = 99999999;
        return;
    }
    vis->pending_count = vis->pending_count * 10 + digit;
}

static int
vi_take_count(vi_visual_t *vis)
{
    int count = vis->pending_count;

    vis->pending_count = 0;
    return count > 0 ? count : 1;
}

static void
vi_set_last_change(vi_visual_t *vis, vi_repeat_kind_t kind, int count, int ch)
{
    vis->last_change = kind;
    vis->last_change_count = count;
    vis->last_change_char = ch;
}

static void
vi_take_register_arg(vi_visual_t *vis, char regarg[2])
{
    if (vis->pending_reg >= 'a' && vis->pending_reg <= 'z') {
        regarg[0] = (char)vis->pending_reg;
        regarg[1] = '\0';
    } else {
        regarg[0] = '\0';
    }
    vis->pending_reg = 0;
}

static int
vi_take_register_index(vi_visual_t *vis)
{
    int idx = 26;

    if (vis->pending_reg >= 'a' && vis->pending_reg <= 'z') {
        idx = vis->pending_reg - 'a';
    }
    vis->pending_reg = 0;
    return idx;
}

static void
vi_store_charwise_range_idx(int reg_idx, line_t *start_line, int start_col,
    line_t *end_line, int end_col)
{
    line_t *line;
    line_t *pos = NULL;
    char *text;
    size_t len;

    if (!start_line || !end_line || start_col < 0 || end_col < 0) {
        return;
    }
    if ((size_t)start_col > start_line->len) {
        start_col = (int)start_line->len;
    }
    if ((size_t)end_col > end_line->len) {
        end_col = (int)end_line->len;
    }

    buf_free(&regs[reg_idx]);
    buf_init(&regs[reg_idx]);
    reg_linewise[reg_idx] = 0;

    if (start_line == end_line) {
        if (end_col < start_col) {
            return;
        }
        text = malloc((size_t)(end_col - start_col) + 1);
        if (!text) {
            return;
        }
        memcpy(text, start_line->text + start_col, (size_t)(end_col - start_col));
        text[end_col - start_col] = '\0';
        buf_insert_after(&regs[reg_idx], NULL, text);
        free(text);
        return;
    }

    len = start_line->len - (size_t)start_col;
    text = malloc(len + 1);
    if (!text) {
        return;
    }
    memcpy(text, start_line->text + start_col, len);
    text[len] = '\0';
    pos = buf_insert_after(&regs[reg_idx], pos, text);
    free(text);

    for (line = start_line->next; line && line != end_line; line = line->next) {
        pos = buf_insert_after(&regs[reg_idx], pos, line->text);
    }

    text = malloc((size_t)end_col + 1);
    if (!text) {
        return;
    }
    memcpy(text, end_line->text, (size_t)end_col);
    text[end_col] = '\0';
    buf_insert_after(&regs[reg_idx], pos, text);
    free(text);
}

static int
vi_compare_positions(buffer_t *b, line_t *left, int left_col, line_t *right,
    int right_col)
{
    line_t *line;

    (void)b;

    if (left == right) {
        if (left_col < right_col) {
            return -1;
        }
        if (left_col > right_col) {
            return 1;
        }
        return 0;
    }

    for (line = left; line; line = line->next) {
        if (line == right) {
            return -1;
        }
    }
    return 1;
}

static void
vi_store_charwise_span(vi_visual_t *vis, line_t *cur, int start, int end)
{
    int reg_idx;

    if (!cur || start < 0 || end < start || (size_t)start > cur->len) {
        return;
    }
    if ((size_t)end > cur->len) {
        end = (int)cur->len;
    }
    reg_idx = vi_take_register_index(vis);
    vi_store_charwise_range_idx(reg_idx, cur, start, cur, end);
}

static int
vi_yank_range(buffer_t *b, vi_visual_t *vis, line_t *start_line, int start_col,
    line_t *end_line, int end_col)
{
    int reg_idx;

    if (!start_line || !end_line) {
        return -1;
    }
    if (vi_compare_positions(b, start_line, start_col, end_line, end_col) > 0) {
        line_t *tmp_line = start_line;
        int tmp_col = start_col;

        start_line = end_line;
        start_col = end_col;
        end_line = tmp_line;
        end_col = tmp_col;
    }
    if (start_line == end_line && end_col <= start_col) {
        return -1;
    }
    reg_idx = vi_take_register_index(vis);
    vi_store_charwise_range_idx(reg_idx, start_line, start_col, end_line, end_col);
    return 0;
}

static int
vi_yank_span(vi_visual_t *vis, line_t *cur, int start, int end)
{
    if (!cur || start < 0 || end <= start || (size_t)start > cur->len) {
        return -1;
    }
    if ((size_t)end > cur->len) {
        end = (int)cur->len;
    }
    if (end <= start) {
        return -1;
    }
    vi_store_charwise_span(vis, cur, start, end);
    return 0;
}

static void
vi_move_vertical(buffer_t *b, int delta)
{
    int cur_line = buf_current_line(b);
    int target = cur_line + delta;

    if (target < 1) {
        target = 1;
    }
    if (target > b->line_count) {
        target = b->line_count;
    }
    if (target >= 1) {
        b->cur = buf_get_line(b, target);
    }
}

static void
vi_move_left_insert(buffer_t *b, vi_visual_t *vis)
{
    line_t *cur = b->cur ? b->cur : b->head;

    if (!cur) {
        return;
    }
    if (vis->cursor_col > 0) {
        vis->cursor_col--;
    } else if (cur->prev) {
        b->cur = cur->prev;
        vis->cursor_col = (int)b->cur->len;
    }
}

static void
vi_move_right_insert(buffer_t *b, vi_visual_t *vis)
{
    line_t *cur = b->cur ? b->cur : b->head;

    if (!cur) {
        return;
    }
    if ((size_t)vis->cursor_col < cur->len) {
        vis->cursor_col++;
    } else if (cur->next) {
        b->cur = cur->next;
        vis->cursor_col = 0;
    }
}

static void
vi_move_arrow_insert(buffer_t *b, vi_visual_t *vis, int key)
{
    line_t *cur = b->cur ? b->cur : b->head;

    switch (key) {
    case VI_KEY_UP:
        vi_move_vertical(b, -1);
        vi_clamp_cursor(b, vis);
        break;
    case VI_KEY_DOWN:
        vi_move_vertical(b, 1);
        vi_clamp_cursor(b, vis);
        break;
    case VI_KEY_LEFT:
        vi_move_left_insert(b, vis);
        break;
    case VI_KEY_RIGHT:
        vi_move_right_insert(b, vis);
        break;
    case VI_KEY_CTRL_LEFT:
        vi_move_word_backward_count(b, vis, 1);
        break;
    case VI_KEY_CTRL_RIGHT:
        vi_move_word_forward_count(b, vis, 1);
        break;
    case VI_KEY_HOME:
        vis->cursor_col = 0;
        break;
    case VI_KEY_END:
        vis->cursor_col = cur ? (int)cur->len : 0;
        break;
    case VI_KEY_PGUP:
        vi_page_scroll(b, vis, -1);
        vi_clamp_cursor(b, vis);
        break;
    case VI_KEY_PGDN:
        vi_page_scroll(b, vis, 1);
        vi_clamp_cursor(b, vis);
        break;
    case VI_KEY_DELETE:
        if (cur && (size_t)vis->cursor_col < cur->len) {
            vi_delete_char(b, vis);
        }
        break;
    default:
        break;
    }
}

static line_t *
vi_ensure_current_line(buffer_t *b)
{
    if (b->cur) {
        return b->cur;
    }
    if (b->head) {
        b->cur = b->head;
        return b->cur;
    }
    b->empty_origin = 1;
    b->cur = buf_insert_after(b, NULL, "");
    return b->cur;
}

static void
vi_ensure_visible_line(buffer_t *b)
{
    int was_modified = b->modified;

    if (b->head) {
        if (!b->cur) {
            b->cur = b->head;
        }
        return;
    }
    b->empty_origin = 1;
    b->cur = buf_insert_after(b, NULL, "");
    b->modified = was_modified;
}

static void
vi_move_line_first_nonblank(buffer_t *b, vi_visual_t *vis, int delta)
{
    vi_move_vertical(b, delta);
    vis->cursor_col = vi_first_nonblank_col(b->cur);
}

static void
vi_page_scroll(buffer_t *b, vi_visual_t *vis, int direction)
{
    int page = vis->rows - 2;

    if (page < 1) {
        page = 1;
    }
    vi_move_vertical(b, direction * page);
}

static void
vi_half_page_scroll(buffer_t *b, vi_visual_t *vis, int direction)
{
    int page = (vis->rows - 2) / 2;

    if (page < 1) {
        page = 1;
    }
    vi_move_vertical(b, direction * page);
}

static int
vi_first_nonblank_col(line_t *cur)
{
    size_t i;

    if (!cur) {
        return 0;
    }
    for (i = 0; i < cur->len; i++) {
        if (cur->text[i] != ' ' && cur->text[i] != '\t') {
            return (int)i;
        }
    }
    return 0;
}

static int
vi_last_nonblank_col(line_t *cur)
{
    int i;

    if (!cur || cur->len == 0) {
        return 0;
    }
    for (i = (int)cur->len - 1; i >= 0; i--) {
        if (cur->text[i] != ' ' && cur->text[i] != '\t') {
            return i;
        }
    }
    return 0;
}

static int
vi_screen_target_line(buffer_t *b, vi_visual_t *vis, int mode, int count)
{
    int visible_rows = vis->rows - 1;
    int screen_row;
    int target_line;

    if (visible_rows < 1) {
        visible_rows = 1;
    }
    switch (mode) {
    case 0:
        screen_row = (count > 0) ? count - 1 : 0;
        break;
    case 1:
        screen_row = (visible_rows - 1) / 2;
        break;
    case 2:
        screen_row = (count > 0) ? visible_rows - count : visible_rows - 1;
        break;
    default:
        screen_row = 0;
        break;
    }
    if (screen_row < 0) {
        screen_row = 0;
    }
    if (screen_row >= visible_rows) {
        screen_row = visible_rows - 1;
    }
    target_line = vis->top_line + screen_row;
    if (target_line < 1) {
        target_line = 1;
    }
    if (target_line > b->line_count) {
        target_line = b->line_count;
    }
    return target_line;
}

static int
vi_clamp_top_line(buffer_t *b, vi_visual_t *vis, int top_line)
{
    int visible_rows = vis->rows - 1;
    int max_top = b->line_count - visible_rows + 1;

    if (visible_rows < 1) {
        visible_rows = 1;
    }
    if (max_top < 1) {
        max_top = 1;
    }
    if (top_line < 1) {
        top_line = 1;
    }
    if (top_line > max_top) {
        top_line = max_top;
    }
    return top_line;
}

static void
vi_reposition_current(buffer_t *b, vi_visual_t *vis, int mode)
{
    int cur_line = buf_current_line(b);
    int visible_rows = vis->rows - 1;
    int top_line;

    if (cur_line < 1) {
        return;
    }
    if (visible_rows < 1) {
        visible_rows = 1;
    }
    switch (mode) {
    case 0:
        top_line = cur_line - (visible_rows / 2);
        break;
    case 1:
        top_line = cur_line;
        break;
    default:
        top_line = cur_line - visible_rows + 1;
        break;
    }
    vis->top_line = vi_clamp_top_line(b, vis, top_line);
}

static int
vi_line_is_blank(line_t *cur)
{
    size_t i;

    if (!cur) {
        return 1;
    }
    for (i = 0; i < cur->len; i++) {
        if (cur->text[i] != ' ' && cur->text[i] != '\t') {
            return 0;
        }
    }
    return 1;
}

static void
vi_move_paragraph_forward(buffer_t *b, vi_visual_t *vis, int count)
{
    int line_no = buf_current_line(b);

    if (line_no < 1) {
        return;
    }
    while (count-- > 0) {
        line_no++;
        while (line_no <= b->line_count &&
            !vi_line_is_blank(buf_get_line(b, line_no))) {
            line_no++;
        }
        while (line_no <= b->line_count &&
            vi_line_is_blank(buf_get_line(b, line_no))) {
            line_no++;
        }
        if (line_no > b->line_count) {
            line_no = b->line_count;
            break;
        }
    }
    b->cur = buf_get_line(b, line_no);
    vis->cursor_col = vi_first_nonblank_col(b->cur);
}

static void
vi_move_paragraph_backward(buffer_t *b, vi_visual_t *vis, int count)
{
    int line_no = buf_current_line(b);

    if (line_no < 1) {
        return;
    }
    while (count-- > 0) {
        line_no--;
        while (line_no >= 1 && vi_line_is_blank(buf_get_line(b, line_no))) {
            line_no--;
        }
        while (line_no >= 1 && !vi_line_is_blank(buf_get_line(b, line_no))) {
            line_no--;
        }
        line_no++;
        if (line_no < 1) {
            line_no = 1;
            break;
        }
    }
    b->cur = buf_get_line(b, line_no);
    vis->cursor_col = vi_first_nonblank_col(b->cur);
}

static int
vi_is_sentence_end_char(int ch)
{
    return ch == '.' || ch == '!' || ch == '?';
}

static int
vi_is_sentence_closer(int ch)
{
    return ch == ')' || ch == ']' || ch == '"' || ch == '\'';
}

static int
vi_is_sentence_end_at(line_t *line, int col)
{
    size_t i;

    if (!line || col < 0 || (size_t)col >= line->len ||
        !vi_is_sentence_end_char((unsigned char)line->text[col])) {
        return 0;
    }
    i = (size_t)col + 1;
    while (i < line->len && vi_is_sentence_closer((unsigned char)line->text[i])) {
        i++;
    }
    return i >= line->len || isspace((unsigned char)line->text[i]);
}

static int
vi_find_first_nonblank(buffer_t *b, int *line_out, int *col_out)
{
    int line_no;

    for (line_no = 1; line_no <= b->line_count; line_no++) {
        line_t *line = buf_get_line(b, line_no);
        int col = vi_first_nonblank_col(line);

        if (line && (line->len > 0 || col == 0) && !vi_line_is_blank(line)) {
            *line_out = line_no;
            *col_out = col;
            return 0;
        }
    }
    return -1;
}

static int
vi_find_sentence_start_after(buffer_t *b, int line_no, int col, int *line_out, int *col_out)
{
    int ln;

    if (line_no < 1) {
        return -1;
    }
    for (ln = line_no; ln <= b->line_count; ln++) {
        line_t *line = buf_get_line(b, ln);
        size_t i = (ln == line_no) ? (size_t)col + 1 : 0;

        if (!line) {
            continue;
        }
        while (i < line->len && isspace((unsigned char)line->text[i])) {
            i++;
        }
        if (i < line->len) {
            *line_out = ln;
            *col_out = (int)i;
            return 0;
        }
    }
    return -1;
}

static void
vi_move_sentence_forward(buffer_t *b, vi_visual_t *vis, int count)
{
    int cur_line = buf_current_line(b);
    int cur_col = vis->cursor_col;

    if (cur_line < 1) {
        return;
    }
    while (count-- > 0) {
        int ln;
        int found = 0;

        for (ln = cur_line; ln <= b->line_count && !found; ln++) {
            line_t *line = buf_get_line(b, ln);
            int start = (ln == cur_line) ? cur_col + 1 : 0;
            int col;

            if (!line) {
                continue;
            }
            for (col = start; (size_t)col < line->len; col++) {
                if (vi_is_sentence_end_at(line, col) &&
                    vi_find_sentence_start_after(b, ln, col, &cur_line, &cur_col) == 0) {
                    found = 1;
                    break;
                }
            }
        }
        if (!found) {
            return;
        }
    }
    b->cur = buf_get_line(b, cur_line);
    vis->cursor_col = cur_col;
}

static void
vi_move_sentence_backward(buffer_t *b, vi_visual_t *vis, int count)
{
    int cur_line = buf_current_line(b);
    int cur_col = vis->cursor_col;

    if (cur_line < 1) {
        return;
    }
    while (count-- > 0) {
        int ln;
        int found = 0;

        for (ln = cur_line; ln >= 1 && !found; ln--) {
            line_t *line = buf_get_line(b, ln);
            int start = (ln == cur_line) ? cur_col - 1 : (line ? (int)line->len - 1 : -1);
            int col;

            if (!line) {
                continue;
            }
            for (col = start; col >= 0; col--) {
                int target_line;
                int target_col;

                if (!vi_is_sentence_end_at(line, col)) {
                    continue;
                }
                if (vi_find_sentence_start_after(b, ln, col, &target_line, &target_col) == 0 &&
                    (target_line < cur_line ||
                    (target_line == cur_line && target_col < cur_col))) {
                    cur_line = target_line;
                    cur_col = target_col;
                    found = 1;
                    break;
                }
            }
        }
        if (!found) {
            if (vi_find_first_nonblank(b, &cur_line, &cur_col) != 0) {
                return;
            }
        }
    }
    b->cur = buf_get_line(b, cur_line);
    vis->cursor_col = cur_col;
}

static int
vi_is_word_char(int ch)
{
    return isalnum((unsigned char)ch) || ch == '_';
}

static int
vi_is_bigword_char(int ch)
{
    return ch != '\0' && !isspace((unsigned char)ch);
}

static int
vi_find_word_boundary_forward(line_t *cur, int start, int *end_out)
{
    size_t i;

    if (!cur || start < 0 || (size_t)start > cur->len) {
        return -1;
    }
    i = (size_t)start;
    if (i < cur->len && vi_is_word_char((unsigned char)cur->text[i])) {
        while (i < cur->len && vi_is_word_char((unsigned char)cur->text[i])) {
            i++;
        }
        while (i < cur->len && !vi_is_word_char((unsigned char)cur->text[i])) {
            i++;
        }
    } else {
        while (i < cur->len && !vi_is_word_char((unsigned char)cur->text[i])) {
            i++;
        }
        while (i < cur->len && vi_is_word_char((unsigned char)cur->text[i])) {
            i++;
        }
    }
    *end_out = (int)i;
    return 0;
}

static void
vi_find_word_boundary_forward_count(line_t *cur, int start, int count, int *end_out)
{
    int end = start;

    while (count-- > 0) {
        if (vi_find_word_boundary_forward(cur, end, &end) != 0) {
            break;
        }
    }
    *end_out = end;
}

static void
vi_move_word_forward(buffer_t *b, vi_visual_t *vis)
{
    int line_no = buf_current_line(b);
    line_t *cur = b->cur;
    size_t i;

    if (!cur) {
        return;
    }
    i = (size_t)vis->cursor_col;
    if (i < cur->len && vi_is_word_char((unsigned char)cur->text[i])) {
        while (i < cur->len && vi_is_word_char((unsigned char)cur->text[i])) {
            i++;
        }
    }
    while (i < cur->len && !vi_is_word_char((unsigned char)cur->text[i])) {
        i++;
    }
    if (i < cur->len) {
        vis->cursor_col = (int)i;
        return;
    }

    for (line_no++; line_no <= b->line_count; line_no++) {
        cur = buf_get_line(b, line_no);
        if (!cur) {
            break;
        }
        for (i = 0; i < cur->len; i++) {
            if (vi_is_word_char((unsigned char)cur->text[i])) {
                b->cur = cur;
                vis->cursor_col = (int)i;
                return;
            }
        }
    }
    if (b->tail) {
        b->cur = b->tail;
        vis->cursor_col = (int)b->tail->len;
    }
}

static void
vi_move_word_forward_count(buffer_t *b, vi_visual_t *vis, int count)
{
    while (count-- > 0) {
        vi_move_word_forward(b, vis);
    }
}

static void
vi_move_word_backward(buffer_t *b, vi_visual_t *vis)
{
    int line_no = buf_current_line(b);
    line_t *cur = b->cur;
    int i;

    if (!cur) {
        return;
    }
    i = vis->cursor_col;
    if (i > 0) {
        i--;
    }
    while (i >= 0 && !vi_is_word_char((unsigned char)cur->text[i])) {
        i--;
    }
    while (i > 0 && vi_is_word_char((unsigned char)cur->text[i - 1])) {
        i--;
    }
    if (i >= 0 && vi_is_word_char((unsigned char)cur->text[i])) {
        vis->cursor_col = i;
        return;
    }

    for (line_no--; line_no >= 1; line_no--) {
        cur = buf_get_line(b, line_no);
        if (!cur) {
            continue;
        }
        for (i = (int)cur->len - 1; i >= 0; i--) {
            if (vi_is_word_char((unsigned char)cur->text[i])) {
                while (i > 0 && vi_is_word_char((unsigned char)cur->text[i - 1])) {
                    i--;
                }
                b->cur = cur;
                vis->cursor_col = i;
                return;
            }
        }
    }
    if (b->head) {
        b->cur = b->head;
        vis->cursor_col = 0;
    }
}

static void
vi_move_word_backward_count(buffer_t *b, vi_visual_t *vis, int count)
{
    while (count-- > 0) {
        vi_move_word_backward(b, vis);
    }
}

static void
vi_move_word_end(buffer_t *b, vi_visual_t *vis)
{
    int line_no = buf_current_line(b);
    line_t *cur = b->cur;
    size_t i;

    if (!cur) {
        return;
    }
    i = (size_t)vis->cursor_col;
    while (i < cur->len && !vi_is_word_char((unsigned char)cur->text[i])) {
        i++;
    }
    if (i < cur->len) {
        while (i + 1 < cur->len && vi_is_word_char((unsigned char)cur->text[i + 1])) {
            i++;
        }
        vis->cursor_col = (int)i;
        return;
    }

    for (line_no++; line_no <= b->line_count; line_no++) {
        cur = buf_get_line(b, line_no);
        if (!cur) {
            break;
        }
        for (i = 0; i < cur->len; i++) {
            if (vi_is_word_char((unsigned char)cur->text[i])) {
                while (i + 1 < cur->len && vi_is_word_char((unsigned char)cur->text[i + 1])) {
                    i++;
                }
                b->cur = cur;
                vis->cursor_col = (int)i;
                return;
            }
        }
    }
}

static void
vi_move_word_end_backward(buffer_t *b, vi_visual_t *vis)
{
    int line_no = buf_current_line(b);
    line_t *cur = b->cur;
    int i;
    int skip_current = 0;

    if (!cur) {
        return;
    }
    if ((size_t)vis->cursor_col < cur->len &&
        vi_is_word_char((unsigned char)cur->text[vis->cursor_col])) {
        skip_current = 1;
    }
    i = vis->cursor_col - 1;
    for (;;) {
        if (!cur) {
            return;
        }
        if (i >= (int)cur->len) {
            i = (int)cur->len - 1;
        }
        if (skip_current) {
            while (i >= 0 && vi_is_word_char((unsigned char)cur->text[i])) {
                i--;
            }
            skip_current = 0;
        }
        while (i >= 0 && !vi_is_word_char((unsigned char)cur->text[i])) {
            i--;
        }
        if (i >= 0) {
            b->cur = cur;
            vis->cursor_col = i;
            return;
        }
        line_no--;
        if (line_no < 1) {
            return;
        }
        cur = buf_get_line(b, line_no);
        i = cur ? (int)cur->len - 1 : -1;
    }
}

static void
vi_move_word_end_backward_count(buffer_t *b, vi_visual_t *vis, int count)
{
    while (count-- > 0) {
        vi_move_word_end_backward(b, vis);
    }
}

static int
vi_find_bigword_boundary_forward(line_t *cur, int start, int *end_out)
{
    size_t i;

    if (!cur || start < 0 || (size_t)start > cur->len) {
        return -1;
    }
    i = (size_t)start;
    if (i < cur->len && vi_is_bigword_char((unsigned char)cur->text[i])) {
        while (i < cur->len && vi_is_bigword_char((unsigned char)cur->text[i])) {
            i++;
        }
        while (i < cur->len && !vi_is_bigword_char((unsigned char)cur->text[i])) {
            i++;
        }
    } else {
        while (i < cur->len && !vi_is_bigword_char((unsigned char)cur->text[i])) {
            i++;
        }
        while (i < cur->len && vi_is_bigword_char((unsigned char)cur->text[i])) {
            i++;
        }
    }
    *end_out = (int)i;
    return 0;
}

static void
vi_find_bigword_boundary_forward_count(line_t *cur, int start, int count, int *end_out)
{
    int end = start;

    while (count-- > 0) {
        if (vi_find_bigword_boundary_forward(cur, end, &end) != 0) {
            break;
        }
    }
    *end_out = end;
}

static void
vi_move_bigword_forward(buffer_t *b, vi_visual_t *vis)
{
    int line_no = buf_current_line(b);
    line_t *cur = b->cur;
    size_t i;

    if (!cur) {
        return;
    }
    i = (size_t)vis->cursor_col;
    if (i < cur->len && vi_is_bigword_char((unsigned char)cur->text[i])) {
        while (i < cur->len && vi_is_bigword_char((unsigned char)cur->text[i])) {
            i++;
        }
    }
    while (i < cur->len && !vi_is_bigword_char((unsigned char)cur->text[i])) {
        i++;
    }
    if (i < cur->len) {
        vis->cursor_col = (int)i;
        return;
    }

    for (line_no++; line_no <= b->line_count; line_no++) {
        cur = buf_get_line(b, line_no);
        if (!cur) {
            break;
        }
        for (i = 0; i < cur->len; i++) {
            if (vi_is_bigword_char((unsigned char)cur->text[i])) {
                b->cur = cur;
                vis->cursor_col = (int)i;
                return;
            }
        }
    }
    if (b->tail) {
        b->cur = b->tail;
        vis->cursor_col = (int)b->tail->len;
    }
}

static void
vi_move_bigword_forward_count(buffer_t *b, vi_visual_t *vis, int count)
{
    while (count-- > 0) {
        vi_move_bigword_forward(b, vis);
    }
}

static void
vi_move_bigword_backward(buffer_t *b, vi_visual_t *vis)
{
    int line_no = buf_current_line(b);
    line_t *cur = b->cur;
    int i;

    if (!cur) {
        return;
    }
    i = vis->cursor_col;
    if (i > 0) {
        i--;
    }
    while (i >= 0 && !vi_is_bigword_char((unsigned char)cur->text[i])) {
        i--;
    }
    while (i > 0 && vi_is_bigword_char((unsigned char)cur->text[i - 1])) {
        i--;
    }
    if (i >= 0 && vi_is_bigword_char((unsigned char)cur->text[i])) {
        vis->cursor_col = i;
        return;
    }

    for (line_no--; line_no >= 1; line_no--) {
        cur = buf_get_line(b, line_no);
        if (!cur) {
            continue;
        }
        for (i = (int)cur->len - 1; i >= 0; i--) {
            if (vi_is_bigword_char((unsigned char)cur->text[i])) {
                while (i > 0 && vi_is_bigword_char((unsigned char)cur->text[i - 1])) {
                    i--;
                }
                b->cur = cur;
                vis->cursor_col = i;
                return;
            }
        }
    }
    if (b->head) {
        b->cur = b->head;
        vis->cursor_col = 0;
    }
}

static void
vi_move_bigword_backward_count(buffer_t *b, vi_visual_t *vis, int count)
{
    while (count-- > 0) {
        vi_move_bigword_backward(b, vis);
    }
}

static void
vi_move_bigword_end(buffer_t *b, vi_visual_t *vis)
{
    int line_no = buf_current_line(b);
    line_t *cur = b->cur;
    size_t i;

    if (!cur) {
        return;
    }
    i = (size_t)vis->cursor_col;
    while (i < cur->len && !vi_is_bigword_char((unsigned char)cur->text[i])) {
        i++;
    }
    if (i < cur->len) {
        while (i + 1 < cur->len && vi_is_bigword_char((unsigned char)cur->text[i + 1])) {
            i++;
        }
        vis->cursor_col = (int)i;
        return;
    }

    for (line_no++; line_no <= b->line_count; line_no++) {
        cur = buf_get_line(b, line_no);
        if (!cur) {
            break;
        }
        for (i = 0; i < cur->len; i++) {
            if (vi_is_bigword_char((unsigned char)cur->text[i])) {
                while (i + 1 < cur->len &&
                    vi_is_bigword_char((unsigned char)cur->text[i + 1])) {
                    i++;
                }
                b->cur = cur;
                vis->cursor_col = (int)i;
                return;
            }
        }
    }
}

static void
vi_move_bigword_end_backward(buffer_t *b, vi_visual_t *vis)
{
    int line_no = buf_current_line(b);
    line_t *cur = b->cur;
    int i;
    int skip_current = 0;

    if (!cur) {
        return;
    }
    if ((size_t)vis->cursor_col < cur->len &&
        vi_is_bigword_char((unsigned char)cur->text[vis->cursor_col])) {
        skip_current = 1;
    }
    i = vis->cursor_col - 1;
    for (;;) {
        if (!cur) {
            return;
        }
        if (i >= (int)cur->len) {
            i = (int)cur->len - 1;
        }
        if (skip_current) {
            while (i >= 0 && vi_is_bigword_char((unsigned char)cur->text[i])) {
                i--;
            }
            skip_current = 0;
        }
        while (i >= 0 && !vi_is_bigword_char((unsigned char)cur->text[i])) {
            i--;
        }
        if (i >= 0) {
            b->cur = cur;
            vis->cursor_col = i;
            return;
        }
        line_no--;
        if (line_no < 1) {
            return;
        }
        cur = buf_get_line(b, line_no);
        i = cur ? (int)cur->len - 1 : -1;
    }
}

static void
vi_move_bigword_end_backward_count(buffer_t *b, vi_visual_t *vis, int count)
{
    while (count-- > 0) {
        vi_move_bigword_end_backward(b, vis);
    }
}

static int
vi_find_bigword_end_exclusive_count(line_t *cur, int start, int count, int *end_out)
{
    size_t i;
    int end;

    if (!cur || start < 0 || (size_t)start >= cur->len) {
        return -1;
    }
    end = start;
    while (count-- > 0) {
        i = (size_t)end;
        while (i < cur->len && !vi_is_bigword_char((unsigned char)cur->text[i])) {
            i++;
        }
        if (i >= cur->len) {
            return -1;
        }
        while (i + 1 < cur->len && vi_is_bigword_char((unsigned char)cur->text[i + 1])) {
            i++;
        }
        end = (int)i + 1;
    }
    *end_out = end;
    return 0;
}

static int
vi_find_bigword_start_backward_count(line_t *cur, int start, int count, int *start_out)
{
    int i;

    if (!cur || start <= 0 || (size_t)start > cur->len) {
        return -1;
    }
    i = start - 1;
    while (count-- > 0) {
        while (i >= 0 && !vi_is_bigword_char((unsigned char)cur->text[i])) {
            i--;
        }
        if (i < 0) {
            return -1;
        }
        while (i > 0 && vi_is_bigword_char((unsigned char)cur->text[i - 1])) {
            i--;
        }
        if (count > 0) {
            i--;
        }
    }
    *start_out = i;
    return 0;
}

static int
vi_find_char_in_line(line_t *cur, int start, int ch, int forward, int count, int *col_out)
{
    int i;

    if (!cur || count < 1) {
        return -1;
    }
    if (forward) {
        for (i = start + 1; (size_t)i < cur->len; i++) {
            if ((unsigned char)cur->text[i] == (unsigned char)ch && --count == 0) {
                *col_out = i;
                return 0;
            }
        }
    } else {
        for (i = start - 1; i >= 0; i--) {
            if ((unsigned char)cur->text[i] == (unsigned char)ch && --count == 0) {
                *col_out = i;
                return 0;
            }
        }
    }
    return -1;
}

static void
vi_find_char_motion(buffer_t *b, vi_visual_t *vis, int ch, int forward, int till, int count)
{
    int col;

    if (!b->cur || vi_find_char_in_line(b->cur, vis->cursor_col, ch, forward, count, &col) != 0) {
        write(STDOUT_FILENO, "\a", 1);
        return;
    }
    if (till) {
        if (forward && col > 0) {
            col--;
        } else if (!forward) {
            col++;
        }
    }
    vis->cursor_col = col;
    vis->last_find_char = ch;
    vis->last_find_forward = forward;
    vis->last_find_till = till;
}

static void
vi_repeat_find_motion(buffer_t *b, vi_visual_t *vis, int reverse, int count)
{
    int forward = vis->last_find_forward;

    if (!vis->last_find_char) {
        write(STDOUT_FILENO, "\a", 1);
        return;
    }
    if (reverse) {
        forward = !forward;
    }
    vi_find_char_motion(b, vis, vis->last_find_char, forward, vis->last_find_till, count);
}

static int
vi_match_bracket(int ch, int *forward_out, int *match_out)
{
    switch (ch) {
    case '(':
        *forward_out = 1;
        *match_out = ')';
        return 0;
    case '[':
        *forward_out = 1;
        *match_out = ']';
        return 0;
    case '{':
        *forward_out = 1;
        *match_out = '}';
        return 0;
    case ')':
        *forward_out = 0;
        *match_out = '(';
        return 0;
    case ']':
        *forward_out = 0;
        *match_out = '[';
        return 0;
    case '}':
        *forward_out = 0;
        *match_out = '{';
        return 0;
    default:
        return -1;
    }
}

static int
vi_match_motion_target(buffer_t *b, vi_visual_t *vis, line_t **line_out, int *col_out)
{
    line_t *line = b->cur;
    int open_ch;
    int match_ch;
    int forward;
    int depth = 1;

    if (!line || vis->cursor_col < 0 || (size_t)vis->cursor_col >= line->len) {
        return -1;
    }
    open_ch = (unsigned char)line->text[vis->cursor_col];
    if (vi_match_bracket(open_ch, &forward, &match_ch) != 0) {
        return -1;
    }

    if (forward) {
        size_t i;

        for (i = (size_t)vis->cursor_col + 1; i < line->len; i++) {
            if ((unsigned char)line->text[i] == (unsigned char)open_ch) {
                depth++;
            } else if ((unsigned char)line->text[i] == (unsigned char)match_ch &&
                --depth == 0) {
                *line_out = line;
                *col_out = (int)i;
                return 0;
            }
        }
        for (line = line->next; line; line = line->next) {
            for (i = 0; i < line->len; i++) {
                if ((unsigned char)line->text[i] == (unsigned char)open_ch) {
                    depth++;
                } else if ((unsigned char)line->text[i] == (unsigned char)match_ch &&
                    --depth == 0) {
                    *line_out = line;
                    *col_out = (int)i;
                    return 0;
                }
            }
        }
    } else {
        int i;

        for (i = vis->cursor_col - 1; i >= 0; i--) {
            if ((unsigned char)line->text[i] == (unsigned char)open_ch) {
                depth++;
            } else if ((unsigned char)line->text[i] == (unsigned char)match_ch &&
                --depth == 0) {
                *line_out = line;
                *col_out = i;
                return 0;
            }
        }
        for (line = line->prev; line; line = line->prev) {
            for (i = (int)line->len - 1; i >= 0; i--) {
                if ((unsigned char)line->text[i] == (unsigned char)open_ch) {
                    depth++;
                } else if ((unsigned char)line->text[i] == (unsigned char)match_ch &&
                    --depth == 0) {
                    *line_out = line;
                    *col_out = i;
                    return 0;
                }
            }
        }
    }

    return -1;
}

static void
vi_match_motion(buffer_t *b, vi_visual_t *vis)
{
    line_t *target_line;
    int target_col;

    if (vi_match_motion_target(b, vis, &target_line, &target_col) != 0) {
        write(STDOUT_FILENO, "\a", 1);
        return;
    }
    b->cur = target_line;
    vis->cursor_col = target_col;
}

static int
vi_find_motion_span(line_t *cur, int cursor_col, int ch, int forward, int till,
    int count, int *start_out, int *end_out)
{
    int col;

    if (!cur || vi_find_char_in_line(cur, cursor_col, ch, forward, count, &col) != 0) {
        return -1;
    }
    if (forward) {
        *start_out = cursor_col;
        *end_out = till ? col : col + 1;
    } else {
        *start_out = till ? col + 1 : col;
        *end_out = cursor_col + 1;
    }
    if (*end_out <= *start_out) {
        return -1;
    }
    return 0;
}

static int
vi_find_word_end_exclusive_count(line_t *cur, int start, int count, int *end_out)
{
    size_t i;
    int end;

    if (!cur || start < 0 || (size_t)start >= cur->len) {
        return -1;
    }
    end = start;
    while (count-- > 0) {
        i = (size_t)end;
        while (i < cur->len && !vi_is_word_char((unsigned char)cur->text[i])) {
            i++;
        }
        if (i >= cur->len) {
            return -1;
        }
        while (i + 1 < cur->len && vi_is_word_char((unsigned char)cur->text[i + 1])) {
            i++;
        }
        end = (int)i + 1;
    }
    *end_out = end;
    return 0;
}

static int
vi_find_word_start_backward_count(line_t *cur, int start, int count, int *start_out)
{
    int i;

    if (!cur || start <= 0 || (size_t)start > cur->len) {
        return -1;
    }
    i = start - 1;
    while (count-- > 0) {
        while (i >= 0 && !vi_is_word_char((unsigned char)cur->text[i])) {
            i--;
        }
        if (i < 0) {
            return -1;
        }
        while (i > 0 && vi_is_word_char((unsigned char)cur->text[i - 1])) {
            i--;
        }
        if (count > 0) {
            i--;
        }
    }
    *start_out = i;
    return 0;
}

static int
vi_clamp_line_target(buffer_t *b, int line_no)
{
    if (line_no < 1) {
        return 1;
    }
    if (line_no > b->line_count) {
        return b->line_count;
    }
    return line_no;
}

static void
vi_move_to_eol_count(buffer_t *b, vi_visual_t *vis, int count)
{
    int target;

    if (count < 1) {
        count = 1;
    }
    target = vi_clamp_line_target(b, buf_current_line(b) + count - 1);
    if (target < 1) {
        return;
    }
    b->cur = buf_get_line(b, target);
    if (!b->cur) {
        vis->cursor_col = 0;
        return;
    }
    vis->cursor_col = (b->cur->len > 0) ? (int)b->cur->len - 1 : 0;
}

static int
vi_eol_motion_target(buffer_t *b, int count, line_t **line_out, int *col_out)
{
    int target;
    line_t *line;

    if (count < 1) {
        count = 1;
    }
    target = vi_clamp_line_target(b, buf_current_line(b) + count - 1);
    if (target < 1) {
        return -1;
    }
    line = buf_get_line(b, target);
    if (!line) {
        return -1;
    }
    *line_out = line;
    *col_out = (line->len > 0) ? (int)line->len - 1 : 0;
    return 0;
}

static void
vi_move_to_percent(buffer_t *b, vi_visual_t *vis, int percent)
{
    int target;

    if (b->line_count < 1) {
        return;
    }
    if (percent < 1) {
        percent = 1;
    }
    if (percent > 100) {
        percent = 100;
    }
    target = (percent * b->line_count + 99) / 100;
    if (target < 1) {
        target = 1;
    }
    if (target > b->line_count) {
        target = b->line_count;
    }
    b->cur = buf_get_line(b, target);
    vis->cursor_col = vi_first_nonblank_col(b->cur);
}

static int
vi_replace_current_text(buffer_t *b, const char *text)
{
    line_t *cur = b->cur;
    char *copy;

    if (!cur) {
        return -1;
    }
    copy = strdup(text);
    if (!copy) {
        return -1;
    }
    free(cur->text);
    cur->text = copy;
    cur->len = strlen(copy);
    b->modified = 1;
    return 0;
}

static void
vi_delete_span(buffer_t *b, vi_visual_t *vis, int start, int end, int enter_insert)
{
    line_t *cur = b->cur;
    char *text;

    if (!cur || start < 0 || end < start || (size_t)start > cur->len) {
        return;
    }
    if ((size_t)end > cur->len) {
        end = (int)cur->len;
    }
    vi_store_charwise_span(vis, cur, start, end);
    save_undo(b);
    text = malloc(cur->len - (size_t)(end - start) + 1);
    if (!text) {
        return;
    }
    memcpy(text, cur->text, (size_t)start);
    memcpy(text + start, cur->text + end, cur->len - (size_t)end + 1);
    if (vi_replace_current_text(b, text) == 0) {
        vis->cursor_col = start;
        vis->insert_mode = enter_insert;
        vis->replace_mode = 0;
        if (enter_insert) {
            vi_set_insert_anchor(b, vis);
        }
    }
    free(text);
}

static void
vi_delete_range(buffer_t *b, vi_visual_t *vis, line_t *start_line, int start_col,
    line_t *end_line, int end_col, int enter_insert)
{
    char *text;
    size_t prefix_len;
    size_t suffix_len;

    if (!start_line || !end_line) {
        return;
    }
    if (vi_compare_positions(b, start_line, start_col, end_line, end_col) > 0) {
        line_t *tmp_line = start_line;
        int tmp_col = start_col;

        start_line = end_line;
        start_col = end_col;
        end_line = tmp_line;
        end_col = tmp_col;
    }
    if (start_line == end_line) {
        b->cur = start_line;
        vi_delete_span(b, vis, start_col, end_col, enter_insert);
        return;
    }
    if ((size_t)start_col > start_line->len) {
        start_col = (int)start_line->len;
    }
    if ((size_t)end_col > end_line->len) {
        end_col = (int)end_line->len;
    }

    vi_yank_range(b, vis, start_line, start_col, end_line, end_col);
    save_undo(b);

    prefix_len = (size_t)start_col;
    suffix_len = end_line->len - (size_t)end_col;
    text = malloc(prefix_len + suffix_len + 1);
    if (!text) {
        return;
    }
    memcpy(text, start_line->text, prefix_len);
    memcpy(text + prefix_len, end_line->text + end_col, suffix_len);
    text[prefix_len + suffix_len] = '\0';

    b->cur = start_line;
    if (vi_replace_current_text(b, text) == 0) {
        line_t *line = start_line->next;

        while (line) {
            line_t *next = line->next;
            int done = (line == end_line);

            buf_delete(b, line);
            if (done) {
                break;
            }
            line = next;
        }
        vis->cursor_col = start_col;
        vis->insert_mode = enter_insert;
        vis->replace_mode = 0;
        if (enter_insert) {
            vi_set_insert_anchor(b, vis);
        }
    }
    free(text);
}

static void
vi_put_from_register(buffer_t *b, vi_visual_t *vis, int before, int reg_idx, int count)
{
    int line_no = buf_current_line(b);
    char regarg[2];

    if (count < 1) {
        count = 1;
    }
    if (line_no < 1) {
        line_no = 1;
    }
    if (!reg_linewise[reg_idx]) {
        save_undo(b);
        while (count-- > 0) {
            line_t *cur = b->cur;
            line_t *src = regs[reg_idx].head;
            int pos;

            if (!cur || !src || !src->text) {
                return;
            }
            if (!before && (size_t)vis->cursor_col < cur->len) {
                pos = vis->cursor_col + 1;
            } else {
                pos = vis->cursor_col;
            }
            if (pos < 0) {
                pos = 0;
            }
            if ((size_t)pos > cur->len) {
                pos = (int)cur->len;
            }

            if (regs[reg_idx].line_count == 1) {
                char *text = malloc(cur->len + src->len + 1);

                if (!text) {
                    return;
                }
                memcpy(text, cur->text, (size_t)pos);
                memcpy(text + pos, src->text, src->len);
                memcpy(text + pos + src->len, cur->text + pos,
                    cur->len - (size_t)pos + 1);
                if (vi_replace_current_text(b, text) == 0) {
                    vis->cursor_col = pos;
                }
                free(text);
            } else {
                char *suffix = strdup(cur->text + pos);
                char *text;
                line_t *dst;

                if (!suffix) {
                    return;
                }
                text = malloc((size_t)pos + src->len + 1);
                if (!text) {
                    free(suffix);
                    return;
                }
                memcpy(text, cur->text, (size_t)pos);
                memcpy(text + pos, src->text, src->len);
                text[pos + (int)src->len] = '\0';
                if (vi_replace_current_text(b, text) != 0) {
                    free(text);
                    free(suffix);
                    return;
                }
                free(text);

                dst = cur;
                for (src = src->next; src && src->next; src = src->next) {
                    dst = buf_insert_after(b, dst, src->text);
                    if (!dst) {
                        free(suffix);
                        return;
                    }
                }

                text = malloc(src->len + strlen(suffix) + 1);
                if (!text) {
                    free(suffix);
                    return;
                }
                memcpy(text, src->text, src->len);
                memcpy(text + src->len, suffix, strlen(suffix) + 1);
                dst = buf_insert_after(b, dst, text);
                free(text);
                free(suffix);
                if (!dst) {
                    return;
                }
                b->cur = dst;
                vis->cursor_col = (src->len > 0) ? (int)src->len - 1 : 0;
            }
        }
        return;
    }
    if (reg_idx >= 0 && reg_idx < 26) {
        regarg[0] = (char)('a' + reg_idx);
        regarg[1] = '\0';
    } else {
        regarg[0] = '\0';
    }
    while (count-- > 0) {
        handle_put_command(b, regarg, before ? line_no - 1 : line_no);
        if (before) {
            if (line_no > 1) {
                b->cur = buf_get_line(b, line_no);
            } else {
                b->cur = buf_get_line(b, 1);
            }
        } else {
            b->cur = buf_get_line(b, line_no + 1);
            line_no++;
        }
    }
    vis->cursor_col = 0;
}

static void
vi_put(buffer_t *b, vi_visual_t *vis, int before, int count)
{
    vi_put_from_register(b, vis, before, vi_take_register_index(vis), count);
}

static void
vi_linewise_yank(buffer_t *b, vi_visual_t *vis, int start, int end)
{
    char regarg[2];

    vi_take_register_arg(vis, regarg);
    handle_yank_command(b, regarg, 1, start, end);
    vis->cursor_col = 0;
}

static void
vi_linewise_delete(buffer_t *b, vi_visual_t *vis, int start, int end)
{
    char regarg[2];

    vi_take_register_arg(vis, regarg);
    handle_yank_command(b, regarg, 1, start, end);
    handle_delete_command(b, 1, start, end);
    vi_ensure_visible_line(b);
    vis->cursor_col = 0;
}

static void
vi_linewise_change(buffer_t *b, vi_visual_t *vis, int start, int end)
{
    char regarg[2];
    line_t *before = (start > 1) ? buf_get_line(b, start - 1) : NULL;

    vi_take_register_arg(vis, regarg);
    handle_yank_command(b, regarg, 1, start, end);
    handle_delete_command(b, 1, start, end);
    b->cur = buf_insert_after(b, before, "");
    if (!b->cur) {
        b->cur = b->head;
    }
    vis->cursor_col = 0;
    vis->insert_mode = 1;
    vis->replace_mode = 0;
    vi_set_insert_anchor(b, vis);
}

static void
vi_substitute_line(buffer_t *b, vi_visual_t *vis)
{
    if (!b->cur) {
        save_undo(b);
        b->cur = buf_insert_after(b, b->tail, "");
    } else {
        save_undo(b);
        if (vi_replace_current_text(b, "") != 0) {
            return;
        }
    }
    vis->cursor_col = 0;
    vis->insert_mode = 1;
    vis->replace_mode = 0;
    vi_set_insert_anchor(b, vis);
}

static int
vi_line_number_for_mark(buffer_t *b, line_t *mark)
{
    int line_no = 1;
    line_t *scan = b->head;

    while (scan) {
        if (scan == mark) {
            return line_no;
        }
        scan = scan->next;
        line_no++;
    }
    return -1;
}

static int
vi_apply_charwise_motion(buffer_t *b, vi_visual_t *vis, line_t *target_line,
    int target_col, int inclusive_end)
{
    line_t *start_line;
    line_t *end_line;
    int start_col;
    int end_col;
    int cmp;

    if (!b->cur || !target_line) {
        return -1;
    }
    if ((size_t)target_col > target_line->len) {
        target_col = (int)target_line->len;
    }

    cmp = vi_compare_positions(b, b->cur, vis->cursor_col, target_line, target_col);
    if (cmp == 0) {
        return -1;
    }
    if (cmp < 0) {
        start_line = b->cur;
        start_col = vis->cursor_col;
        end_line = target_line;
        end_col = target_col + (inclusive_end ? 1 : 0);
    } else {
        start_line = target_line;
        start_col = target_col;
        end_line = b->cur;
        end_col = vis->cursor_col + (inclusive_end ? 1 : 0);
    }

    if (!inclusive_end && start_line != end_line && end_col == 0) {
        end_line = end_line->prev;
        if (!end_line) {
            return -1;
        }
        end_col = (int)end_line->len;
    }

    if (vis->pending_op == 'y') {
        int rc = vi_yank_range(b, vis, start_line, start_col, end_line, end_col);

        if (rc == 0) {
            b->cur = start_line;
            vis->cursor_col = start_col;
        }
        return rc;
    }
    vi_delete_range(b, vis, start_line, start_col, end_line, end_col,
        vis->pending_op == 'c');
    return 0;
}

static int
vi_simulate_motion_target(buffer_t *b, vi_visual_t *vis,
    void (*move_fn)(buffer_t *, vi_visual_t *, int), int count,
    line_t **line_out, int *col_out)
{
    line_t *saved_line = b->cur;
    vi_visual_t tmp = *vis;

    move_fn(b, &tmp, count);
    *line_out = b->cur;
    *col_out = tmp.cursor_col;
    b->cur = saved_line;
    if (!*line_out || (*line_out == saved_line && *col_out == vis->cursor_col)) {
        return -1;
    }
    return 0;
}

static int
vi_backward_end_motion_target(buffer_t *b, vi_visual_t *vis, int count, int bigword,
    line_t **line_out, int *col_out)
{
    line_t *saved_line = b->cur;
    int saved_col = vis->cursor_col;
    vi_visual_t tmp = *vis;

    if (bigword) {
        vi_move_bigword_end_backward_count(b, &tmp, count);
    } else {
        vi_move_word_end_backward_count(b, &tmp, count);
    }
    *line_out = b->cur;
    *col_out = tmp.cursor_col;
    b->cur = saved_line;
    vis->cursor_col = saved_col;
    return (*line_out != NULL) ? 0 : -1;
}

static int
vi_search_motion_target(buffer_t *b, vi_visual_t *vis, const char *pattern, int forward,
    int count, line_t **line_out, int *col_out)
{
    line_t *saved_line = b->cur;
    int line_no = -1;

    if (count < 1) {
        count = 1;
    }
    while (count-- > 0) {
        line_no = exvi_search(b, pattern, forward);
        if (line_no < 1) {
            b->cur = saved_line;
            return -1;
        }
        b->cur = buf_get_line(b, line_no);
        pattern = "";
    }
    if (line_no < 1) {
        b->cur = saved_line;
        return -1;
    }
    *line_out = b->cur;
    *col_out = 0;
    b->cur = saved_line;
    vis->last_search_forward = forward;
    return (*line_out != NULL) ? 0 : -1;
}

static int
vi_apply_search_linewise_motion(buffer_t *b, vi_visual_t *vis, int current_line_no,
    line_t *target_line, int target_col)
{
    int target_line_no;
    int start;
    int end;

    if (target_col != 0 || vis->cursor_col != 0 || !target_line || target_line == b->cur) {
        return -1;
    }
    target_line_no = vi_line_number_for_mark(b, target_line);
    if (target_line_no < 1 || target_line_no == current_line_no) {
        return -1;
    }
    if (target_line_no > current_line_no) {
        start = current_line_no;
        end = target_line_no - 1;
    } else {
        start = target_line_no;
        end = current_line_no - 1;
    }
    if (start > end) {
        return -1;
    }
    if (vis->pending_op == 'y') {
        vi_linewise_yank(b, vis, start, end);
    } else if (vis->pending_op == 'd') {
        vi_linewise_delete(b, vis, start, end);
        vi_set_last_change(vis, VI_REPEAT_DD, end - start + 1, 0);
    } else {
        vi_linewise_change(b, vis, start, end);
    }
    return 0;
}

static void
vi_handle_pending_operator(buffer_t *b, vi_visual_t *vis, int key)
{
    int line_no = buf_current_line(b);
    int raw_count = vis->pending_count;
    int count = vi_take_count(vis);
    int end;
    int last_line = vi_clamp_line_target(b, line_no + count - 1);

    if (vis->pending_op == 'd' && key == 'd') {
        vi_linewise_delete(b, vis, line_no, last_line);
        vi_set_last_change(vis, VI_REPEAT_DD, count, 0);
    } else if (vis->pending_op == 'c' && key == 'c') {
        if (count == 1) {
            char regarg[2];

            vi_take_register_arg(vis, regarg);
            handle_yank_command(b, regarg, 1, line_no, last_line);
            vi_substitute_line(b, vis);
        } else {
            line_t *start = buf_get_line(b, line_no);
            line_t *before = start ? start->prev : NULL;
            char regarg[2];

            vi_take_register_arg(vis, regarg);
            handle_yank_command(b, regarg, 1, line_no, last_line);
            handle_delete_command(b, 1, line_no, last_line);
            b->cur = buf_insert_after(b, before, "");
            vis->cursor_col = 0;
            vis->insert_mode = 1;
            vis->replace_mode = 0;
        }
    } else if (vis->pending_op == 'y' && key == 'y') {
        vi_linewise_yank(b, vis, line_no, last_line);
    } else if ((vis->pending_op == 'd' || vis->pending_op == 'c' || vis->pending_op == 'y') &&
        key == '\'') {
        int mark_key = vi_read_visual_key(b, vis, ':', NULL);
        int mark_line;
        int start;

        if (mark_key < 'a' || mark_key > 'z' || !b->marks[mark_key - 'a']) {
            write(STDOUT_FILENO, "\a", 1);
        } else {
            mark_line = vi_line_number_for_mark(b, b->marks[mark_key - 'a']);
            if (mark_line < 1) {
                write(STDOUT_FILENO, "\a", 1);
            } else {
                start = line_no;
                end = mark_line;
                if (start > end) {
                    int tmp = start;

                    start = end;
                    end = tmp;
                }
                if (vis->pending_op == 'y') {
                    vi_linewise_yank(b, vis, start, end);
                } else if (vis->pending_op == 'd') {
                    vi_linewise_delete(b, vis, start, end);
                } else {
                    vi_linewise_change(b, vis, start, end);
                }
            }
        }
    } else if ((vis->pending_op == 'd' || vis->pending_op == 'c' || vis->pending_op == 'y') &&
        key == '`') {
        int mark_key = vi_read_visual_key(b, vis, ':', NULL);
        int idx = mark_key - 'a';

        if (mark_key < 'a' || mark_key > 'z' || !b->marks[idx]
            || vi_apply_charwise_motion(b, vis, b->marks[idx], b->mark_cols[idx], 0) != 0) {
            write(STDOUT_FILENO, "\a", 1);
        }
    } else if ((vis->pending_op == 'd' || vis->pending_op == 'c' || vis->pending_op == 'y') &&
        key == 'l') {
        line_t *cur = b->cur;
        int end = vis->cursor_col + count;

        if (!cur || (size_t)vis->cursor_col >= cur->len) {
            write(STDOUT_FILENO, "\a", 1);
        } else if (vis->pending_op == 'y') {
            if (vi_yank_span(vis, cur, vis->cursor_col, end) != 0) {
                write(STDOUT_FILENO, "\a", 1);
            }
        } else {
            vi_delete_span(b, vis, vis->cursor_col, end, vis->pending_op == 'c');
        }
    } else if ((vis->pending_op == 'd' || vis->pending_op == 'c' || vis->pending_op == 'y') &&
        key == 'h') {
        int start = vis->cursor_col - count;

        if (start < 0 || vis->cursor_col <= 0) {
            write(STDOUT_FILENO, "\a", 1);
        } else if (vis->pending_op == 'y') {
            if (vi_yank_span(vis, b->cur, start, vis->cursor_col) != 0) {
                write(STDOUT_FILENO, "\a", 1);
            }
        } else {
            vi_delete_span(b, vis, start, vis->cursor_col, vis->pending_op == 'c');
        }
    } else if (vis->pending_op == 'y' && key == 'w') {
        vi_find_word_boundary_forward_count(b->cur, vis->cursor_col, count, &end);
        if (vi_yank_span(vis, b->cur, vis->cursor_col, end) != 0) {
            write(STDOUT_FILENO, "\a", 1);
        }
    } else if (vis->pending_op == 'y' && key == 'e') {
        if (vi_find_word_end_exclusive_count(b->cur, vis->cursor_col, count, &end) == 0 &&
            vi_yank_span(vis, b->cur, vis->cursor_col, end) == 0) {
            /* nothing */
        } else {
            write(STDOUT_FILENO, "\a", 1);
        }
    } else if (vis->pending_op == 'y' && key == 'E') {
        if (vi_find_bigword_end_exclusive_count(b->cur, vis->cursor_col, count, &end) == 0 &&
            vi_yank_span(vis, b->cur, vis->cursor_col, end) == 0) {
            /* nothing */
        } else {
            write(STDOUT_FILENO, "\a", 1);
        }
    } else if (vis->pending_op == 'y' && key == 'b') {
        int start;

        if (vi_find_word_start_backward_count(b->cur, vis->cursor_col, count, &start) == 0 &&
            vi_yank_span(vis, b->cur, start, vis->cursor_col) == 0) {
            /* nothing */
        } else {
            write(STDOUT_FILENO, "\a", 1);
        }
    } else if (vis->pending_op == 'y' && key == 'B') {
        int start;

        if (vi_find_bigword_start_backward_count(b->cur, vis->cursor_col, count, &start) == 0 &&
            vi_yank_span(vis, b->cur, start, vis->cursor_col) == 0) {
            /* nothing */
        } else {
            write(STDOUT_FILENO, "\a", 1);
        }
    } else if (vis->pending_op == 'y' && key == 'W') {
        vi_find_bigword_boundary_forward_count(b->cur, vis->cursor_col, count, &end);
        if (vi_yank_span(vis, b->cur, vis->cursor_col, end) != 0) {
            write(STDOUT_FILENO, "\a", 1);
        }
    } else if ((vis->pending_op == 'd' || vis->pending_op == 'c' || vis->pending_op == 'y') &&
        key == 'g') {
        int motion = vi_read_visual_key(b, vis, ':', NULL);
        line_t *target_line;
        int target_col;

        if (motion == 'g') {
            int target = (raw_count > 0) ? raw_count : 1;
            int start = line_no;
            int end_line = vi_clamp_line_target(b, target);

            if (start > end_line) {
                int tmp = start;

                start = end_line;
                end_line = tmp;
            }
            if (vis->pending_op == 'y') {
                vi_linewise_yank(b, vis, start, end_line);
            } else if (vis->pending_op == 'd') {
                vi_linewise_delete(b, vis, start, end_line);
                vi_set_last_change(vis, VI_REPEAT_DD, end_line - start + 1, 0);
            } else {
                vi_linewise_change(b, vis, start, end_line);
            }
        } else if (motion == '_') {
            int target = vi_clamp_line_target(b, line_no + count - 1);

            target_line = buf_get_line(b, target);
            target_col = vi_last_nonblank_col(target_line);
            if (vi_apply_charwise_motion(b, vis, target_line, target_col, 1) != 0) {
                write(STDOUT_FILENO, "\a", 1);
            }
        } else if (motion != 'e' && motion != 'E') {
            write(STDOUT_FILENO, "\a", 1);
        } else if (vi_backward_end_motion_target(b, vis, count, motion == 'E',
            &target_line, &target_col) != 0 ||
            target_line != b->cur || target_col > vis->cursor_col) {
            write(STDOUT_FILENO, "\a", 1);
        } else if (vis->pending_op == 'y') {
            if (vi_yank_span(vis, b->cur, target_col, vis->cursor_col + 1) != 0) {
                write(STDOUT_FILENO, "\a", 1);
            } else {
                vis->cursor_col = target_col;
            }
        } else {
            vi_delete_span(b, vis, target_col, vis->cursor_col + 1, vis->pending_op == 'c');
        }
    } else if (vis->pending_op == 'y' &&
        (key == 'f' || key == 'F' || key == 't' || key == 'T')) {
        int ch;
        int start;

        ch = vi_read_visual_key(b, vis, ':', NULL);
        if (ch == -1 || ch == '\x1b' || ch == '\r' || ch == '\n') {
            write(STDOUT_FILENO, "\a", 1);
        } else if (vi_find_motion_span(b->cur, vis->cursor_col, ch,
            (key == 'f' || key == 't'),
            (key == 't' || key == 'T'),
            count, &start, &end) == 0 &&
            vi_yank_span(vis, b->cur, start, end) == 0) {
            vis->last_find_char = ch;
            vis->last_find_forward = (key == 'f' || key == 't');
            vis->last_find_till = (key == 't' || key == 'T');
        } else {
            write(STDOUT_FILENO, "\a", 1);
        }
    } else if (vis->pending_op == 'y' && key == '0') {
        if (vi_yank_span(vis, b->cur, 0, vis->cursor_col) != 0) {
            write(STDOUT_FILENO, "\a", 1);
        }
    } else if (vis->pending_op == 'y' && key == '^') {
        int start = vi_first_nonblank_col(b->cur);

        if (vi_yank_span(vis, b->cur, start, vis->cursor_col) != 0) {
            write(STDOUT_FILENO, "\a", 1);
        }
    } else if ((vis->pending_op == 'd' || vis->pending_op == 'c' || vis->pending_op == 'y') &&
        key == '_') {
        if (vis->pending_op == 'y') {
            vi_linewise_yank(b, vis, line_no, last_line);
        } else if (vis->pending_op == 'd') {
            vi_linewise_delete(b, vis, line_no, last_line);
            vi_set_last_change(vis, VI_REPEAT_DD, count, 0);
        } else {
            vi_linewise_change(b, vis, line_no, last_line);
        }
    } else if ((vis->pending_op == 'd' || vis->pending_op == 'c' || vis->pending_op == 'y') &&
        (key == 'j' || key == '+' || key == '\r' || key == '\n')) {
        int start = line_no;
        int end_line = vi_clamp_line_target(b, line_no + count);

        if (vis->pending_op == 'y') {
            vi_linewise_yank(b, vis, start, end_line);
        } else if (vis->pending_op == 'd') {
            vi_linewise_delete(b, vis, start, end_line);
            vi_set_last_change(vis, VI_REPEAT_DD, end_line - start + 1, 0);
        } else {
            vi_linewise_change(b, vis, start, end_line);
        }
    } else if ((vis->pending_op == 'd' || vis->pending_op == 'c' || vis->pending_op == 'y') &&
        (key == 'H' || key == 'M' || key == 'L')) {
        int target;
        int start = line_no;
        int end_line;

        if (key == 'H') {
            target = vi_screen_target_line(b, vis, 0, raw_count);
        } else if (key == 'M') {
            target = vi_screen_target_line(b, vis, 1, 0);
        } else {
            target = vi_screen_target_line(b, vis, 2, raw_count);
        }
        end_line = target;
        if (start > end_line) {
            int tmp = start;

            start = end_line;
            end_line = tmp;
        }
        if (vis->pending_op == 'y') {
            vi_linewise_yank(b, vis, start, end_line);
        } else if (vis->pending_op == 'd') {
            vi_linewise_delete(b, vis, start, end_line);
            vi_set_last_change(vis, VI_REPEAT_DD, end_line - start + 1, 0);
        } else {
            vi_linewise_change(b, vis, start, end_line);
        }
    } else if ((vis->pending_op == 'd' || vis->pending_op == 'c' || vis->pending_op == 'y') &&
        (key == 'k' || key == '-')) {
        int start = vi_clamp_line_target(b, line_no - count);
        int end_line = line_no;

        if (vis->pending_op == 'y') {
            vi_linewise_yank(b, vis, start, end_line);
        } else if (vis->pending_op == 'd') {
            vi_linewise_delete(b, vis, start, end_line);
            vi_set_last_change(vis, VI_REPEAT_DD, end_line - start + 1, 0);
        } else {
            vi_linewise_change(b, vis, start, end_line);
        }
    } else if ((vis->pending_op == 'd' || vis->pending_op == 'c' || vis->pending_op == 'y') &&
        key == '|') {
        line_t *cur = b->cur;
        int target;
        int start;

        if (!cur) {
            write(STDOUT_FILENO, "\a", 1);
        } else {
            target = vi_index_for_display_col(cur, count - 1);
            if (target == vis->cursor_col) {
                write(STDOUT_FILENO, "\a", 1);
            } else if (target > vis->cursor_col) {
                start = vis->cursor_col;
                if (vis->pending_op == 'y') {
                    if (vi_yank_span(vis, cur, start, target) != 0) {
                        write(STDOUT_FILENO, "\a", 1);
                    }
                } else {
                    vi_delete_span(b, vis, start, target, vis->pending_op == 'c');
                }
            } else {
                start = target;
                if (vis->pending_op == 'y') {
                    if (vi_yank_span(vis, cur, start, vis->cursor_col) != 0) {
                        write(STDOUT_FILENO, "\a", 1);
                    }
                } else {
                    vi_delete_span(b, vis, start, vis->cursor_col, vis->pending_op == 'c');
                }
            }
        }
    } else if (vis->pending_op == 'y' && key == '$') {
        line_t *target_line;
        int target_col;
        line_t *cur = b->cur;

        if (count > 1) {
            if (vi_eol_motion_target(b, count, &target_line, &target_col) != 0 ||
                vi_apply_charwise_motion(b, vis, target_line, target_col, 1) != 0) {
                write(STDOUT_FILENO, "\a", 1);
            }
        } else if (!cur || vi_yank_span(vis, cur, vis->cursor_col, (int)cur->len) != 0) {
            write(STDOUT_FILENO, "\a", 1);
        }
    } else if ((vis->pending_op == 'd' || vis->pending_op == 'c') && key == 'w') {
        vi_find_word_boundary_forward_count(b->cur, vis->cursor_col, count, &end);
        if (end >= vis->cursor_col) {
            vi_delete_span(b, vis, vis->cursor_col, end, vis->pending_op == 'c');
            if (vis->pending_op == 'd') {
                vi_set_last_change(vis, VI_REPEAT_DW, count, 0);
            }
        }
    } else if ((vis->pending_op == 'd' || vis->pending_op == 'c') && key == 'e') {
        if (vi_find_word_end_exclusive_count(b->cur, vis->cursor_col, count, &end) == 0 &&
            end > vis->cursor_col) {
            vi_delete_span(b, vis, vis->cursor_col, end, vis->pending_op == 'c');
        } else {
            write(STDOUT_FILENO, "\a", 1);
        }
    } else if ((vis->pending_op == 'd' || vis->pending_op == 'c') && key == 'E') {
        if (vi_find_bigword_end_exclusive_count(b->cur, vis->cursor_col, count, &end) == 0 &&
            end > vis->cursor_col) {
            vi_delete_span(b, vis, vis->cursor_col, end, vis->pending_op == 'c');
        } else {
            write(STDOUT_FILENO, "\a", 1);
        }
    } else if ((vis->pending_op == 'd' || vis->pending_op == 'c') && key == 'b') {
        int start;

        if (vi_find_word_start_backward_count(b->cur, vis->cursor_col, count, &start) == 0 &&
            start < vis->cursor_col) {
            vi_delete_span(b, vis, start, vis->cursor_col, vis->pending_op == 'c');
        } else {
            write(STDOUT_FILENO, "\a", 1);
        }
    } else if ((vis->pending_op == 'd' || vis->pending_op == 'c') && key == 'B') {
        int start;

        if (vi_find_bigword_start_backward_count(b->cur, vis->cursor_col, count, &start) == 0 &&
            start < vis->cursor_col) {
            vi_delete_span(b, vis, start, vis->cursor_col, vis->pending_op == 'c');
        } else {
            write(STDOUT_FILENO, "\a", 1);
        }
    } else if ((vis->pending_op == 'd' || vis->pending_op == 'c') && key == 'W') {
        vi_find_bigword_boundary_forward_count(b->cur, vis->cursor_col, count, &end);
        if (end >= vis->cursor_col) {
            vi_delete_span(b, vis, vis->cursor_col, end, vis->pending_op == 'c');
        }
    } else if ((vis->pending_op == 'd' || vis->pending_op == 'c') &&
        (key == 'f' || key == 'F' || key == 't' || key == 'T')) {
        int ch;
        int start;

        ch = vi_read_visual_key(b, vis, ':', NULL);
        if (ch == -1 || ch == '\x1b' || ch == '\r' || ch == '\n') {
            write(STDOUT_FILENO, "\a", 1);
        } else if (vi_find_motion_span(b->cur, vis->cursor_col, ch,
            (key == 'f' || key == 't'),
            (key == 't' || key == 'T'),
            count, &start, &end) == 0) {
            vi_delete_span(b, vis, start, end, vis->pending_op == 'c');
            vis->last_find_char = ch;
            vis->last_find_forward = (key == 'f' || key == 't');
            vis->last_find_till = (key == 't' || key == 'T');
            if (vis->pending_op == 'd') {
                vi_set_last_change(vis, VI_REPEAT_D_FIND, count, 0);
            }
        } else {
            write(STDOUT_FILENO, "\a", 1);
        }
    } else if ((vis->pending_op == 'd' || vis->pending_op == 'c' || vis->pending_op == 'y')
        && (key == ';' || key == ',')) {
        int start;
        int forward = vis->last_find_forward;

        if (!vis->last_find_char) {
            write(STDOUT_FILENO, "\a", 1);
        } else {
            if (key == ',') {
                forward = !forward;
            }
            if (vi_find_motion_span(b->cur, vis->cursor_col, vis->last_find_char,
                forward, vis->last_find_till, count, &start, &end) == 0) {
                if (vis->pending_op == 'y') {
                    if (vi_yank_span(vis, b->cur, start, end) != 0) {
                        write(STDOUT_FILENO, "\a", 1);
                    }
                } else {
                    vi_delete_span(b, vis, start, end, vis->pending_op == 'c');
                    if (vis->pending_op == 'd') {
                        vi_set_last_change(vis, VI_REPEAT_D_FIND, count, 0);
                    }
                }
            } else {
                write(STDOUT_FILENO, "\a", 1);
            }
        }
    } else if ((vis->pending_op == 'd' || vis->pending_op == 'c' || vis->pending_op == 'y') &&
        (key == ')' || key == '(' || key == '}' || key == '{')) {
        line_t *target_line;
        int target_col;
        int rc;

        if (key == ')') {
            rc = vi_simulate_motion_target(b, vis, vi_move_sentence_forward, count,
                &target_line, &target_col);
        } else if (key == '(') {
            rc = vi_simulate_motion_target(b, vis, vi_move_sentence_backward, count,
                &target_line, &target_col);
        } else if (key == '}') {
            rc = vi_simulate_motion_target(b, vis, vi_move_paragraph_forward, count,
                &target_line, &target_col);
        } else {
            rc = vi_simulate_motion_target(b, vis, vi_move_paragraph_backward, count,
                &target_line, &target_col);
        }
        if (rc != 0 || vi_apply_charwise_motion(b, vis, target_line, target_col, 0) != 0) {
            write(STDOUT_FILENO, "\a", 1);
        }
    } else if ((vis->pending_op == 'd' || vis->pending_op == 'c' || vis->pending_op == 'y') &&
        (key == '/' || key == '?')) {
        char pattern[256];
        int forward = (key == '/');
        line_t *target_line;
        int target_col;

        pattern[0] = '\0';
        if (vi_prompt_input(b, vis, key, pattern, sizeof(pattern)) != 0) {
            write(STDOUT_FILENO, "\a", 1);
        } else if (vi_search_motion_target(b, vis, pattern, forward, count,
            &target_line, &target_col) != 0 ||
            vi_apply_charwise_motion(b, vis, target_line, target_col, 0) != 0) {
            write(STDOUT_FILENO, "\a", 1);
        }
    } else if ((vis->pending_op == 'd' || vis->pending_op == 'c' || vis->pending_op == 'y') &&
        (key == 'n' || key == 'N')) {
        int forward = (key == 'n') ? vis->last_search_forward : !vis->last_search_forward;
        line_t *target_line;
        int target_col;

        if (vi_search_motion_target(b, vis, "", forward, count,
            &target_line, &target_col) != 0 ||
            (vi_apply_search_linewise_motion(b, vis, line_no, target_line, target_col) != 0 &&
             vi_apply_charwise_motion(b, vis, target_line, target_col, 0) != 0)) {
            write(STDOUT_FILENO, "\a", 1);
        }
    } else if ((vis->pending_op == 'd' || vis->pending_op == 'c' || vis->pending_op == 'y') &&
        (key == '*' || key == '#')) {
        char *pattern = vi_current_word_pattern(b, vis);
        line_t *target_line;
        int target_col;

        if (!pattern) {
            write(STDOUT_FILENO, "\a", 1);
        } else if (vi_search_motion_target(b, vis, pattern, key == '*', count,
            &target_line, &target_col) != 0 ||
            (vi_apply_search_linewise_motion(b, vis, line_no, target_line, target_col) != 0 &&
             vi_apply_charwise_motion(b, vis, target_line, target_col, 0) != 0)) {
            write(STDOUT_FILENO, "\a", 1);
        }
        free(pattern);
    } else if ((vis->pending_op == 'd' || vis->pending_op == 'c') && key == '0') {
        if (vis->cursor_col > 0) {
            vi_delete_span(b, vis, 0, vis->cursor_col, vis->pending_op == 'c');
        } else {
            write(STDOUT_FILENO, "\a", 1);
        }
    } else if ((vis->pending_op == 'd' || vis->pending_op == 'c') && key == '^') {
        int start = vi_first_nonblank_col(b->cur);

        if (start < vis->cursor_col) {
            vi_delete_span(b, vis, start, vis->cursor_col, vis->pending_op == 'c');
        } else {
            write(STDOUT_FILENO, "\a", 1);
        }
    } else if ((vis->pending_op == 'd' || vis->pending_op == 'c') && key == '$') {
        line_t *target_line;
        int target_col;
        line_t *cur = b->cur;

        if (count > 1) {
            if (vi_eol_motion_target(b, count, &target_line, &target_col) != 0) {
                write(STDOUT_FILENO, "\a", 1);
            } else if (vis->pending_op == 'd' && vis->cursor_col == 0 &&
                target_line != b->cur) {
                int end_line = vi_line_number_for_mark(b, target_line);

                if (end_line < line_no) {
                    end_line = line_no;
                }
                vi_linewise_delete(b, vis, line_no, end_line);
                vi_set_last_change(vis, VI_REPEAT_DD, end_line - line_no + 1, 0);
            } else if (vi_apply_charwise_motion(b, vis, target_line, target_col, 1) != 0) {
                write(STDOUT_FILENO, "\a", 1);
            } else if (vis->pending_op == 'd') {
                vi_set_last_change(vis, VI_REPEAT_D_EOL, count, 0);
            }
        } else if (cur && (size_t)vis->cursor_col <= cur->len) {
            vi_delete_span(b, vis, vis->cursor_col, (int)cur->len,
                vis->pending_op == 'c');
            if (vis->pending_op == 'd') {
                vi_set_last_change(vis, VI_REPEAT_D_EOL, 1, 0);
            }
        } else {
            write(STDOUT_FILENO, "\a", 1);
        }
    } else if ((vis->pending_op == 'd' || vis->pending_op == 'c' || vis->pending_op == 'y') &&
        key == 'G') {
        int start = line_no;
        int end_line = vi_clamp_line_target(b, (raw_count > 0) ? raw_count : b->line_count);

        if (start > end_line) {
            int tmp = start;

            start = end_line;
            end_line = tmp;
        }
        if (vis->pending_op == 'y') {
            vi_linewise_yank(b, vis, start, end_line);
        } else if (vis->pending_op == 'd') {
            vi_linewise_delete(b, vis, start, end_line);
            vi_set_last_change(vis, VI_REPEAT_DD, end_line - start + 1, 0);
        } else {
            vi_linewise_change(b, vis, start, end_line);
        }
    } else if ((vis->pending_op == 'd' || vis->pending_op == 'c' || vis->pending_op == 'y')
        && key == '%') {
        if (raw_count > 0) {
            int target = (raw_count * b->line_count + 99) / 100;
            int start = line_no;
            int end_line;

            if (target < 1) {
                target = 1;
            }
            if (target > b->line_count) {
                target = b->line_count;
            }
            end_line = target;
            if (start > end_line) {
                int tmp = start;

                start = end_line;
                end_line = tmp;
            }
            if (vis->pending_op == 'y') {
                vi_linewise_yank(b, vis, start, end_line);
            } else if (vis->pending_op == 'd') {
                vi_linewise_delete(b, vis, start, end_line);
                vi_set_last_change(vis, VI_REPEAT_DD, end_line - start + 1, 0);
            } else {
                vi_linewise_change(b, vis, start, end_line);
            }
        } else {
            line_t *target_line;
            int target_col;

            if (vi_match_motion_target(b, vis, &target_line, &target_col) != 0 ||
                vi_apply_charwise_motion(b, vis, target_line, target_col, 1) != 0) {
                write(STDOUT_FILENO, "\a", 1);
            }
        }
    } else {
        write(STDOUT_FILENO, "\a", 1);
    }
    vis->pending_op = 0;
    vis->pending_reg = 0;
}

static void
vi_insert_char(buffer_t *b, vi_visual_t *vis, int ch)
{
    line_t *cur = vi_ensure_current_line(b);
    char *text;

    if (!cur) {
        return;
    }
    text = malloc(cur->len + 2);
    if (!text) {
        return;
    }
    memcpy(text, cur->text, (size_t)vis->cursor_col);
    text[vis->cursor_col] = (char)ch;
    memcpy(text + vis->cursor_col + 1, cur->text + vis->cursor_col,
        cur->len - (size_t)vis->cursor_col + 1);
    if (vi_replace_current_text(b, text) == 0) {
        vis->cursor_col++;
    }
    free(text);
}

static void
vi_replace_insert_char(buffer_t *b, vi_visual_t *vis, int ch)
{
    line_t *cur = vi_ensure_current_line(b);
    char *text;

    if (!cur) {
        return;
    }
    if ((size_t)vis->cursor_col >= cur->len) {
        vi_insert_char(b, vis, ch);
        return;
    }
    text = strdup(cur->text);
    if (!text) {
        return;
    }
    text[vis->cursor_col] = (char)ch;
    if (vi_replace_current_text(b, text) == 0) {
        vis->cursor_col++;
    }
    free(text);
}

static void
vi_backspace_char(buffer_t *b, vi_visual_t *vis)
{
    line_t *cur = b->cur;
    line_t *prev;
    char *text;
    char *merged;
    size_t prev_len;

    if (!cur) {
        return;
    }
    if (vis->cursor_col <= 0) {
        if (!cur->prev) {
            return;
        }
        prev = cur->prev;
        prev_len = prev->len;
        merged = malloc(prev->len + cur->len + 1);
        if (!merged) {
            return;
        }
        memcpy(merged, prev->text, prev->len);
        memcpy(merged + prev->len, cur->text, cur->len + 1);
        free(prev->text);
        prev->text = merged;
        prev->len += cur->len;
        b->cur = cur;
        buf_delete(b, cur);
        b->cur = prev;
        vis->cursor_col = (int)prev_len;
        if (vis->insert_anchor_line == cur) {
            vis->insert_anchor_line = prev;
            vis->insert_anchor_col += (int)prev_len;
        }
        return;
    }
    text = malloc(cur->len + 1);
    if (!text) {
        return;
    }
    memcpy(text, cur->text, (size_t)vis->cursor_col - 1);
    memcpy(text + vis->cursor_col - 1, cur->text + vis->cursor_col,
        cur->len - (size_t)vis->cursor_col + 1);
    if (vi_replace_current_text(b, text) == 0) {
        vis->cursor_col--;
    }
    free(text);
}

static void
vi_erase_word_backward_insert(buffer_t *b, vi_visual_t *vis)
{
    line_t *end_line = b->cur ? b->cur : b->head;
    int end_col = vis->cursor_col;
    line_t *start_line;
    int start_col;
    vi_visual_t tmp;

    if (!end_line) {
        return;
    }
    tmp = *vis;
    vi_move_word_backward_count(b, &tmp, 1);
    start_line = b->cur ? b->cur : b->head;
    start_col = tmp.cursor_col;
    b->cur = end_line;
    vis->cursor_col = end_col;
    if (!start_line) {
        return;
    }
    if (start_line == end_line && start_col == end_col) {
        return;
    }
    vi_delete_range(b, vis, start_line, start_col, end_line, end_col, 1);
}

static void
vi_erase_to_insert_anchor(buffer_t *b, vi_visual_t *vis)
{
    line_t *end_line = b->cur ? b->cur : b->head;
    int end_col = vis->cursor_col;
    line_t *start_line = vis->insert_anchor_line;
    int start_col = vis->insert_anchor_col;

    if (!end_line) {
        return;
    }
    if (!start_line) {
        start_line = end_line;
        start_col = 0;
    }
    if (start_line != end_line) {
        start_line = end_line;
        start_col = 0;
        vis->insert_anchor_line = end_line;
        vis->insert_anchor_col = 0;
    }
    if (start_col < 0) {
        start_col = 0;
    }
    if (start_col > end_col) {
        start_col = end_col;
    }
    if (start_col == end_col) {
        return;
    }
    vi_delete_range(b, vis, start_line, start_col, end_line, end_col, 1);
}

static void
vi_replace_char(buffer_t *b, vi_visual_t *vis, int ch)
{
    line_t *cur = b->cur;
    char *text;

    if (!cur || (size_t)vis->cursor_col >= cur->len) {
        write(STDOUT_FILENO, "\a", 1);
        return;
    }
    save_undo(b);
    text = strdup(cur->text);
    if (!text) {
        return;
    }
    text[vis->cursor_col] = (char)ch;
    vi_replace_current_text(b, text);
    free(text);
}

static void
vi_delete_char(buffer_t *b, vi_visual_t *vis)
{
    line_t *cur = b->cur;
    char *text;

    if (!cur || (size_t)vis->cursor_col >= cur->len) {
        return;
    }
    vi_store_charwise_span(vis, cur, vis->cursor_col, vis->cursor_col + 1);
    save_undo(b);
    text = malloc(cur->len);
    if (!text) {
        return;
    }
    memcpy(text, cur->text, (size_t)vis->cursor_col);
    memcpy(text + vis->cursor_col, cur->text + vis->cursor_col + 1,
        cur->len - (size_t)vis->cursor_col);
    vi_replace_current_text(b, text);
    free(text);
    vi_clamp_cursor(b, vis);
}

static void
vi_delete_prev_char(buffer_t *b, vi_visual_t *vis)
{
    line_t *cur = b->cur;
    char *text;

    if (!cur || vis->cursor_col <= 0) {
        return;
    }
    vi_store_charwise_span(vis, cur, vis->cursor_col - 1, vis->cursor_col);
    save_undo(b);
    text = malloc(cur->len);
    if (!text) {
        return;
    }
    memcpy(text, cur->text, (size_t)vis->cursor_col - 1);
    memcpy(text + vis->cursor_col - 1, cur->text + vis->cursor_col,
        cur->len - (size_t)vis->cursor_col + 1);
    vi_replace_current_text(b, text);
    free(text);
    vis->cursor_col--;
    vi_clamp_cursor(b, vis);
}

static void
vi_toggle_case(buffer_t *b, vi_visual_t *vis, int count)
{
    line_t *cur = b->cur;
    char *text;
    int start_col = vis->cursor_col;
    size_t i;

    if (!cur || (size_t)vis->cursor_col >= cur->len) {
        write(STDOUT_FILENO, "\a", 1);
        return;
    }
    save_undo(b);
    text = strdup(cur->text);
    if (!text) {
        return;
    }
    for (i = (size_t)start_col; i < cur->len && count > 0; i++, count--) {
        unsigned char ch = (unsigned char)text[i];

        if (islower(ch)) {
            text[i] = (char)toupper(ch);
        } else if (isupper(ch)) {
            text[i] = (char)tolower(ch);
        }
    }
    if (vi_replace_current_text(b, text) == 0) {
        if (i < cur->len) {
            vis->cursor_col = (int)i;
        } else if (cur->len > 0) {
            vis->cursor_col = (int)cur->len - 1;
        }
        vi_clamp_cursor(b, vis);
    }
    free(text);
}

static void
vi_split_line(buffer_t *b, vi_visual_t *vis)
{
    line_t *cur = b->cur;
    char *left;
    char *right;

    if (!cur) {
        return;
    }
    left = malloc((size_t)vis->cursor_col + 1);
    right = strdup(cur->text + vis->cursor_col);
    if (!left || !right) {
        free(left);
        free(right);
        return;
    }
    memcpy(left, cur->text, (size_t)vis->cursor_col);
    left[vis->cursor_col] = '\0';
    if (vi_replace_current_text(b, left) != 0) {
        free(left);
        free(right);
        return;
    }
    b->cur = buf_insert_after(b, cur, right);
    vis->cursor_col = 0;
    vi_set_insert_anchor(b, vis);
    free(left);
    free(right);
}

static void
vi_open_line(buffer_t *b, vi_visual_t *vis, int above)
{
    line_t *cur = b->cur ? b->cur : b->tail;
    line_t *pos;

    save_undo(b);
    if (!cur) {
        b->cur = buf_insert_after(b, NULL, "");
    } else {
        pos = above ? cur->prev : cur;
        b->cur = buf_insert_after(b, pos, "");
    }
    vis->cursor_col = 0;
    vis->insert_mode = 1;
    vi_set_insert_anchor(b, vis);
}

static int
vi_prompt_input(buffer_t *b, vi_visual_t *vis, char prefix, char *buf, size_t buf_size)
{
    size_t len = strlen(buf);

    for (;;) {
        int key;

        vi_render(b, vis, prefix, buf);
        key = vi_read_visual_key(b, vis, prefix, buf);
        if (key == -1 || key == '\x1b') {
            return -1;
        }
        if (key == '\r' || key == '\n') {
            buf[len] = '\0';
            return 0;
        }
        if (key == 127 || key == '\b') {
            if (len > 0) {
                buf[--len] = '\0';
            }
            continue;
        }
        if (isprint(key) && len + 1 < buf_size) {
            buf[len++] = (char)key;
            buf[len] = '\0';
        }
    }
}

static void
vi_command_prompt(buffer_t *b, vi_visual_t *vis)
{
    char cmd[256];

    cmd[0] = '\0';
    if (vi_prompt_input(b, vis, ':', cmd, sizeof(cmd)) != 0) {
        return;
    }
    exvi_execute_command(b, cmd);
    vi_clamp_cursor(b, vis);
}

static void
vi_repeat_last_change(buffer_t *b, vi_visual_t *vis)
{
    line_t *cur = b->cur;
    int count = vis->pending_count > 0 ? vis->pending_count : vis->last_change_count;
    int line_no = buf_current_line(b);
    int end;
    int last;

    vis->pending_count = 0;
    if (count < 1) {
        count = 1;
    }

    switch (vis->last_change) {
    case VI_REPEAT_X:
        if (!cur || (size_t)vis->cursor_col >= cur->len) {
            write(STDOUT_FILENO, "\a", 1);
            return;
        }
        if (count == 1) {
            vi_delete_char(b, vis);
        } else {
            vi_delete_span(b, vis, vis->cursor_col, vis->cursor_col + count, 0);
        }
        break;
    case VI_REPEAT_X_BACK:
        if (!cur || vis->cursor_col <= 0) {
            write(STDOUT_FILENO, "\a", 1);
            return;
        }
        while (count-- > 0 && vis->cursor_col > 0) {
            vi_delete_prev_char(b, vis);
        }
        break;
    case VI_REPEAT_R:
        if (!cur || (size_t)vis->cursor_col >= cur->len) {
            write(STDOUT_FILENO, "\a", 1);
            return;
        }
        vi_replace_char(b, vis, vis->last_change_char);
        break;
    case VI_REPEAT_TILDE:
        vi_toggle_case(b, vis, count);
        break;
    case VI_REPEAT_DD:
        last = vi_clamp_line_target(b, line_no + count - 1);
        handle_delete_command(b, 1, line_no, last);
        if (!b->cur && b->head) {
            b->cur = b->head;
        }
        vis->cursor_col = 0;
        break;
    case VI_REPEAT_DW:
        vi_find_word_boundary_forward_count(b->cur, vis->cursor_col, count, &end);
        if (end >= vis->cursor_col) {
            vi_delete_span(b, vis, vis->cursor_col, end, 0);
        }
        break;
    case VI_REPEAT_D_FIND:
        {
            int start;

            if (vi_find_motion_span(b->cur, vis->cursor_col, vis->last_find_char,
                vis->last_find_forward, vis->last_find_till, count, &start, &end) == 0) {
                vi_delete_span(b, vis, start, end, 0);
            } else {
                write(STDOUT_FILENO, "\a", 1);
            }
        }
        break;
    case VI_REPEAT_D_EOL:
        if (count <= 1 || !cur) {
            vi_delete_span(b, vis, vis->cursor_col,
                cur ? (int)cur->len : vis->cursor_col, 0);
        } else {
            line_t *target_line;
            int target_col;

            if (vi_eol_motion_target(b, count, &target_line, &target_col) != 0 ||
                vi_apply_charwise_motion(b, vis, target_line, target_col, 1) != 0) {
                write(STDOUT_FILENO, "\a", 1);
            }
        }
        break;
    case VI_REPEAT_J:
        last = vi_clamp_line_target(b, line_no + count - 1);
        if (b->cur && b->cur->next && last > line_no) {
            handle_join_command(b, 1, line_no, last);
            vi_clamp_cursor(b, vis);
        } else if (b->cur && b->cur->next) {
            handle_join_command(b, 1, line_no, line_no + 1);
            vi_clamp_cursor(b, vis);
        } else {
            write(STDOUT_FILENO, "\a", 1);
        }
        break;
    case VI_REPEAT_P:
        vi_put_from_register(b, vis, 0, 26, count);
        break;
    case VI_REPEAT_P_BEFORE:
        vi_put_from_register(b, vis, 1, 26, count);
        break;
    default:
        write(STDOUT_FILENO, "\a", 1);
        break;
    }
}

static void
vi_apply_search(buffer_t *b, vi_visual_t *vis, const char *pattern, int forward)
{
    int line_no = exvi_search(b, pattern, forward);

    if (line_no < 1) {
        write(STDOUT_FILENO, "\a", 1);
        return;
    }
    b->cur = buf_get_line(b, line_no);
    vis->cursor_col = 0;
    vis->last_search_forward = forward;
}

static void
vi_repeat_search(buffer_t *b, vi_visual_t *vis, int forward, int count)
{
    while (count-- > 0) {
        int line_no = exvi_search(b, "", forward);

        if (line_no < 1) {
            write(STDOUT_FILENO, "\a", 1);
            return;
        }
        b->cur = buf_get_line(b, line_no);
        vis->cursor_col = 0;
        vis->last_search_forward = forward;
    }
}

static void
vi_search_prompt(buffer_t *b, vi_visual_t *vis, int forward)
{
    char pattern[256];
    char prefix = forward ? '/' : '?';

    pattern[0] = '\0';
    if (vi_prompt_input(b, vis, prefix, pattern, sizeof(pattern)) == 0) {
        vi_apply_search(b, vis, pattern, forward);
    }
}

static char *
vi_current_word_pattern(buffer_t *b, vi_visual_t *vis)
{
    line_t *cur = b->cur;
    int start;
    int end;
    int pos;
    size_t len;
    const char *meta = ".^$*+?()[{\\|";
    char *pattern;
    size_t out_len = 0;

    if (!cur || cur->len == 0) {
        return NULL;
    }
    pos = vis->cursor_col;
    if (pos >= (int)cur->len) {
        pos = (int)cur->len - 1;
    }
    if (pos < 0 || !vi_is_word_char((unsigned char)cur->text[pos])) {
        return NULL;
    }
    start = pos;
    while (start > 0 && vi_is_word_char((unsigned char)cur->text[start - 1])) {
        start--;
    }
    end = pos + 1;
    while ((size_t)end < cur->len && vi_is_word_char((unsigned char)cur->text[end])) {
        end++;
    }
    len = (size_t)(end - start);
    for (size_t i = 0; i < len; i++) {
        if (strchr(meta, cur->text[start + (int)i])) {
            out_len++;
        }
        out_len++;
    }
    pattern = malloc(out_len + 1);
    if (!pattern) {
        return NULL;
    }
    out_len = 0;
    for (size_t i = 0; i < len; i++) {
        char ch = cur->text[start + (int)i];

        if (strchr(meta, ch)) {
            pattern[out_len++] = '\\';
        }
        pattern[out_len++] = ch;
    }
    pattern[out_len] = '\0';
    return pattern;
}

static void
vi_search_current_word(buffer_t *b, vi_visual_t *vis, int forward)
{
    char *pattern = vi_current_word_pattern(b, vis);

    if (!pattern) {
        write(STDOUT_FILENO, "\a", 1);
        return;
    }
    vi_apply_search(b, vis, pattern, forward);
    free(pattern);
}

int
exvi_visual_main(buffer_t *b)
{
    vi_visual_t vis;
    struct sigaction sa;
    struct sigaction old_winch;
    int have_winch = 0;
    int key;

    memset(&vis, 0, sizeof(vis));
    vis.top_line = 1;
    vis.last_search_forward = 1;
    vi_ensure_visible_line(b);
    if (vi_enable_raw(&vis) != 0) {
        return 1;
    }
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = vi_handle_winch;
    sigemptyset(&sa.sa_mask);
    if (sigaction(SIGWINCH, &sa, &old_winch) == 0) {
        have_winch = 1;
    }
    vi_resize_pending = 0;
    printf("\x1b[2J");

    for (;;) {
        line_t *cur;

        vi_ensure_visible_line(b);
        vi_render(b, &vis, ':', NULL);
        key = vi_read_visual_key(b, &vis, ':', NULL);
        if (key == -1) {
            break;
        }

        cur = b->cur ? b->cur : b->head;
        if (!cur) {
            cur = b->head;
        }
        if (vis.replace_mode) {
            switch (key) {
            case '\x1b':
                vis.replace_mode = 0;
                if (vis.cursor_col > 0) {
                    vis.cursor_col--;
                }
                break;
            case '\r':
            case '\n':
                vi_split_line(b, &vis);
                break;
            case 127:
            case '\b':
                if (vis.cursor_col > 0) {
                    vis.cursor_col--;
                }
                break;
            case 0x17:
                vi_erase_word_backward_insert(b, &vis);
                break;
            case 0x15:
                vi_erase_to_insert_anchor(b, &vis);
                break;
            case VI_KEY_UP:
            case VI_KEY_DOWN:
            case VI_KEY_LEFT:
            case VI_KEY_RIGHT:
            case VI_KEY_CTRL_LEFT:
            case VI_KEY_CTRL_RIGHT:
            case VI_KEY_HOME:
            case VI_KEY_END:
            case VI_KEY_PGUP:
            case VI_KEY_PGDN:
            case VI_KEY_DELETE:
                vi_move_arrow_insert(b, &vis, key);
                vi_set_insert_anchor(b, &vis);
                break;
            default:
                if (isprint(key) || key == '\t') {
                    vi_replace_insert_char(b, &vis, key);
                }
                break;
            }
            continue;
        }
        if (vis.insert_mode) {
            switch (key) {
            case '\x1b':
                vis.insert_mode = 0;
                if (vis.cursor_col > 0) {
                    vis.cursor_col--;
                }
                break;
            case '\r':
            case '\n':
                vi_split_line(b, &vis);
                break;
            case 127:
            case '\b':
                vi_backspace_char(b, &vis);
                break;
            case 0x17:
                vi_erase_word_backward_insert(b, &vis);
                break;
            case 0x15:
                vi_erase_to_insert_anchor(b, &vis);
                break;
            case VI_KEY_UP:
            case VI_KEY_DOWN:
            case VI_KEY_LEFT:
            case VI_KEY_RIGHT:
            case VI_KEY_CTRL_LEFT:
            case VI_KEY_CTRL_RIGHT:
            case VI_KEY_HOME:
            case VI_KEY_END:
            case VI_KEY_PGUP:
            case VI_KEY_PGDN:
            case VI_KEY_DELETE:
                vi_move_arrow_insert(b, &vis, key);
                vi_set_insert_anchor(b, &vis);
                break;
            default:
                if (isprint(key) || key == '\t') {
                    vi_insert_char(b, &vis, key);
                }
                break;
            }
            continue;
        }
        switch (key) {
        case VI_KEY_UP:
            key = 'k';
            break;
        case VI_KEY_DOWN:
            key = 'j';
            break;
        case VI_KEY_RIGHT:
            key = 'l';
            break;
        case VI_KEY_LEFT:
            key = 'h';
            break;
        case VI_KEY_CTRL_RIGHT:
            key = 'w';
            break;
        case VI_KEY_CTRL_LEFT:
            key = 'b';
            break;
        case VI_KEY_HOME:
            key = '0';
            break;
        case VI_KEY_END:
            key = '$';
            break;
        case VI_KEY_PGUP:
            key = '\x02';
            break;
        case VI_KEY_PGDN:
            key = '\x06';
            break;
        case VI_KEY_DELETE:
            key = 'x';
            break;
        default:
            break;
        }
        if (vis.pending_op) {
            if (key >= '1' && key <= '9') {
                vi_append_count(&vis, key - '0');
                continue;
            }
            if (key == '0' && vis.pending_count > 0) {
                vi_append_count(&vis, 0);
                continue;
            }
            vi_handle_pending_operator(b, &vis, key);
            continue;
        }
        if (vis.pending_z) {
            vis.pending_z = 0;
            switch (key) {
            case 'z':
            case '.':
                vi_reposition_current(b, &vis, 0);
                break;
            case '\r':
            case '\n':
                vi_reposition_current(b, &vis, 1);
                break;
            case '-':
                vi_reposition_current(b, &vis, 2);
                break;
            default:
                write(STDOUT_FILENO, "\a", 1);
                break;
            }
            continue;
        }
        if (vis.pending_big_z) {
            vis.pending_big_z = 0;
            switch (key) {
            case 'Z':
                if (b->modified) {
                    if (!b->filename) {
                        write(STDOUT_FILENO, "\a", 1);
                        break;
                    }
                    buf_write_file(b, b->filename, 0);
                }
                goto done;
            case 'Q':
                goto done;
            default:
                write(STDOUT_FILENO, "\a", 1);
                break;
            }
            continue;
        }

        if (key >= '1' && key <= '9') {
            vis.pending_g = 0;
            vi_append_count(&vis, key - '0');
            continue;
        }

        switch (key) {
        case '"':
            key = vi_read_visual_key(b, &vis, ':', NULL);
            if (key >= 'a' && key <= 'z') {
                vis.pending_reg = key;
            } else {
                vis.pending_reg = 0;
                write(STDOUT_FILENO, "\a", 1);
            }
            break;
        case 'h':
            vis.pending_g = 0;
            {
                int count = vi_take_count(&vis);

                while (count-- > 0 && vis.cursor_col > 0) {
                    vis.cursor_col--;
                }
            }
            break;
        case 'l':
            vis.pending_g = 0;
            {
                int count = vi_take_count(&vis);

                while (count-- > 0 && cur && vis.cursor_col < (int)cur->len) {
                    vis.cursor_col++;
                }
            }
            break;
        case 'j':
            vis.pending_g = 0;
            vi_move_vertical(b, vi_take_count(&vis));
            break;
        case 'k':
            vis.pending_g = 0;
            vi_move_vertical(b, -vi_take_count(&vis));
            break;
        case ')':
            vis.pending_g = 0;
            vi_move_sentence_forward(b, &vis, vi_take_count(&vis));
            break;
        case '(':
            vis.pending_g = 0;
            vi_move_sentence_backward(b, &vis, vi_take_count(&vis));
            break;
        case '}':
            vis.pending_g = 0;
            vi_move_paragraph_forward(b, &vis, vi_take_count(&vis));
            break;
        case '{':
            vis.pending_g = 0;
            vi_move_paragraph_backward(b, &vis, vi_take_count(&vis));
            break;
        case '+':
        case '\r':
        case '\n':
            vis.pending_g = 0;
            vi_move_line_first_nonblank(b, &vis, vi_take_count(&vis));
            break;
        case '-':
            vis.pending_g = 0;
            vi_move_line_first_nonblank(b, &vis, -vi_take_count(&vis));
            break;
        case 'w':
            vis.pending_g = 0;
            vi_move_word_forward_count(b, &vis, vi_take_count(&vis));
            break;
        case 'b':
            vis.pending_g = 0;
            vi_move_word_backward_count(b, &vis, vi_take_count(&vis));
            break;
        case 'e':
            if (vis.pending_g) {
                int count = vi_take_count(&vis);

                vis.pending_g = 0;
                vi_move_word_end_backward_count(b, &vis, count);
            } else {
                vis.pending_g = 0;
                {
                    int count = vi_take_count(&vis);

                    while (count-- > 0) {
                        vi_move_word_end(b, &vis);
                    }
                }
            }
            break;
        case 'W':
            vis.pending_g = 0;
            vi_move_bigword_forward_count(b, &vis, vi_take_count(&vis));
            break;
        case 'B':
            vis.pending_g = 0;
            vi_move_bigword_backward_count(b, &vis, vi_take_count(&vis));
            break;
        case 'E':
            if (vis.pending_g) {
                int count = vi_take_count(&vis);

                vis.pending_g = 0;
                vi_move_bigword_end_backward_count(b, &vis, count);
            } else {
                vis.pending_g = 0;
                {
                    int count = vi_take_count(&vis);

                    while (count-- > 0) {
                        vi_move_bigword_end(b, &vis);
                    }
                }
            }
            break;
        case 'f':
            vis.pending_g = 0;
            key = vi_read_visual_key(b, &vis, ':', NULL);
            if (key == -1 || key == '\x1b' || key == '\r' || key == '\n') {
                write(STDOUT_FILENO, "\a", 1);
                break;
            }
            vi_find_char_motion(b, &vis, key, 1, 0, vi_take_count(&vis));
            break;
        case 'F':
            vis.pending_g = 0;
            key = vi_read_visual_key(b, &vis, ':', NULL);
            if (key == -1 || key == '\x1b' || key == '\r' || key == '\n') {
                write(STDOUT_FILENO, "\a", 1);
                break;
            }
            vi_find_char_motion(b, &vis, key, 0, 0, vi_take_count(&vis));
            break;
        case 't':
            vis.pending_g = 0;
            key = vi_read_visual_key(b, &vis, ':', NULL);
            if (key == -1 || key == '\x1b' || key == '\r' || key == '\n') {
                write(STDOUT_FILENO, "\a", 1);
                break;
            }
            vi_find_char_motion(b, &vis, key, 1, 1, vi_take_count(&vis));
            break;
        case 'T':
            vis.pending_g = 0;
            key = vi_read_visual_key(b, &vis, ':', NULL);
            if (key == -1 || key == '\x1b' || key == '\r' || key == '\n') {
                write(STDOUT_FILENO, "\a", 1);
                break;
            }
            vi_find_char_motion(b, &vis, key, 0, 1, vi_take_count(&vis));
            break;
        case ';':
            vis.pending_g = 0;
            vi_repeat_find_motion(b, &vis, 0, vi_take_count(&vis));
            break;
        case ',':
            vis.pending_g = 0;
            vi_repeat_find_motion(b, &vis, 1, vi_take_count(&vis));
            break;
        case '0':
            if (vis.pending_count > 0) {
                vi_append_count(&vis, 0);
            } else {
                vis.pending_g = 0;
                vis.cursor_col = 0;
            }
            break;
        case '^':
            vis.pending_g = 0;
            vi_take_count(&vis);
            vis.cursor_col = vi_first_nonblank_col(cur);
            break;
        case '_':
            if (vis.pending_g) {
                int line_no = buf_current_line(b);
                int count = vi_take_count(&vis);
                int target = vi_clamp_line_target(b, line_no + count - 1);

                b->cur = buf_get_line(b, target);
                vis.cursor_col = vi_last_nonblank_col(b->cur);
                vis.pending_g = 0;
            } else {
                int line_no = buf_current_line(b);
                int count = vi_take_count(&vis);
                int target = vi_clamp_line_target(b, line_no + count - 1);

                vis.pending_g = 0;
                if (b->line_count > 0) {
                    b->cur = buf_get_line(b, target);
                    vis.cursor_col = vi_first_nonblank_col(b->cur);
                }
            }
            break;
        case '|':
            vis.pending_g = 0;
            {
                int target = vi_take_count(&vis) - 1;

                if (target < 0) {
                    target = 0;
                }
                if (cur) {
                    vis.cursor_col = vi_index_for_display_col(cur, target);
                } else {
                    vis.cursor_col = 0;
                }
            }
            break;
        case '$':
            vis.pending_g = 0;
            vi_move_to_eol_count(b, &vis, vi_take_count(&vis));
            break;
        case '%':
            vis.pending_g = 0;
            if (vis.pending_count > 0) {
                vi_move_to_percent(b, &vis, vi_take_count(&vis));
            } else {
                vi_match_motion(b, &vis);
            }
            break;
        case 'i':
            vis.pending_g = 0;
            save_undo(b);
            vis.insert_mode = 1;
            vi_set_insert_anchor(b, &vis);
            break;
        case 'I':
            vis.pending_g = 0;
            vis.cursor_col = vi_first_nonblank_col(cur);
            save_undo(b);
            vis.insert_mode = 1;
            vi_set_insert_anchor(b, &vis);
            break;
        case 'a':
            vis.pending_g = 0;
            if (cur && vis.cursor_col < (int)cur->len) {
                vis.cursor_col++;
            }
            save_undo(b);
            vis.insert_mode = 1;
            vi_set_insert_anchor(b, &vis);
            break;
        case 'A':
            vis.pending_g = 0;
            if (cur) {
                vis.cursor_col = (int)cur->len;
            } else {
                vis.cursor_col = 0;
            }
            save_undo(b);
            vis.insert_mode = 1;
            vi_set_insert_anchor(b, &vis);
            break;
        case 'R':
            vis.pending_g = 0;
            save_undo(b);
            vis.insert_mode = 0;
            vis.replace_mode = 1;
            vi_set_insert_anchor(b, &vis);
            break;
        case 'o':
            vis.pending_g = 0;
            vi_open_line(b, &vis, 0);
            break;
        case 'O':
            vis.pending_g = 0;
            vi_open_line(b, &vis, 1);
            break;
        case 'x':
            vis.pending_g = 0;
            {
                int count = vi_take_count(&vis);

                if (count == 1) {
                    vi_delete_char(b, &vis);
                } else if (cur && (size_t)vis.cursor_col < cur->len) {
                    int end = vis.cursor_col + count;

                    vi_delete_span(b, &vis, vis.cursor_col, end, 0);
                }
                vi_set_last_change(&vis, VI_REPEAT_X, count, 0);
            }
            break;
        case 'X':
            vis.pending_g = 0;
            {
                int count = vi_take_count(&vis);
                int repeat_count = count;

                while (count-- > 0 && vis.cursor_col > 0) {
                    vi_delete_prev_char(b, &vis);
                }
                vi_set_last_change(&vis, VI_REPEAT_X_BACK, repeat_count, 0);
            }
            break;
        case 'r':
            vis.pending_g = 0;
            key = vi_read_visual_key(b, &vis, ':', NULL);
            if (key == -1 || key == '\x1b' || key == '\r' || key == '\n') {
                write(STDOUT_FILENO, "\a", 1);
                break;
            }
            if (isprint(key) || key == '\t') {
                vi_replace_char(b, &vis, key);
                vi_set_last_change(&vis, VI_REPEAT_R, 1, key);
            }
            break;
        case '~':
            vis.pending_g = 0;
            {
                int count = vi_take_count(&vis);

                vi_toggle_case(b, &vis, count);
                vi_set_last_change(&vis, VI_REPEAT_TILDE, count, 0);
            }
            break;
        case 's':
            vis.pending_g = 0;
            if (cur && (size_t)vis.cursor_col < cur->len) {
                vi_delete_span(b, &vis, vis.cursor_col, vis.cursor_col + 1, 1);
            } else {
                save_undo(b);
                vis.insert_mode = 1;
            }
            break;
        case 'S':
            vis.pending_g = 0;
            vi_substitute_line(b, &vis);
            break;
        case 'u':
        case 0x12:
            vis.pending_g = 0;
            vis.pending_count = 0;
            handle_undo_command(b);
            vi_clamp_cursor(b, &vis);
            break;
        case 'c':
            vis.pending_g = 0;
            vis.pending_op = 'c';
            break;
        case 'd':
            vis.pending_g = 0;
            vis.pending_op = 'd';
            break;
        case 'y':
            vis.pending_g = 0;
            vis.pending_op = 'y';
            break;
        case 'p':
            vis.pending_g = 0;
            {
                int count = vi_take_count(&vis);
                int repeat_count = count;

                vi_put(b, &vis, 0, count);
                vi_set_last_change(&vis, VI_REPEAT_P, repeat_count, 0);
            }
            break;
        case 'P':
            vis.pending_g = 0;
            {
                int count = vi_take_count(&vis);
                int repeat_count = count;

                vi_put(b, &vis, 1, count);
                vi_set_last_change(&vis, VI_REPEAT_P_BEFORE, repeat_count, 0);
            }
            break;
        case 'J':
            vis.pending_g = 0;
            {
                int count = vi_take_count(&vis);
                int line_no = buf_current_line(b);
                int last = vi_clamp_line_target(b, line_no + count - 1);

                if (b->cur && b->cur->next && last > line_no) {
                    handle_join_command(b, 1, line_no, last);
                    vi_clamp_cursor(b, &vis);
                } else if (b->cur && b->cur->next) {
                    handle_join_command(b, 1, line_no, line_no + 1);
                    vi_clamp_cursor(b, &vis);
                } else {
                    write(STDOUT_FILENO, "\a", 1);
                }
                vi_set_last_change(&vis, VI_REPEAT_J, count, 0);
            }
            break;
        case 'g':
            if (vis.pending_g) {
                int target = vis.pending_count > 0 ? vis.pending_count : 1;

                b->cur = buf_get_line(b, vi_clamp_line_target(b, target));
                vis.cursor_col = vi_first_nonblank_col(b->cur);
                vis.top_line = 1;
                vis.pending_g = 0;
                vis.pending_count = 0;
            } else {
                vis.pending_g = 1;
            }
            break;
        case 'm':
            vis.pending_g = 0;
            key = vi_read_visual_key(b, &vis, ':', NULL);
            if (key >= 'a' && key <= 'z' && b->cur) {
                b->marks[key - 'a'] = b->cur;
                b->mark_cols[key - 'a'] = vis.cursor_col;
            } else {
                write(STDOUT_FILENO, "\a", 1);
            }
            break;
        case '\'':
        case '`':
            vis.pending_g = 0;
            key = vi_read_visual_key(b, &vis, ':', NULL);
            if (key >= 'a' && key <= 'z' && b->marks[key - 'a']) {
                b->cur = b->marks[key - 'a'];
                if (key == '\'') {
                    vis.cursor_col = vi_first_nonblank_col(b->cur);
                } else {
                    vis.cursor_col = b->mark_cols[key - 'a'];
                    if ((size_t)vis.cursor_col > b->cur->len) {
                        vis.cursor_col = (int)b->cur->len;
                    }
                }
            } else {
                write(STDOUT_FILENO, "\a", 1);
            }
            break;
        case 'z':
            vis.pending_g = 0;
            vis.pending_z = 1;
            break;
        case 'Z':
            vis.pending_g = 0;
            vis.pending_count = 0;
            vis.pending_big_z = 1;
            break;
        case 'G':
            vis.pending_g = 0;
            {
                int target = vis.pending_count > 0 ? vis.pending_count : b->line_count;

                vis.pending_count = 0;
                if (b->line_count > 0) {
                    b->cur = buf_get_line(b, vi_clamp_line_target(b, target));
                    vis.cursor_col = vi_first_nonblank_col(b->cur);
                }
            }
            break;
        case 'H':
            vis.pending_g = 0;
            if (b->line_count > 0) {
                b->cur = buf_get_line(b, vi_screen_target_line(b, &vis, 0,
                    vis.pending_count > 0 ? vi_take_count(&vis) : 0));
                vis.cursor_col = vi_first_nonblank_col(b->cur);
            } else {
                vi_take_count(&vis);
            }
            break;
        case 'M':
            vis.pending_g = 0;
            if (b->line_count > 0) {
                vi_take_count(&vis);
                b->cur = buf_get_line(b, vi_screen_target_line(b, &vis, 1, 0));
                vis.cursor_col = vi_first_nonblank_col(b->cur);
            } else {
                vi_take_count(&vis);
            }
            break;
        case 'L':
            vis.pending_g = 0;
            if (b->line_count > 0) {
                b->cur = buf_get_line(b, vi_screen_target_line(b, &vis, 2,
                    vis.pending_count > 0 ? vi_take_count(&vis) : 0));
                vis.cursor_col = vi_first_nonblank_col(b->cur);
            } else {
                vi_take_count(&vis);
            }
            break;
        case ':':
            vis.pending_g = 0;
            vis.pending_count = 0;
            vi_command_prompt(b, &vis);
            break;
        case '/':
            vis.pending_g = 0;
            vis.pending_count = 0;
            vi_search_prompt(b, &vis, 1);
            break;
        case '?':
            vis.pending_g = 0;
            vis.pending_count = 0;
            vi_search_prompt(b, &vis, 0);
            break;
        case '*':
            vis.pending_g = 0;
            vis.pending_count = 0;
            vi_search_current_word(b, &vis, 1);
            break;
        case '#':
            vis.pending_g = 0;
            vis.pending_count = 0;
            vi_search_current_word(b, &vis, 0);
            break;
        case 'n':
            vis.pending_g = 0;
            vi_repeat_search(b, &vis, vis.last_search_forward, vi_take_count(&vis));
            break;
        case 'N':
            vis.pending_g = 0;
            vi_repeat_search(b, &vis, !vis.last_search_forward, vi_take_count(&vis));
            break;
        case '\f':
            vis.pending_g = 0;
            vis.pending_count = 0;
            printf("\x1b[2J");
            break;
        case 0x02:
            vis.pending_g = 0;
            vi_page_scroll(b, &vis, -vi_take_count(&vis));
            break;
        case 0x04:
            vis.pending_g = 0;
            vi_half_page_scroll(b, &vis, vi_take_count(&vis));
            break;
        case 0x06:
            vis.pending_g = 0;
            vi_page_scroll(b, &vis, vi_take_count(&vis));
            break;
        case 0x15:
            vis.pending_g = 0;
            vi_half_page_scroll(b, &vis, -vi_take_count(&vis));
            break;
        case 'D':
            vis.pending_g = 0;
            vis.pending_count = 0;
            vi_delete_span(b, &vis, vis.cursor_col,
                cur ? (int)cur->len : vis.cursor_col, 0);
            vi_set_last_change(&vis, VI_REPEAT_D_EOL, 1, 0);
            break;
        case 'C':
            vis.pending_g = 0;
            vis.pending_count = 0;
            vi_delete_span(b, &vis, vis.cursor_col,
                cur ? (int)cur->len : vis.cursor_col, 1);
            break;
        case 'Y':
            vis.pending_g = 0;
            {
                int count = vi_take_count(&vis);
                int line_no = buf_current_line(b);
                int last_line = vi_clamp_line_target(b, line_no + count - 1);

                vi_linewise_yank(b, &vis, line_no, last_line);
            }
            break;
        case '.':
            vis.pending_g = 0;
            vi_repeat_last_change(b, &vis);
            break;
        default:
            vis.pending_g = 0;
            vis.pending_count = 0;
            break;
        }
        if (vis.pending_reg && key != '"' && key != 'd' && key != 'y' && key != 'c') {
            vis.pending_reg = 0;
        }
    }

done:
    if (have_winch) {
        sigaction(SIGWINCH, &old_winch, NULL);
    }
    vi_restore_terminal();
    return 0;
}
