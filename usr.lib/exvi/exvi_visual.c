#include <exvi.h>
#include "exvi_internal.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>
#include <sys/ioctl.h>

typedef struct {
    struct termios saved_tio;
    int raw_active;
    int rows;
    int cols;
    int top_line;
    int cursor_col;
    int pending_g;
} vi_visual_t;

static vi_visual_t *active_visual = NULL;

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
vi_render(buffer_t *b, vi_visual_t *vis, const char *prompt)
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
        printf(":%s", prompt);
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
vi_read_key(void)
{
    char c;

    if (read(STDIN_FILENO, &c, 1) != 1) {
        return -1;
    }
    if (c == '\x1b') {
        char seq[2];

        if (read(STDIN_FILENO, &seq[0], 1) != 1) {
            return '\x1b';
        }
        if (read(STDIN_FILENO, &seq[1], 1) != 1) {
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
vi_page_scroll(buffer_t *b, vi_visual_t *vis, int direction)
{
    int page = vis->rows - 2;

    if (page < 1) {
        page = 1;
    }
    vi_move_vertical(b, direction * page);
}

static void
vi_command_prompt(buffer_t *b, vi_visual_t *vis)
{
    char cmd[256];
    size_t len = 0;

    cmd[0] = '\0';
    for (;;) {
        int key;

        vi_render(b, vis, cmd);
        key = vi_read_key();
        if (key == -1) {
            return;
        }
        if (key == '\r' || key == '\n') {
            cmd[len] = '\0';
            exvi_execute_command(b, cmd);
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

int
exvi_visual_main(buffer_t *b)
{
    vi_visual_t vis;
    int key;

    memset(&vis, 0, sizeof(vis));
    vis.top_line = 1;
    if (!b->cur) {
        b->cur = b->head;
    }
    if (vi_enable_raw(&vis) != 0) {
        return 1;
    }
    printf("\x1b[2J");

    for (;;) {
        line_t *cur;

        vi_render(b, &vis, NULL);
        key = vi_read_key();
        if (key == -1) {
            break;
        }

        cur = b->cur ? b->cur : b->head;
        if (!cur) {
            cur = b->head;
        }
        switch (key) {
        case 'h':
            vis.pending_g = 0;
            if (vis.cursor_col > 0) {
                vis.cursor_col--;
            }
            break;
        case 'l':
            vis.pending_g = 0;
            if (cur && vis.cursor_col < (int)cur->len) {
                vis.cursor_col++;
            }
            break;
        case 'j':
            vis.pending_g = 0;
            vi_move_vertical(b, 1);
            break;
        case 'k':
            vis.pending_g = 0;
            vi_move_vertical(b, -1);
            break;
        case '0':
            vis.pending_g = 0;
            vis.cursor_col = 0;
            break;
        case '$':
            vis.pending_g = 0;
            if (cur) {
                vis.cursor_col = (int)cur->len;
            }
            break;
        case 'g':
            if (vis.pending_g) {
                b->cur = buf_get_line(b, 1);
                vis.top_line = 1;
                vis.pending_g = 0;
            } else {
                vis.pending_g = 1;
            }
            break;
        case 'G':
            vis.pending_g = 0;
            if (b->line_count > 0) {
                b->cur = buf_get_line(b, b->line_count);
            }
            break;
        case ':':
            vis.pending_g = 0;
            vi_command_prompt(b, &vis);
            break;
        case '\f':
            vis.pending_g = 0;
            printf("\x1b[2J");
            break;
        case 0x02:
            vis.pending_g = 0;
            vi_page_scroll(b, &vis, -1);
            break;
        case 0x06:
            vis.pending_g = 0;
            vi_page_scroll(b, &vis, 1);
            break;
        default:
            vis.pending_g = 0;
            break;
        }
    }

    vi_restore_terminal();
    return 0;
}
