#include <exvi.h>
#include "exvi_internal.h"

#include <ctype.h>
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
    VI_REPEAT_R,
    VI_REPEAT_DD,
    VI_REPEAT_DW,
    VI_REPEAT_D_EOL,
    VI_REPEAT_J,
    VI_REPEAT_P,
    VI_REPEAT_P_BEFORE,
} vi_repeat_kind_t;

typedef struct {
    struct termios saved_tio;
    int raw_active;
    int rows;
    int cols;
    int top_line;
    int cursor_col;
    int pending_g;
    int pending_op;
    int pending_count;
    int last_search_forward;
    int insert_mode;
    int replace_mode;
    vi_repeat_kind_t last_change;
    int last_change_count;
    int last_change_char;
} vi_visual_t;

static vi_visual_t *active_visual = NULL;

static int vi_first_nonblank_col(line_t *cur);

static void
vi_restore_terminal(void)
{
    if (active_visual && active_visual->raw_active) {
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &active_visual->saved_tio);
        active_visual->raw_active = 0;
        write(STDOUT_FILENO, "\x1b[?25h\x1b[0m\x1b[2J\x1b[H", 18);
    }
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
vi_scroll_into_view(buffer_t *b, vi_visual_t *vis)
{
    int cur_line = buf_current_line(b);
    int visible_rows = vis->rows - 1;

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
}

static void
vi_draw_line(const char *text, int cols, int number, int line_no)
{
    int used = 0;

    if (number) {
        used += printf("%6d  ", line_no);
    }
    while (*text && used < cols) {
        unsigned char c = (unsigned char)*text++;

        if (c == '\t') {
            if (used + 1 >= cols) {
                break;
            }
            putchar('^');
            putchar('I');
            used += 2;
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
    }
}

static void
vi_render(buffer_t *b, vi_visual_t *vis, char prompt_prefix, const char *prompt)
{
    int cur_line = buf_current_line(b);
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
            vi_draw_line(line->text, vis->cols, option_number, line_no);
        } else {
            putchar('~');
        }
        if (row < vis->rows - 2) {
            putchar('\n');
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
    cursor_col = vis->cursor_col + 1 + (option_number ? 8 : 0);
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
        if (seq[0] == '[') {
            switch (seq[1]) {
            case 'A': return 'k';
            case 'B': return 'j';
            case 'C': return 'l';
            case 'D': return 'h';
            default: return '\x1b';
            }
        }
        return '\x1b';
    }
    return (unsigned char)c;
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

static void
vi_move_to_screen_line(buffer_t *b, vi_visual_t *vis, int screen_row)
{
    int visible_rows = vis->rows - 1;
    int target_line;

    if (visible_rows < 1) {
        visible_rows = 1;
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
    b->cur = buf_get_line(b, target_line);
    vis->cursor_col = vi_first_nonblank_col(b->cur);
}

static int
vi_is_word_char(int ch)
{
    return isalnum((unsigned char)ch) || ch == '_';
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
    }
    free(text);
}

static void
vi_linewise_put(buffer_t *b, vi_visual_t *vis, int before)
{
    int line_no = buf_current_line(b);

    if (line_no < 1) {
        line_no = 1;
    }
    handle_put_command(b, "", before ? line_no - 1 : line_no);
    if (before) {
        if (line_no > 1) {
            b->cur = buf_get_line(b, line_no);
        } else {
            b->cur = buf_get_line(b, 1);
        }
    } else {
        b->cur = buf_get_line(b, line_no + 1);
    }
    vis->cursor_col = 0;
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
}

static void
vi_handle_pending_operator(buffer_t *b, vi_visual_t *vis, int key)
{
    int line_no = buf_current_line(b);
    int count = vi_take_count(vis);
    int end;
    int last_line = vi_clamp_line_target(b, line_no + count - 1);

    if (vis->pending_op == 'd' && key == 'd') {
        handle_delete_command(b, 1, line_no, last_line);
        if (!b->cur && b->head) {
            b->cur = b->head;
        }
        vis->cursor_col = 0;
        vi_set_last_change(vis, VI_REPEAT_DD, count, 0);
    } else if (vis->pending_op == 'c' && key == 'c') {
        if (count == 1) {
            vi_substitute_line(b, vis);
        } else {
            line_t *start = buf_get_line(b, line_no);
            line_t *before = start ? start->prev : NULL;

            handle_delete_command(b, 1, line_no, last_line);
            b->cur = buf_insert_after(b, before, "");
            vis->cursor_col = 0;
            vis->insert_mode = 1;
            vis->replace_mode = 0;
        }
    } else if (vis->pending_op == 'y' && key == 'y') {
        handle_yank_command(b, "", 1, line_no, last_line);
        vis->cursor_col = 0;
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
    } else if ((vis->pending_op == 'd' || vis->pending_op == 'c') && key == '$') {
        line_t *cur = b->cur;

        if (cur && (size_t)vis->cursor_col <= cur->len) {
            vi_delete_span(b, vis, vis->cursor_col, (int)cur->len,
                vis->pending_op == 'c');
            if (vis->pending_op == 'd') {
                vi_set_last_change(vis, VI_REPEAT_D_EOL, 1, 0);
            }
        } else {
            write(STDOUT_FILENO, "\a", 1);
        }
    } else {
        write(STDOUT_FILENO, "\a", 1);
    }
    vis->pending_op = 0;
}

static void
vi_insert_char(buffer_t *b, vi_visual_t *vis, int ch)
{
    line_t *cur = b->cur;
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
    line_t *cur = b->cur;
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
    char *text;

    if (!cur || vis->cursor_col <= 0) {
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
}

static void
vi_command_prompt(buffer_t *b, vi_visual_t *vis)
{
    char cmd[256];
    size_t len = 0;

    cmd[0] = '\0';
    for (;;) {
        int key;

        vi_render(b, vis, ':', cmd);
        key = vi_read_key();
        if (key == -1) {
            return;
        }
        if (key == '\r' || key == '\n') {
            cmd[len] = '\0';
            exvi_execute_command(b, cmd);
            vi_clamp_cursor(b, vis);
            return;
        }
        if (key == '\x1b') {
            return;
        }
        if (key == 127 || key == '\b') {
            if (len > 0) {
                cmd[--len] = '\0';
            }
            continue;
        }
        if (isprint(key) && len + 1 < sizeof(cmd)) {
            cmd[len++] = (char)key;
            cmd[len] = '\0';
        }
    }
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
    case VI_REPEAT_R:
        if (!cur || (size_t)vis->cursor_col >= cur->len) {
            write(STDOUT_FILENO, "\a", 1);
            return;
        }
        vi_replace_char(b, vis, vis->last_change_char);
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
    case VI_REPEAT_D_EOL:
        vi_delete_span(b, vis, vis->cursor_col,
            cur ? (int)cur->len : vis->cursor_col, 0);
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
        while (count-- > 0) {
            vi_linewise_put(b, vis, 0);
        }
        break;
    case VI_REPEAT_P_BEFORE:
        while (count-- > 0) {
            vi_linewise_put(b, vis, 1);
        }
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
    size_t len = 0;
    char prefix = forward ? '/' : '?';

    pattern[0] = '\0';
    for (;;) {
        int key;

        vi_render(b, vis, prefix, pattern);
        key = vi_read_key();
        if (key == -1) {
            return;
        }
        if (key == '\r' || key == '\n') {
            pattern[len] = '\0';
            vi_apply_search(b, vis, pattern, forward);
            return;
        }
        if (key == '\x1b') {
            return;
        }
        if (key == 127 || key == '\b') {
            if (len > 0) {
                pattern[--len] = '\0';
            }
            continue;
        }
        if (isprint(key) && len + 1 < sizeof(pattern)) {
            pattern[len++] = (char)key;
            pattern[len] = '\0';
        }
    }
}

int
exvi_visual_main(buffer_t *b)
{
    vi_visual_t vis;
    int key;

    memset(&vis, 0, sizeof(vis));
    vis.top_line = 1;
    vis.last_search_forward = 1;
    if (!b->cur) {
        b->cur = b->head;
    }
    if (vi_enable_raw(&vis) != 0) {
        return 1;
    }
    printf("\x1b[2J");

    for (;;) {
        line_t *cur;

        vi_render(b, &vis, ':', NULL);
        key = vi_read_key();
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
            default:
                if (isprint(key) || key == '\t') {
                    vi_insert_char(b, &vis, key);
                }
                break;
            }
            continue;
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

        if (key >= '1' && key <= '9') {
            vis.pending_g = 0;
            vi_append_count(&vis, key - '0');
            continue;
        }

        switch (key) {
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
            vis.pending_g = 0;
            {
                int count = vi_take_count(&vis);

                while (count-- > 0) {
                    vi_move_word_end(b, &vis);
                }
            }
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
        case '$':
            vis.pending_g = 0;
            vi_take_count(&vis);
            if (cur) {
                vis.cursor_col = (int)cur->len;
            }
            break;
        case 'i':
            vis.pending_g = 0;
            save_undo(b);
            vis.insert_mode = 1;
            break;
        case 'I':
            vis.pending_g = 0;
            vis.cursor_col = 0;
            save_undo(b);
            vis.insert_mode = 1;
            break;
        case 'a':
            vis.pending_g = 0;
            if (cur && vis.cursor_col < (int)cur->len) {
                vis.cursor_col++;
            }
            save_undo(b);
            vis.insert_mode = 1;
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
            break;
        case 'R':
            vis.pending_g = 0;
            save_undo(b);
            vis.insert_mode = 0;
            vis.replace_mode = 1;
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
        case 'r':
            vis.pending_g = 0;
            key = vi_read_key();
            if (key == -1 || key == '\x1b' || key == '\r' || key == '\n') {
                write(STDOUT_FILENO, "\a", 1);
                break;
            }
            if (isprint(key) || key == '\t') {
                vi_replace_char(b, &vis, key);
                vi_set_last_change(&vis, VI_REPEAT_R, 1, key);
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
            vis.pending_g = 0;
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

                while (count-- > 0) {
                    vi_linewise_put(b, &vis, 0);
                }
                vi_set_last_change(&vis, VI_REPEAT_P, repeat_count, 0);
            }
            break;
        case 'P':
            vis.pending_g = 0;
            {
                int count = vi_take_count(&vis);
                int repeat_count = count;

                while (count-- > 0) {
                    vi_linewise_put(b, &vis, 1);
                }
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
                vis.top_line = 1;
                vis.pending_g = 0;
                vis.pending_count = 0;
            } else {
                vis.pending_g = 1;
            }
            break;
        case 'G':
            vis.pending_g = 0;
            {
                int target = vis.pending_count > 0 ? vis.pending_count : b->line_count;

                vis.pending_count = 0;
                if (b->line_count > 0) {
                    b->cur = buf_get_line(b, vi_clamp_line_target(b, target));
                }
            }
            break;
        case 'H':
            vis.pending_g = 0;
            vi_take_count(&vis);
            vi_move_to_screen_line(b, &vis, 0);
            break;
        case 'M':
            vis.pending_g = 0;
            vi_take_count(&vis);
            vi_move_to_screen_line(b, &vis, (vis.rows - 2) / 2);
            break;
        case 'L':
            vis.pending_g = 0;
            vi_take_count(&vis);
            vi_move_to_screen_line(b, &vis, vis.rows - 2);
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
        case '.':
            vis.pending_g = 0;
            vi_repeat_last_change(b, &vis);
            break;
        default:
            vis.pending_g = 0;
            vis.pending_count = 0;
            break;
        }
    }

    vi_restore_terminal();
    return 0;
}
