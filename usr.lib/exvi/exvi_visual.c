#include <exvi.h>
#include "exvi_internal.h"

#include <ctype.h>
#include <errno.h>
#include <regex.h>
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
    VI_REPEAT_INSERT,
    VI_REPEAT_C_WORD,
    VI_REPEAT_S,
    VI_REPEAT_C_EOL_CHANGE,
    VI_REPEAT_S_LINE,
    VI_REPEAT_CC,
    VI_REPEAT_C_CHARS,
    VI_REPEAT_C_END,
    VI_REPEAT_C_FIND,
    VI_REPEAT_C_TO_COL0,
    VI_REPEAT_C_TO_FIRST_NONBLANK,
    VI_REPEAT_C_SEARCH_REPEAT,
    VI_REPEAT_C_PERCENT,
    VI_REPEAT_C_BACK_END,
    VI_REPEAT_C_MOTION,
    VI_REPEAT_C_MARK_LINE,
    VI_REPEAT_C_MARK_EXACT,
    VI_REPEAT_C_LINE_MOTION,
} vi_repeat_kind_t;

typedef enum {
    VI_REPLACE_EDIT_CHAR = 0,
    VI_REPLACE_EDIT_INSERT,
    VI_REPLACE_EDIT_SPLIT,
} vi_replace_edit_kind_t;

typedef struct {
    vi_replace_edit_kind_t kind;
    line_t *line;
    line_t *aux_line;
    int col;
    char ch;
} vi_replace_edit_t;

#define VI_PROMPT_HISTORY_MAX 32

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
    int pending_op_count;
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
    int last_insert_line_no;
    int last_insert_col;
    int insert_entry_key;
    char *last_insert_text;
    vi_replace_edit_t *replace_edits;
    int replace_edit_count;
    int replace_edit_cap;
    vi_repeat_kind_t last_change;
    int last_change_count;
    int last_change_char;
    int last_change_aux;
    char status_msg[256];
    int status_once;
    char *cmd_history[VI_PROMPT_HISTORY_MAX];
    int cmd_history_len;
    char *search_history[VI_PROMPT_HISTORY_MAX];
    int search_history_len;
    int screen_dirty;
    int status_dirty;
    int clear_screen;
    int rendered_cur_line;
} vi_visual_t;

static vi_visual_t *active_visual = NULL;
static volatile sig_atomic_t vi_resize_pending = 0;

static int vi_first_nonblank_col(line_t *cur);
static int vi_display_col_for_index(line_t *cur, int idx);
static line_t *vi_ensure_current_line(buffer_t *b);
static int vi_replace_current_text(buffer_t *b, const char *text);
static void vi_open_line(buffer_t *b, vi_visual_t *vis, int above);
static int vi_clamp_line_target(buffer_t *b, int line_no);
static int vi_clamp_top_line(buffer_t *b, vi_visual_t *vis, int top_line);
static int vi_compare_positions(buffer_t *b, line_t *left, int left_col, line_t *right,
    int right_col);
static void vi_insert_char(buffer_t *b, vi_visual_t *vis, int ch);
static void vi_replace_insert_char(buffer_t *b, vi_visual_t *vis, int ch);
static void vi_split_line(buffer_t *b, vi_visual_t *vis);
static void vi_move_word_forward_count(buffer_t *b, vi_visual_t *vis, int count);
static void vi_move_word_backward_count(buffer_t *b, vi_visual_t *vis, int count);
static void vi_move_word_end_count(buffer_t *b, vi_visual_t *vis, int count);
static void vi_move_bigword_end_count(buffer_t *b, vi_visual_t *vis, int count);
static void vi_page_scroll(buffer_t *b, vi_visual_t *vis, int direction);
static void vi_delete_char(buffer_t *b, vi_visual_t *vis);
static void vi_linewise_yank(buffer_t *b, vi_visual_t *vis, int start, int end);
static void vi_linewise_delete(buffer_t *b, vi_visual_t *vis, int start, int end);
static void vi_linewise_change(buffer_t *b, vi_visual_t *vis, int start, int end);
static void vi_set_last_change(vi_visual_t *vis, vi_repeat_kind_t kind, int count, int ch);
static int vi_prompt_input(buffer_t *b, vi_visual_t *vis, char prefix, char *buf,
    size_t buf_size);
static int vi_read_visual_key(buffer_t *b, vi_visual_t *vis, char prompt_prefix,
    const char *prompt);
static char *vi_current_word_text(buffer_t *b, vi_visual_t *vis);
static char *vi_current_word_pattern(buffer_t *b, vi_visual_t *vis);
static void vi_visual_apply_tag_target(buffer_t *b, char *cmd);
static void vi_reset_replace_mode(vi_visual_t *vis);
static void vi_record_last_insert_site(buffer_t *b, vi_visual_t *vis);
static void vi_set_insert_anchor(buffer_t *b, vi_visual_t *vis);
static void vi_update_last_insert_text(buffer_t *b, vi_visual_t *vis);
static int vi_current_insert_span_nonempty(buffer_t *b, vi_visual_t *vis);
static void vi_start_change_insert(buffer_t *b, vi_visual_t *vis);
static void vi_finish_repeat_insert(buffer_t *b, vi_visual_t *vis);
static int vi_read_register_index_prompt(buffer_t *b, vi_visual_t *vis);
static char *vi_capture_register_text(int reg_idx);
static void vi_insert_quoted_key(buffer_t *b, vi_visual_t *vis);
static void vi_insert_register_text(buffer_t *b, vi_visual_t *vis);
static int vi_find_match_for_bracket(line_t *line, int match_col, line_t **line_out,
    int *col_out);
static int vi_find_scanned_bracket_target(line_t *line, int cursor_col, int motion_mode,
    line_t **line_out, int *col_out);
static int vi_find_scanned_cross_bracket_span(line_t *line, int cursor_col,
    int *start_col_out, line_t **line_out, int *col_out);
static int vi_match_operator_target(buffer_t *b, vi_visual_t *vis, line_t **line_out,
    int *col_out);
static int vi_is_word_char(int ch);

static void
vi_record_last_insert_site(buffer_t *b, vi_visual_t *vis)
{
    int line_no = buf_current_line(b);

    if (line_no < 1) {
        line_no = 1;
    }
    vis->last_insert_line_no = line_no;
    vis->last_insert_col = vis->cursor_col;
}

static void
vi_clear_replace_edits(vi_visual_t *vis)
{
    free(vis->replace_edits);
    vis->replace_edits = NULL;
    vis->replace_edit_count = 0;
    vis->replace_edit_cap = 0;
}

static char *
vi_capture_insert_span(buffer_t *b, vi_visual_t *vis)
{
    line_t *start_line = vis->insert_anchor_line;
    line_t *end_line = b->cur ? b->cur : b->head;
    int start_col = vis->insert_anchor_col;
    int end_col = vis->cursor_col;
    line_t *line;
    size_t total = 0;
    char *text;
    char *dst;

    if (!start_line) {
        start_line = end_line;
        start_col = 0;
    }
    if (!start_line || !end_line) {
        return strdup("");
    }
    if (vi_compare_positions(b, start_line, start_col, end_line, end_col) > 0) {
        return strdup("");
    }
    if (start_line == end_line) {
        if (start_col < 0) {
            start_col = 0;
        }
        if (end_col < start_col) {
            end_col = start_col;
        }
        if ((size_t)start_col > start_line->len) {
            start_col = (int)start_line->len;
        }
        if ((size_t)end_col > start_line->len) {
            end_col = (int)start_line->len;
        }
        text = malloc((size_t)(end_col - start_col) + 1);
        if (!text) {
            return NULL;
        }
        memcpy(text, start_line->text + start_col, (size_t)(end_col - start_col));
        text[end_col - start_col] = '\0';
        return text;
    }

    line = start_line;
    for (;;) {
        if (line == start_line) {
            size_t part = line->len;

            if (start_col < 0) {
                start_col = 0;
            }
            if ((size_t)start_col > line->len) {
                start_col = (int)line->len;
            }
            part -= (size_t)start_col;
            total += part;
        } else if (line == end_line) {
            if (end_col < 0) {
                end_col = 0;
            }
            if ((size_t)end_col > line->len) {
                end_col = (int)line->len;
            }
            total += (size_t)end_col;
        } else {
            total += line->len;
        }
        if (line == end_line) {
            break;
        }
        total++;
        line = line->next;
        if (!line) {
            return strdup("");
        }
    }

    text = malloc(total + 1);
    if (!text) {
        return NULL;
    }
    dst = text;
    line = start_line;
    for (;;) {
        if (line == start_line) {
            size_t part = line->len - (size_t)start_col;

            memcpy(dst, line->text + start_col, part);
            dst += part;
        } else if (line == end_line) {
            memcpy(dst, line->text, (size_t)end_col);
            dst += end_col;
        } else {
            memcpy(dst, line->text, line->len);
            dst += line->len;
        }
        if (line == end_line) {
            break;
        }
        *dst++ = '\n';
        line = line->next;
    }
    *dst = '\0';
    return text;
}

static void
vi_update_last_insert_text(buffer_t *b, vi_visual_t *vis)
{
    char *text = vi_capture_insert_span(b, vis);

    if (!text) {
        return;
    }
    if (text[0] != '\0') {
        free(vis->last_insert_text);
        vis->last_insert_text = text;
    } else {
        free(text);
    }
}

static int
vi_current_insert_span_nonempty(buffer_t *b, vi_visual_t *vis)
{
    char *text = vi_capture_insert_span(b, vis);
    int nonempty = (text && text[0] != '\0');

    free(text);
    return nonempty;
}

static int
vi_begin_repeat_insert(buffer_t *b, vi_visual_t *vis, int entry_key)
{
    line_t *cur = b->cur;

    switch (entry_key) {
    case 'i':
        save_undo(b);
        vis->insert_mode = 1;
        vis->replace_mode = 0;
        vis->insert_entry_key = entry_key;
        vi_record_last_insert_site(b, vis);
        vi_set_insert_anchor(b, vis);
        return 0;
    case 'a':
        if (cur && vis->cursor_col < (int)cur->len) {
            vis->cursor_col++;
        }
        save_undo(b);
        vis->insert_mode = 1;
        vis->replace_mode = 0;
        vis->insert_entry_key = entry_key;
        vi_record_last_insert_site(b, vis);
        vi_set_insert_anchor(b, vis);
        return 0;
    case 'I':
        vis->cursor_col = vi_first_nonblank_col(cur);
        save_undo(b);
        vis->insert_mode = 1;
        vis->replace_mode = 0;
        vis->insert_entry_key = entry_key;
        vi_record_last_insert_site(b, vis);
        vi_set_insert_anchor(b, vis);
        return 0;
    case 'A':
        vis->cursor_col = cur ? (int)cur->len : 0;
        save_undo(b);
        vis->insert_mode = 1;
        vis->replace_mode = 0;
        vis->insert_entry_key = entry_key;
        vi_record_last_insert_site(b, vis);
        vi_set_insert_anchor(b, vis);
        return 0;
    case 'o':
        vi_open_line(b, vis, 0);
        return 0;
    case 'O':
        vi_open_line(b, vis, 1);
        return 0;
    case 'R':
        save_undo(b);
        vis->insert_mode = 0;
        vi_clear_replace_edits(vis);
        vis->replace_mode = 1;
        vis->insert_entry_key = entry_key;
        vi_record_last_insert_site(b, vis);
        vi_set_insert_anchor(b, vis);
        return 0;
    default:
        return -1;
    }
}

static void
vi_replay_insert_text(buffer_t *b, vi_visual_t *vis, const char *text)
{
    const unsigned char *p = (const unsigned char *)text;

    if (!text || text[0] == '\0') {
        write(STDOUT_FILENO, "\a", 1);
        return;
    }
    while (*p != '\0') {
        if (*p == '\n') {
            vi_split_line(b, vis);
        } else if (vis->replace_mode) {
            vi_replace_insert_char(b, vis, *p);
        } else {
            vi_insert_char(b, vis, *p);
        }
        p++;
    }
}

static int
vi_read_register_index_prompt(buffer_t *b, vi_visual_t *vis)
{
    int key = vi_read_visual_key(b, vis, '"', NULL);

    if (key == -1) {
        return -1;
    }
    if (key == '"') {
        return 26;
    }
    if (key >= 'a' && key <= 'z') {
        return key - 'a';
    }
    if (key >= 'A' && key <= 'Z') {
        return key - 'A';
    }
    write(STDOUT_FILENO, "\a", 1);
    return -1;
}

static char *
vi_capture_register_text(int reg_idx)
{
    line_t *line;
    char *text;
    char *dst;
    size_t total = 0;

    if (reg_idx < 0 || reg_idx >= 27 || !regs[reg_idx].head) {
        return NULL;
    }
    for (line = regs[reg_idx].head; line; line = line->next) {
        total += line->len;
        if (line->next || reg_linewise[reg_idx]) {
            total++;
        }
    }
    text = malloc(total + 1);
    if (!text) {
        return NULL;
    }
    dst = text;
    for (line = regs[reg_idx].head; line; line = line->next) {
        memcpy(dst, line->text, line->len);
        dst += line->len;
        if (line->next || reg_linewise[reg_idx]) {
            *dst++ = '\n';
        }
    }
    *dst = '\0';
    return text;
}

static void
vi_insert_quoted_key(buffer_t *b, vi_visual_t *vis)
{
    int key = vi_read_visual_key(b, vis, '^', NULL);
    unsigned char ch;

    if (key == -1) {
        return;
    }
    if (key >= VI_KEY_UNKNOWN) {
        write(STDOUT_FILENO, "\a", 1);
        return;
    }
    ch = (unsigned char)key;
    if (vis->replace_mode) {
        vi_replace_insert_char(b, vis, (int)ch);
    } else {
        vi_insert_char(b, vis, (int)ch);
    }
}

static void
vi_insert_register_text(buffer_t *b, vi_visual_t *vis)
{
    int reg_idx = vi_read_register_index_prompt(b, vis);
    char *text;

    if (reg_idx < 0) {
        return;
    }
    text = vi_capture_register_text(reg_idx);
    if (!text || text[0] == '\0') {
        free(text);
        write(STDOUT_FILENO, "\a", 1);
        return;
    }
    vi_replay_insert_text(b, vis, text);
    free(text);
}

static int
vi_push_replace_edit(vi_visual_t *vis, const vi_replace_edit_t *edit)
{
    vi_replace_edit_t *tmp;
    int new_cap;

    if (vis->replace_edit_count == vis->replace_edit_cap) {
        new_cap = vis->replace_edit_cap ? vis->replace_edit_cap * 2 : 16;
        tmp = realloc(vis->replace_edits, sizeof(*tmp) * (size_t)new_cap);
        if (!tmp) {
            return -1;
        }
        vis->replace_edits = tmp;
        vis->replace_edit_cap = new_cap;
    }
    vis->replace_edits[vis->replace_edit_count++] = *edit;
    return 0;
}

static void
vi_record_replace_char(vi_visual_t *vis, line_t *line, int col, char ch)
{
    vi_replace_edit_t edit = {
        .kind = VI_REPLACE_EDIT_CHAR,
        .line = line,
        .aux_line = NULL,
        .col = col,
        .ch = ch,
    };

    vi_push_replace_edit(vis, &edit);
}

static void
vi_record_replace_insert(vi_visual_t *vis, line_t *line, int col)
{
    vi_replace_edit_t edit = {
        .kind = VI_REPLACE_EDIT_INSERT,
        .line = line,
        .aux_line = NULL,
        .col = col,
        .ch = '\0',
    };

    vi_push_replace_edit(vis, &edit);
}

static void
vi_record_replace_split(vi_visual_t *vis, line_t *left, line_t *right, int col)
{
    vi_replace_edit_t edit = {
        .kind = VI_REPLACE_EDIT_SPLIT,
        .line = left,
        .aux_line = right,
        .col = col,
        .ch = '\0',
    };

    vi_push_replace_edit(vis, &edit);
}

static int
vi_undo_replace_edit(buffer_t *b, vi_visual_t *vis)
{
    vi_replace_edit_t edit;
    line_t *line;
    char *text;
    size_t right_len;

    if (vis->replace_edit_count <= 0) {
        return -1;
    }

    edit = vis->replace_edits[--vis->replace_edit_count];
    switch (edit.kind) {
    case VI_REPLACE_EDIT_CHAR:
        line = edit.line;
        if (!line || edit.col < 0 || (size_t)edit.col >= line->len) {
            return -1;
        }
        text = strdup(line->text);
        if (!text) {
            vis->replace_edit_count++;
            return -1;
        }
        text[edit.col] = edit.ch;
        b->cur = line;
        if (vi_replace_current_text(b, text) != 0) {
            free(text);
            vis->replace_edit_count++;
            return -1;
        }
        free(text);
        vis->cursor_col = edit.col;
        return 0;
    case VI_REPLACE_EDIT_INSERT:
        line = edit.line;
        if (!line || edit.col < 0 || (size_t)edit.col >= line->len) {
            return -1;
        }
        text = malloc(line->len);
        if (!text) {
            vis->replace_edit_count++;
            return -1;
        }
        memcpy(text, line->text, (size_t)edit.col);
        memcpy(text + edit.col, line->text + edit.col + 1,
            line->len - (size_t)edit.col);
        b->cur = line;
        if (vi_replace_current_text(b, text) != 0) {
            free(text);
            vis->replace_edit_count++;
            return -1;
        }
        free(text);
        vis->cursor_col = edit.col;
        return 0;
    case VI_REPLACE_EDIT_SPLIT:
        if (!edit.line || !edit.aux_line) {
            return -1;
        }
        right_len = edit.aux_line->len;
        text = malloc(edit.line->len + right_len + 1);
        if (!text) {
            vis->replace_edit_count++;
            return -1;
        }
        memcpy(text, edit.line->text, edit.line->len);
        memcpy(text + edit.line->len, edit.aux_line->text, right_len + 1);
        b->cur = edit.line;
        if (vi_replace_current_text(b, text) != 0) {
            free(text);
            vis->replace_edit_count++;
            return -1;
        }
        free(text);
        if (vis->insert_anchor_line == edit.aux_line) {
            vis->insert_anchor_line = edit.line;
            vis->insert_anchor_col = edit.col;
        }
        b->cur = edit.aux_line;
        buf_delete(b, edit.aux_line);
        b->cur = edit.line;
        vis->cursor_col = edit.col;
        return 0;
    default:
        return -1;
    }
}

static int
vi_rewind_replace_to(buffer_t *b, vi_visual_t *vis, line_t *target_line, int target_col)
{
    int progress = 0;

    if (!target_line) {
        return -1;
    }
    while (b->cur && (b->cur != target_line || vis->cursor_col != target_col)) {
        if (vi_undo_replace_edit(b, vis) != 0) {
            return progress ? 0 : -1;
        }
        progress = 1;
    }
    return 0;
}

static void
vi_reset_replace_mode(vi_visual_t *vis)
{
    vis->replace_mode = 0;
    vi_clear_replace_edits(vis);
}

static void
vi_set_status(vi_visual_t *vis, const char *msg)
{
    if (!msg) {
        vis->status_msg[0] = '\0';
        vis->status_once = 0;
        vis->status_dirty = 1;
        return;
    }
    snprintf(vis->status_msg, sizeof(vis->status_msg), "%s", msg);
    vis->status_once = 1;
    vis->status_dirty = 1;
}

static void
vi_prompt_history_add(char **entries, int *count, const char *text)
{
    char *copy;

    if (!entries || !count || !text || text[0] == '\0') {
        return;
    }
    if (*count > 0 && strcmp(entries[*count - 1], text) == 0) {
        return;
    }

    copy = strdup(text);
    if (!copy) {
        return;
    }

    if (*count == VI_PROMPT_HISTORY_MAX) {
        free(entries[0]);
        memmove(&entries[0], &entries[1],
            sizeof(entries[0]) * (size_t)(VI_PROMPT_HISTORY_MAX - 1));
        entries[VI_PROMPT_HISTORY_MAX - 1] = copy;
        return;
    }

    entries[*count] = copy;
    (*count)++;
}

static void
vi_prompt_history_free(char **entries, int count)
{
    int i;

    for (i = 0; i < count; i++) {
        free(entries[i]);
    }
}

static void
vi_display_search_pattern(const char *pattern, char *buf, size_t buf_size)
{
    size_t len;
    size_t i;
    size_t j = 0;

    if (!buf || buf_size == 0) {
        return;
    }
    buf[0] = '\0';
    if (!pattern) {
        return;
    }
    len = strlen(pattern);
    if (len < 4 || strncmp(pattern, "\\<", 2) != 0 ||
        pattern[len - 2] != '\\' || pattern[len - 1] != '>') {
        strlcpy(buf, pattern, buf_size);
        return;
    }
    for (i = 2; i + 2 < len; i++) {
        unsigned char ch = (unsigned char)pattern[i];

        if (ch == '\\' && i + 3 < len) {
            ch = (unsigned char)pattern[++i];
        }
        if (!vi_is_word_char(ch) || j + 1 >= buf_size) {
            strlcpy(buf, pattern, buf_size);
            return;
        }
        buf[j++] = (char)ch;
    }
    buf[j] = '\0';
}

static void
vi_set_search_failure_status(vi_visual_t *vis, const char *pattern)
{
    const char *effective = (pattern && *pattern) ? pattern : last_search_pattern;
    char display[256];

    if (effective && *effective) {
        vi_display_search_pattern(effective, display, sizeof(display));
        snprintf(vis->status_msg, sizeof(vis->status_msg),
            "Pattern not found: %.236s", display);
    } else {
        snprintf(vis->status_msg, sizeof(vis->status_msg),
            "No previous search pattern");
    }
    vis->status_once = 1;
}

static void
vi_set_insert_anchor(buffer_t *b, vi_visual_t *vis)
{
    vis->insert_anchor_line = b->cur ? b->cur : b->head;
    vis->insert_anchor_col = vis->cursor_col;
}

static void
vi_restore_terminal(void)
{
    static const char restore_seq[] =
        "\x1b[?1l\x1b>\x1b[?25h\x1b[0m\x1b[2J\x1b[H";

    if (active_visual && active_visual->raw_active) {
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &active_visual->saved_tio);
        active_visual->raw_active = 0;
        write(STDOUT_FILENO, restore_seq, sizeof(restore_seq) - 1);
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
    static const char enter_seq[] = "\x1b[?1h\x1b=";
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
    write(STDOUT_FILENO, enter_seq, sizeof(enter_seq) - 1);
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
vi_indent_unit(void)
{
    return (option_tabstop > 0) ? option_tabstop : 8;
}

static int
vi_leading_indent_len(line_t *cur)
{
    int len = 0;

    if (!cur) {
        return 0;
    }
    while ((size_t)len < cur->len &&
        (cur->text[len] == ' ' || cur->text[len] == '\t')) {
        len++;
    }
    return len;
}

static void
vi_reindent_current_line(buffer_t *b, vi_visual_t *vis, int increase)
{
    line_t *cur = vi_ensure_current_line(b);
    int old_prefix_len;
    int old_cols;
    int unit;
    int target_cols;
    int new_tabs;
    int new_spaces;
    int new_prefix_len;
    int delta;
    char *text;

    if (!cur) {
        return;
    }
    old_prefix_len = vi_leading_indent_len(cur);
    old_cols = vi_display_col_for_index(cur, old_prefix_len);
    unit = vi_indent_unit();
    if (increase) {
        target_cols = old_cols + unit;
    } else {
        if (old_cols <= 0) {
            return;
        }
        target_cols = old_cols - unit;
        if (target_cols < 0) {
            target_cols = 0;
        }
    }
    new_tabs = target_cols / unit;
    new_spaces = target_cols % unit;
    new_prefix_len = new_tabs + new_spaces;
    text = malloc((size_t)new_prefix_len + (cur->len - (size_t)old_prefix_len) + 1);
    if (!text) {
        return;
    }
    memset(text, '\t', (size_t)new_tabs);
    memset(text + new_tabs, ' ', (size_t)new_spaces);
    memcpy(text + new_prefix_len, cur->text + old_prefix_len,
        cur->len - (size_t)old_prefix_len + 1);
    if (vi_replace_current_text(b, text) == 0) {
        delta = new_prefix_len - old_prefix_len;
        if (vis->cursor_col >= old_prefix_len) {
            vis->cursor_col += delta;
        }
        if (vis->cursor_col < 0) {
            vis->cursor_col = 0;
        }
        if (vis->insert_anchor_line == cur) {
            if (!(vis->insert_anchor_col == 0 && old_prefix_len == 0) &&
                vis->insert_anchor_col >= old_prefix_len) {
                vis->insert_anchor_col += delta;
            }
            if (vis->insert_anchor_col < 0) {
                vis->insert_anchor_col = 0;
            }
        }
        vi_clamp_cursor(b, vis);
    }
    free(text);
}

static int
vi_shift_range(buffer_t *b, vi_visual_t *vis, int start_line_no, int end_line_no, int increase)
{
    line_t *orig;
    int line_no;

    if (start_line_no < 1 || end_line_no < start_line_no || b->line_count < 1) {
        return -1;
    }
    orig = buf_get_line(b, start_line_no);
    if (!orig) {
        return -1;
    }
    save_undo(b);
    for (line_no = start_line_no; line_no <= end_line_no; line_no++) {
        b->cur = buf_get_line(b, line_no);
        if (!b->cur) {
            break;
        }
        vi_reindent_current_line(b, vis, increase);
    }
    b->cur = orig;
    vis->cursor_col = vi_first_nonblank_col(orig);
    return 0;
}

static int
vi_apply_linewise_operator(buffer_t *b, vi_visual_t *vis, int start_line, int end_line)
{
    if (start_line < 1 || end_line < start_line) {
        return -1;
    }
    if (vis->pending_op == 'y') {
        vi_linewise_yank(b, vis, start_line, end_line);
        return 0;
    }
    if (vis->pending_op == 'd') {
        vi_linewise_delete(b, vis, start_line, end_line);
        vi_set_last_change(vis, VI_REPEAT_DD, end_line - start_line + 1, 0);
        return 0;
    }
    if (vis->pending_op == 'c') {
        vi_linewise_change(b, vis, start_line, end_line);
        return 0;
    }
    if (vis->pending_op == '>') {
        return vi_shift_range(b, vis, start_line, end_line, 1);
    }
    if (vis->pending_op == '<') {
        return vi_shift_range(b, vis, start_line, end_line, 0);
    }
    return -1;
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
        used += printf("%7d ", line_no);
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
vi_draw_status_text(const char *text, int cols)
{
    int used = 0;

    if (!text || cols <= 0) {
        return;
    }
    while (*text && used < cols) {
        unsigned char c = (unsigned char)*text++;

        if (!isprint(c)) {
            c = '?';
        }
        putchar((int)c);
        used++;
    }
}

static void
vi_draw_default_status(buffer_t *b, int cols, int cur_line)
{
    char prefix[384];
    char suffix[96];
    int prefix_cols;

    snprintf(prefix, sizeof(prefix), "\"%s\"%s%s",
        b->filename ? b->filename : "[No Name]",
        b->modified ? " [Modified]" : "",
        option_readonly ? " [Readonly]" : "");
    snprintf(suffix, sizeof(suffix), "  line %d/%d",
        cur_line > 0 ? cur_line : 1, b->line_count);

    if ((int)(strlen(prefix) + strlen(suffix)) <= cols) {
        vi_draw_status_text(prefix, cols);
        vi_draw_status_text(suffix, cols - (int)strlen(prefix));
        return;
    }

    if ((int)strlen(suffix) >= cols) {
        vi_draw_status_text(suffix + (strlen(suffix) - (size_t)cols), cols);
        return;
    }

    prefix_cols = cols - (int)strlen(suffix);
    vi_draw_status_text(prefix, prefix_cols);
    vi_draw_status_text(suffix, (int)strlen(suffix));
}

static void
vi_render_body(buffer_t *b, vi_visual_t *vis)
{
    int row;

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
}

static void
vi_render_status(buffer_t *b, vi_visual_t *vis, int cur_line, char prompt_prefix,
    const char *prompt)
{
    printf("\x1b[%d;1H\x1b[K\x1b[7m", vis->rows);
    if (prompt) {
        char status_buf[512];

        snprintf(status_buf, sizeof(status_buf), "%c%s", prompt_prefix, prompt);
        vi_draw_status_text(status_buf, vis->cols);
    } else if (vis->replace_mode && option_showmode) {
        vi_draw_status_text("-- REPLACE --", vis->cols);
    } else if (vis->insert_mode && option_showmode) {
        vi_draw_status_text("-- INSERT --", vis->cols);
    } else if (vis->status_msg[0] != '\0') {
        vi_draw_status_text(vis->status_msg, vis->cols);
    } else {
        vi_draw_default_status(b, vis->cols, cur_line);
    }
    printf("\x1b[K\x1b[m");
    vis->rendered_cur_line = cur_line;
}

static void
vi_position_cursor(buffer_t *b, vi_visual_t *vis, int cur_line)
{
    int cursor_disp = vi_display_col_for_index(b->cur ? b->cur : b->head, vis->cursor_col);
    int cursor_row;
    int cursor_col;

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
}

static void
vi_render(buffer_t *b, vi_visual_t *vis, char prompt_prefix, const char *prompt)
{
    int old_top = vis->top_line;
    int old_left = vis->left_col;
    int old_rows = vis->rows;
    int old_cols = vis->cols;
    int cur_line;

    vi_update_size(vis);
    vi_clamp_cursor(b, vis);
    vi_scroll_into_view(b, vis);
    cur_line = buf_current_line(b);

    if (vis->rows != old_rows || vis->cols != old_cols || vis->top_line != old_top
        || vis->left_col != old_left) {
        vis->screen_dirty = 1;
    }
    if (prompt || vis->insert_mode || vis->replace_mode || vis->status_msg[0] != '\0'
        || cur_line != vis->rendered_cur_line) {
        vis->status_dirty = 1;
    }

    if (vis->screen_dirty) {
        if (vis->clear_screen) {
            printf("\x1b[2J");
            vis->clear_screen = 0;
        }
        vi_render_body(b, vis);
        vis->screen_dirty = 0;
        vis->status_dirty = 1;
    }
    if (vis->status_dirty || prompt) {
        vi_render_status(b, vis, cur_line, prompt_prefix, prompt);
        vis->status_dirty = 0;
    }
    vi_position_cursor(b, vis, cur_line);
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

int
exvi_decode_terminal_key_sequence(char prefix, const char *seq)
{
    int num = 0;
    int mod = 0;
    char final = '\0';

    (void)prefix;

    if (seq[0] == '\0') {
        return VI_KEY_UNKNOWN;
    }

    if (seq[1] == '\0' && strchr("ABCDHF", seq[0])) {
        switch (seq[0]) {
        case 'A': return VI_KEY_UP;
        case 'B': return VI_KEY_DOWN;
        case 'C': return VI_KEY_RIGHT;
        case 'D': return VI_KEY_LEFT;
        case 'H': return VI_KEY_HOME;
        case 'F': return VI_KEY_END;
        default: return VI_KEY_UNKNOWN;
        }
    }

    if (sscanf(seq, "%d;%d%c", &num, &mod, &final) == 3) {
        if (mod == 5) {
            switch (final) {
            case 'C':
                return VI_KEY_CTRL_RIGHT;
            case 'D':
                return VI_KEY_CTRL_LEFT;
            case '~':
                if (num == 3) {
                    return VI_KEY_CTRL_DELETE;
                }
                break;
            case 'u':
                if (num == 8 || num == 127) {
                    return VI_KEY_CTRL_BACKSPACE;
                }
                break;
            default:
                break;
            }
        }
        return VI_KEY_UNKNOWN;
    }

    if (sscanf(seq, "%d%c", &num, &final) == 2) {
        switch (final) {
        case '~':
            switch (num) {
            case 1:
            case 7:
                return VI_KEY_HOME;
            case 3:
                return VI_KEY_DELETE;
            case 4:
            case 8:
                return VI_KEY_END;
            case 5:
                return VI_KEY_PGUP;
            case 6:
                return VI_KEY_PGDN;
            default:
                return VI_KEY_UNKNOWN;
            }
        case 'C':
            return (num == 5) ? VI_KEY_CTRL_RIGHT : VI_KEY_UNKNOWN;
        case 'D':
            return (num == 5) ? VI_KEY_CTRL_LEFT : VI_KEY_UNKNOWN;
        default:
            return VI_KEY_UNKNOWN;
        }
    }

    return VI_KEY_UNKNOWN;
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
        char prefix;
        char seq[32];
        size_t len = 0;

        if (!vi_read_timed_byte(&prefix, 50)) {
            return '\x1b';
        }
        if (prefix == '[' || prefix == 'O') {
            for (;;) {
                if (!vi_read_timed_byte(&seq[len], 50)) {
                    return VI_KEY_UNKNOWN;
                }
                if ((unsigned char)seq[len] >= 0x40 &&
                    (unsigned char)seq[len] <= 0x7e) {
                    len++;
                    break;
                }
                len++;
                if (len + 1 >= sizeof(seq)) {
                    break;
                }
            }
            seq[len] = '\0';
            return exvi_decode_terminal_key_sequence(prefix, seq);
        }
        return VI_KEY_UNKNOWN;
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

static int
vi_multiply_counts(int left, int right)
{
    if (left <= 0) {
        left = 1;
    }
    if (right <= 0) {
        right = 1;
    }
    if (left > 99999999 / right) {
        return 99999999;
    }
    return left * right;
}

static void
vi_set_last_change(vi_visual_t *vis, vi_repeat_kind_t kind, int count, int ch)
{
    vis->last_change = kind;
    vis->last_change_count = count;
    vis->last_change_char = ch;
    vis->last_change_aux = 0;
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
    int was_modified = b->modified;

    if (b->cur) {
        return b->cur;
    }
    if (b->head) {
        b->cur = b->head;
        return b->cur;
    }
    b->empty_origin = 1;
    b->started_empty = 1;
    b->cur = buf_insert_after(b, NULL, "");
    b->empty_origin = 1;
    b->trailing_newline = 0;
    b->modified = was_modified;
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
    b->started_empty = 1;
    b->cur = buf_insert_after(b, NULL, "");
    b->empty_origin = 1;
    b->trailing_newline = 0;
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
    int sign = (direction < 0) ? -1 : 1;
    int count = (direction < 0) ? -direction : direction;
    int page = option_scroll;

    if (count > 1) {
        page = count;
    } else if (!option_scroll_explicit) {
        page = (vis->rows - 2) / 2;
    }
    if (page <= 0) {
        page = (vis->rows - 2) / 2;
        if (page < 1) {
            page = 1;
        }
    }
    vi_move_vertical(b, sign * page);
}

static void
vi_line_scroll(buffer_t *b, vi_visual_t *vis, int direction)
{
    int cur_line_no;
    int old_top;
    int new_top;

    if (!b->cur || b->line_count < 1) {
        return;
    }

    cur_line_no = buf_current_line(b);
    old_top = vi_clamp_top_line(b, vis, vis->top_line);
    new_top = vi_clamp_top_line(b, vis, old_top + direction);
    vis->top_line = new_top;
    if (direction > 0 && cur_line_no < new_top) {
        b->cur = buf_get_line(b, new_top);
        if (b->cur && (size_t)vis->cursor_col > b->cur->len) {
            vis->cursor_col = (int)b->cur->len;
        }
    }
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

static size_t
vi_indent_len(line_t *cur)
{
    size_t i;

    if (!cur) {
        return 0;
    }
    for (i = 0; i < cur->len; i++) {
        if (cur->text[i] != ' ' && cur->text[i] != '\t') {
            break;
        }
    }
    return i;
}

static char *
vi_autoindent_prefix(line_t *cur, size_t *indent_len_out)
{
    size_t indent_len = (option_autoindent && cur) ? vi_indent_len(cur) : 0;
    char *prefix = malloc(indent_len + 1);

    if (!prefix) {
        return NULL;
    }
    if (indent_len > 0) {
        memcpy(prefix, cur->text, indent_len);
    }
    prefix[indent_len] = '\0';
    if (indent_len_out) {
        *indent_len_out = indent_len;
    }
    return prefix;
}

static int
vi_screen_target_line(buffer_t *b, vi_visual_t *vis, int mode, int count)
{
    int visible_rows = vis->rows - 1;
    int remaining_lines;
    int screen_row;
    int target_line;

    if (visible_rows < 1) {
        visible_rows = 1;
    }
    remaining_lines = b->line_count - vis->top_line + 1;
    if (remaining_lines < 1) {
        remaining_lines = 1;
    }
    if (visible_rows > remaining_lines) {
        visible_rows = remaining_lines;
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
vi_reposition_target(buffer_t *b, vi_visual_t *vis, int target_line, int mode, int keep_col)
{
    line_t *target;
    int visible_rows = vis->rows - 1;
    int top_line;

    if (b->line_count < 1) {
        return;
    }
    if (visible_rows < 1) {
        visible_rows = 1;
    }
    target_line = vi_clamp_line_target(b, target_line);
    target = buf_get_line(b, target_line);
    if (!target) {
        return;
    }
    b->cur = target;
    switch (mode) {
    case 0:
        top_line = target_line - (visible_rows / 2);
        break;
    case 1:
        top_line = target_line;
        break;
    default:
        top_line = target_line - visible_rows + 1;
        break;
    }
    vis->top_line = vi_clamp_top_line(b, vis, top_line);
    if (keep_col) {
        if ((size_t)vis->cursor_col > target->len) {
            vis->cursor_col = (int)target->len;
        }
    } else {
        vis->cursor_col = vi_first_nonblank_col(target);
    }
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
    line_t *line;

    if (line_no < 1) {
        return;
    }
    while (count-- > 0) {
        line = buf_get_line(b, line_no);
        if (!line) {
            return;
        }
        if (vi_line_is_blank(line)) {
            while (line_no <= b->line_count &&
                vi_line_is_blank(buf_get_line(b, line_no))) {
                line_no++;
            }
            while (line_no <= b->line_count &&
                !vi_line_is_blank(buf_get_line(b, line_no))) {
                line_no++;
            }
        } else {
            line_no++;
            while (line_no <= b->line_count &&
                !vi_line_is_blank(buf_get_line(b, line_no))) {
                line_no++;
            }
        }
        if (line_no > b->line_count) {
            line_no = b->line_count;
            break;
        }
    }
    b->cur = buf_get_line(b, line_no);
    if (b->cur && vi_line_is_blank(b->cur)) {
        vis->cursor_col = 0;
    } else {
        vis->cursor_col = vi_first_nonblank_col(b->cur);
    }
}

static void
vi_move_paragraph_backward(buffer_t *b, vi_visual_t *vis, int count)
{
    int line_no = buf_current_line(b);
    line_t *line;

    if (line_no < 1) {
        return;
    }
    while (count-- > 0) {
        line = buf_get_line(b, line_no);
        if (!line) {
            return;
        }
        if (vi_line_is_blank(line)) {
            while (line_no >= 1 && vi_line_is_blank(buf_get_line(b, line_no))) {
                line_no--;
            }
            while (line_no >= 1 && !vi_line_is_blank(buf_get_line(b, line_no))) {
                line_no--;
            }
            if (line_no < 1) {
                line_no = 1;
            }
        } else {
            int start = line_no;

            while (start > 1 && !vi_line_is_blank(buf_get_line(b, start - 1))) {
                start--;
            }
            if (start == line_no) {
                line_no = start - 1;
                if (line_no < 1) {
                    line_no = 1;
                }
            } else {
                line_no = start;
            }
        }
        if (line_no < 1) {
            line_no = 1;
            break;
        }
    }
    b->cur = buf_get_line(b, line_no);
    if (b->cur && vi_line_is_blank(b->cur)) {
        vis->cursor_col = 0;
    } else {
        vis->cursor_col = vi_first_nonblank_col(b->cur);
    }
}

static int
vi_is_section_line(line_t *line, int want_end)
{
    unsigned char ch;

    if (!line || line->len == 0) {
        return 0;
    }
    ch = (unsigned char)line->text[0];
    if (ch == '\f') {
        return 1;
    }
    if (want_end) {
        return ch == '}';
    }
    return ch == '{';
}

static void
vi_move_section_boundary(buffer_t *b, vi_visual_t *vis, int forward, int want_end,
    int count)
{
    int line_no = buf_current_line(b);

    if (line_no < 1 || count < 1) {
        return;
    }
    while (count-- > 0) {
        int probe = line_no + (forward ? 1 : -1);
        int found = 0;

        while (probe >= 1 && probe <= b->line_count) {
            if (vi_is_section_line(buf_get_line(b, probe), want_end)) {
                line_no = probe;
                found = 1;
                break;
            }
            probe += forward ? 1 : -1;
        }
        if (!found && forward && !want_end) {
            probe = line_no + 1;
            while (probe <= b->line_count) {
                if (vi_is_section_line(buf_get_line(b, probe), 1)) {
                    line_no = probe;
                    found = 1;
                    break;
                }
                probe++;
            }
        }
        if (!found) {
            break;
        }
    }
    b->cur = buf_get_line(b, line_no);
    vis->cursor_col = 0;
}

static void
vi_move_section_start_forward(buffer_t *b, vi_visual_t *vis, int count)
{
    vi_move_section_boundary(b, vis, 1, 0, count);
}

static void
vi_move_section_start_backward(buffer_t *b, vi_visual_t *vis, int count)
{
    vi_move_section_boundary(b, vis, 0, 0, count);
}

static void
vi_move_section_end_forward(buffer_t *b, vi_visual_t *vis, int count)
{
    vi_move_section_boundary(b, vis, 1, 1, count);
}

static void
vi_move_section_end_backward(buffer_t *b, vi_visual_t *vis, int count)
{
    vi_move_section_boundary(b, vis, 0, 1, count);
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
        line_t *cur = buf_get_line(b, cur_line);

        if (cur && vi_is_sentence_end_at(cur, cur_col) &&
            vi_find_sentence_start_after(b, cur_line, cur_col, &cur_line, &cur_col) == 0) {
            continue;
        }

        for (ln = cur_line; ln <= b->line_count && !found; ln++) {
            line_t *line = buf_get_line(b, ln);
            int start = (ln == cur_line) ? cur_col + 1 : 0;
            int col;

            if (!line) {
                continue;
            }
            if (vi_line_is_blank(line)) {
                if (ln == cur_line && cur_col == 0) {
                    int next = ln;

                    while (next <= b->line_count &&
                        vi_line_is_blank(buf_get_line(b, next))) {
                        next++;
                    }
                    if (next <= b->line_count) {
                        cur_line = next;
                        cur_col = vi_first_nonblank_col(buf_get_line(b, next));
                        found = 1;
                    }
                } else {
                    cur_line = ln;
                    cur_col = 0;
                    found = 1;
                }
                break;
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
            line_t *line = buf_get_line(b, cur_line);

            if (!line) {
                return;
            }
            if (vi_line_is_blank(line)) {
                while (cur_line >= 1 && vi_line_is_blank(buf_get_line(b, cur_line))) {
                    cur_line--;
                }
                if (cur_line < 1) {
                    if (vi_find_first_nonblank(b, &cur_line, &cur_col) != 0) {
                        return;
                    }
                } else {
                    while (cur_line > 1 &&
                        !vi_line_is_blank(buf_get_line(b, cur_line - 1))) {
                        cur_line--;
                    }
                    cur_col = vi_first_nonblank_col(buf_get_line(b, cur_line));
                }
            } else {
                int start = cur_line;
                int start_col;

                while (start > 1 && !vi_line_is_blank(buf_get_line(b, start - 1))) {
                    start--;
                }
                start_col = vi_first_nonblank_col(buf_get_line(b, start));
                if (start < cur_line || cur_col > start_col) {
                    cur_line = start;
                    cur_col = start_col;
                } else if (start > 1) {
                    cur_line = start - 1;
                    cur_col = 0;
                } else if (vi_find_first_nonblank(b, &cur_line, &cur_col) != 0) {
                    return;
                }
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
        if (vi_line_is_blank(cur)) {
            b->cur = cur;
            vis->cursor_col = 0;
            return;
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
    int current_blank;

    if (!cur) {
        return;
    }
    current_blank = vi_line_is_blank(cur);
    i = vis->cursor_col;
    if (i > 0) {
        i--;
    } else if ((size_t)vis->cursor_col < cur->len &&
        vi_is_word_char((unsigned char)cur->text[vis->cursor_col])) {
        i = -1;
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
        if (vi_line_is_blank(cur)) {
            if (!current_blank) {
                b->cur = cur;
                vis->cursor_col = 0;
                return;
            }
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
    if (i < cur->len && vi_is_word_char((unsigned char)cur->text[i])) {
        while (i + 1 < cur->len && vi_is_word_char((unsigned char)cur->text[i + 1])) {
            i++;
        }
        if ((int)i != vis->cursor_col) {
            vis->cursor_col = (int)i;
            return;
        }
        i++;
    } else {
        while (i < cur->len && !vi_is_word_char((unsigned char)cur->text[i])) {
            i++;
        }
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
vi_move_word_end_count(buffer_t *b, vi_visual_t *vis, int count)
{
    while (count-- > 0) {
        vi_move_word_end(b, vis);
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
        if (vi_line_is_blank(cur)) {
            b->cur = cur;
            vis->cursor_col = 0;
            return;
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
    int current_blank;

    if (!cur) {
        return;
    }
    current_blank = vi_line_is_blank(cur);
    i = vis->cursor_col;
    if (i > 0) {
        i--;
    } else if ((size_t)vis->cursor_col < cur->len &&
        vi_is_bigword_char((unsigned char)cur->text[vis->cursor_col])) {
        i = -1;
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
        if (vi_line_is_blank(cur)) {
            if (!current_blank) {
                b->cur = cur;
                vis->cursor_col = 0;
                return;
            }
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
    if (i < cur->len && vi_is_bigword_char((unsigned char)cur->text[i])) {
        while (i + 1 < cur->len && vi_is_bigword_char((unsigned char)cur->text[i + 1])) {
            i++;
        }
        if ((int)i != vis->cursor_col) {
            vis->cursor_col = (int)i;
            return;
        }
        i++;
    } else {
        while (i < cur->len && !vi_is_bigword_char((unsigned char)cur->text[i])) {
            i++;
        }
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
vi_move_bigword_end_count(buffer_t *b, vi_visual_t *vis, int count)
{
    while (count-- > 0) {
        vi_move_bigword_end(b, vis);
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

    vis->last_find_char = ch;
    vis->last_find_forward = forward;
    vis->last_find_till = till;
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
vi_find_match_for_bracket(line_t *line, int match_col, line_t **line_out, int *col_out)
{
    int open_ch;
    int match_ch;
    int forward;
    int depth = 1;
    
    if (!line || match_col < 0 || (size_t)match_col >= line->len) {
        return -1;
    }
    open_ch = (unsigned char)line->text[match_col];
    if (vi_match_bracket(open_ch, &forward, &match_ch) != 0) {
        return -1;
    }

    if (forward) {
        size_t i;

        for (i = (size_t)match_col + 1; i < line->len; i++) {
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

        for (i = match_col - 1; i >= 0; i--) {
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

static int
vi_find_scanned_bracket_target(line_t *line, int cursor_col, int motion_mode,
    line_t **line_out, int *col_out)
{
    line_t *fallback_line = NULL;
    int fallback_col = 0;
    size_t i;

    if (!line || cursor_col < 0 || (size_t)cursor_col >= line->len) {
        return -1;
    }

    for (i = (size_t)cursor_col + 1; i < line->len; i++) {
        line_t *candidate_line;
        int candidate_col;

        if (vi_find_match_for_bracket(line, (int)i, &candidate_line, &candidate_col) != 0) {
            continue;
        }
        if (!fallback_line) {
            fallback_line = candidate_line;
            fallback_col = candidate_col;
        }
        if (candidate_line != line) {
            *line_out = motion_mode ? line : candidate_line;
            *col_out = motion_mode ? (int)i : candidate_col;
            return 0;
        }
    }

    if (fallback_line) {
        *line_out = fallback_line;
        *col_out = fallback_col;
        return 0;
    }
    return -1;
}

static int
vi_match_cursor_col(line_t *line, int cursor_col)
{
    if (!line || line->len == 0 || cursor_col < 0) {
        return -1;
    }
    if ((size_t)cursor_col >= line->len) {
        return (int)line->len - 1;
    }
    return cursor_col;
}

static int
vi_find_scanned_cross_bracket_span(line_t *line, int cursor_col, int *start_col_out,
    line_t **line_out, int *col_out)
{
    int forward;
    int match_ch;
    size_t i;

    cursor_col = vi_match_cursor_col(line, cursor_col);
    if (cursor_col < 0) {
        return -1;
    }
    if (vi_match_bracket((unsigned char)line->text[cursor_col], &forward, &match_ch) == 0) {
        return -1;
    }
    for (i = (size_t)cursor_col + 1; i < line->len; i++) {
        if (vi_find_match_for_bracket(line, (int)i, line_out, col_out) == 0 &&
            *line_out != line) {
            *start_col_out = (int)i;
            return 0;
        }
    }
    return -1;
}

static int
vi_match_motion_target(buffer_t *b, vi_visual_t *vis, line_t **line_out, int *col_out)
{
    line_t *line = b->cur;
    int cursor_col = vi_match_cursor_col(line, vis->cursor_col);

    if (cursor_col < 0) {
        return -1;
    }
    if (vi_find_match_for_bracket(line, cursor_col, line_out, col_out) == 0) {
        return 0;
    }
    return vi_find_scanned_bracket_target(line, cursor_col, 1, line_out, col_out);
}

static int
vi_match_operator_target(buffer_t *b, vi_visual_t *vis, line_t **line_out, int *col_out)
{
    line_t *line = b->cur;
    int cursor_col = vi_match_cursor_col(line, vis->cursor_col);

    if (cursor_col < 0) {
        return -1;
    }
    if (vi_find_match_for_bracket(line, cursor_col, line_out, col_out) == 0) {
        return 0;
    }
    return vi_find_scanned_bracket_target(line, cursor_col, 0, line_out, col_out);
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
        *end_out = cursor_col;
    }
    if (*end_out <= *start_out) {
        return -1;
    }
    return 0;
}

static int
vi_find_change_motion_span(line_t *cur, int cursor_col, int ch, int forward, int till,
    int count, int *start_out, int *end_out)
{
    int col;

    if (vi_find_motion_span(cur, cursor_col, ch, forward, till, count,
        start_out, end_out) == 0) {
        return 0;
    }
    if (!cur || forward || !till || count != 1) {
        return -1;
    }
    if (vi_find_char_in_line(cur, cursor_col, ch, 0, 1, &col) != 0) {
        return -1;
    }
    if (col + 1 != cursor_col) {
        return -1;
    }
    *start_out = cursor_col;
    *end_out = cursor_col + 1;
    return 0;
}

static int
vi_change_find_is_zero_width_insert(line_t *cur, int cursor_col, int ch, int forward, int till,
    int count)
{
    int col;

    if (!cur || forward || !till || count != 1) {
        return 0;
    }
    if (vi_find_char_in_line(cur, cursor_col, ch, 0, 1, &col) != 0) {
        return 0;
    }
    return (col + 1 == cursor_col);
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
vi_find_change_word_target(line_t *cur, int start, int count, int bigword, int *end_out)
{
    int i;

    if (!cur || start < 0 || (size_t)start > cur->len) {
        return -1;
    }
    if (count < 1) {
        count = 1;
    }
    if ((size_t)start >= cur->len) {
        *end_out = (int)cur->len;
        return 0;
    }

    if (isspace((unsigned char)cur->text[start])) {
        i = start;
        while ((size_t)i < cur->len && isspace((unsigned char)cur->text[i])) {
            i++;
        }
        if (count == 1) {
            *end_out = i;
            return 0;
        }
        if (bigword) {
            return vi_find_bigword_end_exclusive_count(cur, i, count - 1, end_out);
        }
        return vi_find_word_end_exclusive_count(cur, i, count - 1, end_out);
    }

    if (bigword) {
        return vi_find_bigword_end_exclusive_count(cur, start, count, end_out);
    }
    return vi_find_word_end_exclusive_count(cur, start, count, end_out);
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
    int replacing_empty_placeholder;

    if (!cur) {
        return -1;
    }
    replacing_empty_placeholder = b->empty_origin
        && b->line_count == 1
        && b->head == cur
        && b->tail == cur
        && cur->len == 0
        && !b->trailing_newline;
    copy = strdup(text);
    if (!copy) {
        return -1;
    }
    free(cur->text);
    cur->text = copy;
    cur->len = strlen(copy);
    if (replacing_empty_placeholder && cur->len > 0) {
        b->trailing_newline = 1;
    }
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
            vis->insert_entry_key = 0;
            vi_record_last_insert_site(b, vis);
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
            vis->insert_entry_key = 0;
            vi_record_last_insert_site(b, vis);
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
                    if (src->len > 0) {
                        vis->cursor_col = pos + (int)src->len - 1;
                    } else {
                        vis->cursor_col = pos;
                    }
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
    line_t *orig;

    vi_take_register_arg(vis, regarg);
    handle_yank_command(b, regarg, 1, start, end);
    orig = buf_get_line(b, start);
    if (orig) {
        b->cur = orig;
    }
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
    char *indent;
    size_t indent_len = 0;
    int retarget_marks[26] = {0};
    line_t *scan;

    indent = vi_autoindent_prefix(buf_get_line(b, start), &indent_len);
    if (!indent) {
        return;
    }
    scan = buf_get_line(b, start);
    for (int line_no = start; line_no <= end && scan; line_no++, scan = scan->next) {
        for (int i = 0; i < 26; i++) {
            if (b->marks[i] == scan) {
                retarget_marks[i] = 1;
            }
        }
    }
    vi_take_register_arg(vis, regarg);
    handle_yank_command(b, regarg, 1, start, end);
    handle_delete_command(b, 1, start, end);
    b->cur = buf_insert_after(b, before, indent);
    free(indent);
    if (!b->cur) {
        b->cur = b->head;
    }
    for (int i = 0; i < 26; i++) {
        if (retarget_marks[i]) {
            b->marks[i] = b->cur;
            b->mark_cols[i] = 0;
        }
    }
    vis->cursor_col = (int)indent_len;
    vis->insert_mode = 1;
    vis->replace_mode = 0;
    vis->insert_entry_key = 0;
    vi_record_last_insert_site(b, vis);
    vi_set_insert_anchor(b, vis);
}

static void
vi_substitute_line(buffer_t *b, vi_visual_t *vis, int capture_register)
{
    int line_no = buf_current_line(b);
    line_t *cur = b->cur;
    char *indent = NULL;
    size_t indent_len = 0;

    if (capture_register && line_no >= 1) {
        char regarg[2];

        vi_take_register_arg(vis, regarg);
        handle_yank_command(b, regarg, 1, line_no, line_no);
    }
    if (!b->cur) {
        save_undo(b);
        b->cur = buf_insert_after(b, b->tail, "");
    } else {
        indent = vi_autoindent_prefix(cur, &indent_len);
        if (!indent) {
            return;
        }
        save_undo(b);
        if (vi_replace_current_text(b, indent) != 0) {
            free(indent);
            return;
        }
    }
    vis->cursor_col = (int)indent_len;
    vis->insert_mode = 1;
    vis->replace_mode = 0;
    vis->insert_entry_key = 0;
    vi_record_last_insert_site(b, vis);
    vi_set_insert_anchor(b, vis);
    free(indent);
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
vi_apply_backward_start_motion(buffer_t *b, vi_visual_t *vis, line_t *target_line,
    int target_col)
{
    line_t *end_line;
    int end_col;
    int rc;

    if (!b->cur || !target_line) {
        return -1;
    }
    if (target_line == b->cur || vis->cursor_col != 0) {
        return vi_apply_charwise_motion(b, vis, target_line, target_col, 0);
    }

    if (target_col == 0) {
        if (vis->pending_op == 'c') {
            int start_line_no = vi_line_number_for_mark(b, target_line);
            int end_line_no = vi_line_number_for_mark(b, b->cur) - 1;

            if (start_line_no < 1 || end_line_no < start_line_no) {
                return -1;
            }
            vi_linewise_change(b, vis, start_line_no, end_line_no);
            return 0;
        }
        end_line = b->cur;
        end_col = 0;
    } else {
        end_line = b->cur->prev;
        if (!end_line) {
            return -1;
        }
        end_col = (int)end_line->len;
    }

    if (vis->pending_op == 'y') {
        rc = vi_yank_range(b, vis, target_line, target_col, end_line, end_col);
        if (rc == 0) {
            b->cur = target_line;
            vis->cursor_col = target_col;
        }
        return rc;
    }

    vi_delete_range(b, vis, target_line, target_col, end_line, end_col,
        vis->pending_op == 'c');
    return 0;
}

static int
vi_apply_search_charwise_motion(buffer_t *b, vi_visual_t *vis, line_t *target_line,
    int target_col)
{
    if (!b->cur || !target_line) {
        return -1;
    }
    if (vis->cursor_col == 0 &&
        vi_compare_positions(b, b->cur, vis->cursor_col, target_line, target_col) > 0) {
        return vi_apply_backward_start_motion(b, vis, target_line, target_col);
    }
    return vi_apply_charwise_motion(b, vis, target_line, target_col, 0);
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
static void
vi_pattern_word_boundaries(const char *pattern, int *word_start, int *word_end)
{
    size_t len;

    *word_start = 0;
    *word_end = 0;
    if (!pattern) {
        return;
    }
    len = strlen(pattern);
    if (len >= 2 && pattern[0] == '\\' && pattern[1] == '<') {
        *word_start = 1;
    }
    if (len >= 2 && pattern[len - 2] == '\\' && pattern[len - 1] == '>') {
        *word_end = 1;
    }
}

static int
vi_match_respects_word_boundaries(line_t *line, int start_col, int end_col,
    int word_start, int word_end)
{
    if (!line) {
        return 0;
    }
    if (start_col < 0 || end_col < start_col || (size_t)end_col > line->len) {
        return 0;
    }
    if (word_start && start_col > 0 &&
        vi_is_word_char((unsigned char)line->text[start_col - 1]) &&
        (size_t)start_col < line->len &&
        vi_is_word_char((unsigned char)line->text[start_col])) {
        return 0;
    }
    if (word_end && end_col > 0 && (size_t)end_col < line->len &&
        vi_is_word_char((unsigned char)line->text[end_col - 1]) &&
        vi_is_word_char((unsigned char)line->text[end_col])) {
        return 0;
    }
    return 1;
}

static int
vi_search_forward_in_line(line_t *line, regex_t *re, int start_col, int max_col,
    int word_start, int word_end, int *match_col)
{
    size_t offset;
    regmatch_t match;

    if (!line) {
        return -1;
    }
    if (start_col < 0) {
        start_col = 0;
    }
    if (max_col < start_col) {
        return -1;
    }
    if ((size_t)start_col > line->len) {
        return -1;
    }
    if (max_col > (int)line->len) {
        max_col = (int)line->len;
    }
    offset = (size_t)start_col;
    while (offset <= line->len) {
        int start;
        int end;

        if (regexec(re, line->text + offset, 1, &match, 0) != 0) {
            return -1;
        }
        if (match.rm_so < 0) {
            return -1;
        }
        start = (int)(offset + (size_t)match.rm_so);
        end = (int)(offset + (size_t)match.rm_eo);
        if (start > max_col) {
            return -1;
        }
        if (vi_match_respects_word_boundaries(line, start, end, word_start, word_end)) {
            *match_col = start;
            return 0;
        }
        if (match.rm_eo > match.rm_so) {
            offset += (size_t)match.rm_eo;
        } else {
            offset = (size_t)start + 1;
        }
    }

    return -1;
}

static int
vi_search_backward_in_line(line_t *line, regex_t *re, int min_col, int max_col,
    int word_start, int word_end, int *match_col)
{
    size_t offset;
    regmatch_t match;
    int found = -1;

    if (!line) {
        return -1;
    }
    if (min_col < 0) {
        min_col = 0;
    }
    if (max_col < min_col) {
        return -1;
    }
    if (max_col > (int)line->len) {
        max_col = (int)line->len;
    }
    if ((size_t)min_col > line->len) {
        return -1;
    }
    offset = (size_t)min_col;
    while (offset <= line->len) {
        size_t pos;
        size_t end;

        if (regexec(re, line->text + offset, 1, &match, 0) != 0) {
            break;
        }
        if (match.rm_so < 0) {
            break;
        }
        pos = offset + (size_t)match.rm_so;
        if ((int)pos > max_col) {
            break;
        }
        end = offset + (size_t)match.rm_eo;
        if (vi_match_respects_word_boundaries(line, (int)pos, (int)end,
            word_start, word_end)) {
            found = (int)pos;
        }
        if (match.rm_eo > match.rm_so) {
            offset += (size_t)match.rm_eo;
        } else {
            offset = pos + 1;
        }
    }

    if (found < 0) {
        return -1;
    }
    *match_col = found;
    return 0;
}

static int
vi_search_once(buffer_t *b, regex_t *re, int forward, line_t *start_line, int start_col,
    int word_start, int word_end, line_t **line_out, int *col_out)
{
    line_t *line;
    int col;

    if (!b->head) {
        return -1;
    }

    if (forward) {
        if (!start_line) {
            start_line = b->head;
            start_col = -1;
        }
        if (vi_search_forward_in_line(start_line, re, start_col + 1,
            (int)start_line->len, word_start, word_end, &col) == 0) {
            *line_out = start_line;
            *col_out = col;
            return 0;
        }
        for (line = start_line->next; line; line = line->next) {
            if (vi_search_forward_in_line(line, re, 0, (int)line->len,
                word_start, word_end, &col) == 0) {
                *line_out = line;
                *col_out = col;
                return 0;
            }
        }
        if (!option_wrapscan) {
            return -1;
        }
        for (line = b->head; line && line != start_line; line = line->next) {
            if (vi_search_forward_in_line(line, re, 0, (int)line->len,
                word_start, word_end, &col) == 0) {
                *line_out = line;
                *col_out = col;
                return 0;
            }
        }
        if (vi_search_forward_in_line(start_line, re, 0, start_col,
            word_start, word_end, &col) == 0) {
            *line_out = start_line;
            *col_out = col;
            return 0;
        }
        return -1;
    }

    if (!start_line) {
        start_line = b->tail;
        start_col = start_line ? (int)start_line->len : 0;
    }
    if (vi_search_backward_in_line(start_line, re, 0, start_col - 1,
        word_start, word_end, &col) == 0) {
        *line_out = start_line;
        *col_out = col;
        return 0;
    }
    for (line = start_line->prev; line; line = line->prev) {
        if (vi_search_backward_in_line(line, re, 0, (int)line->len,
            word_start, word_end, &col) == 0) {
            *line_out = line;
            *col_out = col;
            return 0;
        }
    }
    if (!option_wrapscan) {
        return -1;
    }
    for (line = b->tail; line && line != start_line; line = line->prev) {
        if (vi_search_backward_in_line(line, re, 0, (int)line->len,
            word_start, word_end, &col) == 0) {
            *line_out = line;
            *col_out = col;
            return 0;
        }
    }
    if (vi_search_backward_in_line(start_line, re, start_col,
        (int)start_line->len, word_start, word_end, &col) == 0) {
        *line_out = start_line;
        *col_out = col;
        return 0;
    }
    return -1;
}

static int
vi_search_target(buffer_t *b, vi_visual_t *vis, const char *pattern, int forward,
    int count, line_t **line_out, int *col_out)
{
    char *search = NULL;
    regex_t re;
    line_t *line = b->cur ? b->cur : (forward ? b->head : b->tail);
    int col = vis->cursor_col;
    int word_start = 0;
    int word_end = 0;

    if (count < 1) {
        count = 1;
    }
    if (pattern && pattern[0] != '\0') {
        search = strdup(pattern);
        if (search) {
            replace_saved_string(&last_search_pattern, search);
        }
    } else if (last_search_pattern) {
        search = strdup(last_search_pattern);
    }
    if (!search) {
        return -1;
    }
    vi_pattern_word_boundaries(search, &word_start, &word_end);
    if (regcomp(&re, search, exvi_regex_flags()) != 0) {
        free(search);
        return -1;
    }

    while (count-- > 0) {
        if (vi_search_once(b, &re, forward, line, col, word_start, word_end,
            &line, &col) != 0) {
            vi_set_search_failure_status(vis, search);
            regfree(&re);
            free(search);
            return -1;
        }
    }

    regfree(&re);
    free(search);
    *line_out = line;
    *col_out = col;
    vis->last_search_forward = forward;
    return 0;
}

static int
vi_search_motion_target(buffer_t *b, vi_visual_t *vis, const char *pattern, int forward,
    int count, line_t **line_out, int *col_out)
{
    return vi_search_target(b, vis, pattern, forward, count, line_out, col_out);
}

static int
vi_apply_charwise_linewise_motion(buffer_t *b, vi_visual_t *vis, int current_line_no,
    line_t *target_line, int target_col)
{
    int target_line_no;
    int start;
    int end;

    if (!target_line) {
        return -1;
    }
    target_line_no = vi_line_number_for_mark(b, target_line);
    if (target_line_no < 1) {
        return -1;
    }
    if (target_line_no == current_line_no) {
        return vi_apply_linewise_operator(b, vis, current_line_no, current_line_no);
    }
    if (target_col == 0) {
        if (target_line_no > current_line_no) {
            start = current_line_no;
            end = target_line_no - 1;
        } else {
            start = target_line_no;
            end = current_line_no - 1;
        }
    } else if (target_line_no > current_line_no) {
        start = current_line_no;
        end = target_line_no;
    } else {
        start = target_line_no;
        end = current_line_no;
    }
    if (start > end) {
        return -1;
    }
    if (vi_apply_linewise_operator(b, vis, start, end) != 0) {
        return -1;
    }
    return 0;
}

static int
vi_apply_search_linewise_motion(buffer_t *b, vi_visual_t *vis, int current_line_no,
    line_t *target_line, int target_col)
{
    int target_line_no;

    if (!target_line) {
        return -1;
    }
    target_line_no = vi_line_number_for_mark(b, target_line);
    if (target_line_no < 1) {
        return -1;
    }
    if ((vis->pending_op == 'd' || vis->pending_op == 'c' || vis->pending_op == 'y') &&
        (vis->cursor_col != 0 || target_line_no <= current_line_no)) {
        /*
         * Real vi only coerces search motions linewise for delete/change/yank
         * when a column-zero search target lands on a later line.
         * Backward and wrapped same-line matches stay charwise.
         */
        return -1;
    }
    return vi_apply_charwise_linewise_motion(b, vis, current_line_no, target_line,
        target_col);
}

static void
vi_handle_pending_operator(buffer_t *b, vi_visual_t *vis, int key)
{
    int line_no = buf_current_line(b);
    int raw_count = vis->pending_count;
    int motion_count = vi_take_count(vis);
    int op_count = vis->pending_op_count > 0 ? vis->pending_op_count : 1;
    int count = vi_multiply_counts(op_count, motion_count);
    int end;
    int last_line = vi_clamp_line_target(b, line_no + count - 1);

    if (vis->pending_op == '>' && key == '>') {
        if (vi_shift_range(b, vis, line_no, last_line, 1) != 0) {
            write(STDOUT_FILENO, "\a", 1);
        }
    } else if (vis->pending_op == '<' && key == '<') {
        if (vi_shift_range(b, vis, line_no, last_line, 0) != 0) {
            write(STDOUT_FILENO, "\a", 1);
        }
    } else if (vis->pending_op == 'd' && key == 'd') {
        vi_linewise_delete(b, vis, line_no, last_line);
        vi_set_last_change(vis, VI_REPEAT_DD, count, 0);
    } else if (vis->pending_op == 'c' && key == 'c') {
        if (count == 1) {
            char regarg[2];

            vi_take_register_arg(vis, regarg);
            handle_yank_command(b, regarg, 1, line_no, last_line);
            vi_substitute_line(b, vis, 0);
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
        vi_set_last_change(vis, VI_REPEAT_CC, count, 0);
    } else if (vis->pending_op == 'y' && key == 'y') {
        vi_linewise_yank(b, vis, line_no, last_line);
    } else if ((vis->pending_op == 'd' || vis->pending_op == 'c' || vis->pending_op == 'y' ||
        vis->pending_op == '>' || vis->pending_op == '<') &&
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
                if (vi_apply_linewise_operator(b, vis, start, end) != 0) {
                    write(STDOUT_FILENO, "\a", 1);
                } else if (vis->pending_op == 'c') {
                    vi_set_last_change(vis, VI_REPEAT_C_MARK_LINE, 1, mark_key);
                    vis->last_change_aux = mark_line - line_no;
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
        } else if (vis->pending_op == 'c') {
            vi_set_last_change(vis, VI_REPEAT_C_MARK_EXACT, 1, mark_key);
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
            if (vis->pending_op == 'c') {
                vi_set_last_change(vis, VI_REPEAT_C_CHARS, count, 1);
            }
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
            if (vis->pending_op == 'c') {
                vi_set_last_change(vis, VI_REPEAT_C_CHARS, count, -1);
            }
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
    } else if (vis->pending_op == 'y' && key == 'W') {
        vi_find_bigword_boundary_forward_count(b->cur, vis->cursor_col, count, &end);
        if (vi_yank_span(vis, b->cur, vis->cursor_col, end) != 0) {
            write(STDOUT_FILENO, "\a", 1);
        }
    } else if ((vis->pending_op == 'd' || vis->pending_op == 'c' || vis->pending_op == 'y' ||
        vis->pending_op == '>' || vis->pending_op == '<') &&
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
            if (vi_apply_linewise_operator(b, vis, start, end_line) != 0) {
                write(STDOUT_FILENO, "\a", 1);
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
            (target_line == b->cur && target_col > vis->cursor_col)) {
            write(STDOUT_FILENO, "\a", 1);
        } else if (target_line == b->cur && target_col == vis->cursor_col) {
            write(STDOUT_FILENO, "\a", 1);
        } else if (vis->pending_op == '>' || vis->pending_op == '<') {
            if (vi_apply_charwise_linewise_motion(b, vis, line_no, target_line,
                target_col) != 0) {
                write(STDOUT_FILENO, "\a", 1);
            }
        } else if (vi_apply_charwise_motion(b, vis, target_line, target_col, 1) != 0) {
            write(STDOUT_FILENO, "\a", 1);
        } else if (vis->pending_op == 'c') {
            vi_set_last_change(vis, VI_REPEAT_C_BACK_END, count, motion == 'E');
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
    } else if ((vis->pending_op == 'd' || vis->pending_op == 'c' || vis->pending_op == 'y' ||
        vis->pending_op == '>' || vis->pending_op == '<') &&
        key == '_') {
        if (vi_apply_linewise_operator(b, vis, line_no, last_line) != 0) {
            write(STDOUT_FILENO, "\a", 1);
        } else if (vis->pending_op == 'c') {
            vi_set_last_change(vis, VI_REPEAT_C_LINE_MOTION, count, '_');
            vis->last_change_aux = count - 1;
        }
    } else if ((vis->pending_op == 'd' || vis->pending_op == 'c' || vis->pending_op == 'y' ||
        vis->pending_op == '>' || vis->pending_op == '<') &&
        (key == 'j' || key == '+' || key == '\r' || key == '\n')) {
        int start = line_no;
        int end_line = vi_clamp_line_target(b, line_no + count);

        if (vi_apply_linewise_operator(b, vis, start, end_line) != 0) {
            write(STDOUT_FILENO, "\a", 1);
        } else if (vis->pending_op == 'c') {
            vi_set_last_change(vis, VI_REPEAT_C_LINE_MOTION, count, key);
            vis->last_change_aux = count;
        }
    } else if ((vis->pending_op == 'd' || vis->pending_op == 'c' || vis->pending_op == 'y' ||
        vis->pending_op == '>' || vis->pending_op == '<') &&
        (key == 'H' || key == 'M' || key == 'L')) {
        int target;
        int start = line_no;
        int end_line;
        int screen_count = (raw_count > 0) ? raw_count :
            ((vis->pending_op_count > 1) ? vis->pending_op_count : 0);

        if (key == 'H') {
            target = vi_screen_target_line(b, vis, 0, screen_count);
        } else if (key == 'M') {
            target = vi_screen_target_line(b, vis, 1, 0);
        } else {
            target = vi_screen_target_line(b, vis, 2, screen_count);
        }
        end_line = target;
        if (start > end_line) {
            int tmp = start;

            start = end_line;
            end_line = tmp;
        }
        if (vi_apply_linewise_operator(b, vis, start, end_line) != 0) {
            write(STDOUT_FILENO, "\a", 1);
        } else if (vis->pending_op == 'c') {
            vi_set_last_change(vis, VI_REPEAT_C_MOTION, count, key);
            vis->last_change_aux = screen_count;
        }
    } else if ((vis->pending_op == 'd' || vis->pending_op == 'c' || vis->pending_op == 'y' ||
        vis->pending_op == '>' || vis->pending_op == '<') &&
        (key == 'k' || key == '-')) {
        int start = vi_clamp_line_target(b, line_no - count);
        int end_line = line_no;

        if (line_no - count < 1) {
            write(STDOUT_FILENO, "\a", 1);
        } else if (vi_apply_linewise_operator(b, vis, start, end_line) != 0) {
            write(STDOUT_FILENO, "\a", 1);
        } else if (vis->pending_op == 'c') {
            vi_set_last_change(vis, VI_REPEAT_C_LINE_MOTION, count, key);
            vis->last_change_aux = -count;
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
    } else if ((vis->pending_op == '>' || vis->pending_op == '<') && key == 'w') {
        line_t *target_line;
        int target_col;

        if (vi_simulate_motion_target(b, vis, vi_move_word_forward_count, count,
            &target_line, &target_col) != 0 ||
            vi_apply_charwise_linewise_motion(b, vis, line_no, target_line,
                target_col) != 0) {
            write(STDOUT_FILENO, "\a", 1);
        }
    } else if ((vis->pending_op == 'd' || vis->pending_op == 'c') && key == 'w') {
        int rc;

        if (vis->pending_op == 'c') {
            rc = vi_find_change_word_target(b->cur, vis->cursor_col, count, 0, &end);
        } else {
            vi_find_word_boundary_forward_count(b->cur, vis->cursor_col, count, &end);
            rc = (end >= vis->cursor_col) ? 0 : -1;
        }
        if (rc == 0 && end >= vis->cursor_col) {
            vi_delete_span(b, vis, vis->cursor_col, end, vis->pending_op == 'c');
            if (vis->pending_op == 'd') {
                vi_set_last_change(vis, VI_REPEAT_DW, count, 0);
            } else {
                vi_set_last_change(vis, VI_REPEAT_C_WORD, count, 0);
            }
        } else {
            write(STDOUT_FILENO, "\a", 1);
        }
    } else if ((vis->pending_op == '>' || vis->pending_op == '<') && key == 'e') {
        line_t *target_line;
        int target_col;

        if (vi_simulate_motion_target(b, vis, vi_move_word_end_count, count, &target_line,
            &target_col) != 0 ||
            vi_apply_charwise_linewise_motion(b, vis, line_no, target_line,
                target_col) != 0) {
            write(STDOUT_FILENO, "\a", 1);
        }
    } else if ((vis->pending_op == 'd' || vis->pending_op == 'c') && key == 'e') {
        if (vi_find_word_end_exclusive_count(b->cur, vis->cursor_col, count, &end) == 0 &&
            end > vis->cursor_col) {
            vi_delete_span(b, vis, vis->cursor_col, end, vis->pending_op == 'c');
            if (vis->pending_op == 'c') {
                vi_set_last_change(vis, VI_REPEAT_C_END, count, 0);
            }
        } else {
            write(STDOUT_FILENO, "\a", 1);
        }
    } else if ((vis->pending_op == '>' || vis->pending_op == '<') && key == 'E') {
        line_t *target_line;
        int target_col;

        if (vi_simulate_motion_target(b, vis, vi_move_bigword_end_count, count,
            &target_line, &target_col) != 0 ||
            vi_apply_charwise_linewise_motion(b, vis, line_no, target_line,
                target_col) != 0) {
            write(STDOUT_FILENO, "\a", 1);
        }
    } else if ((vis->pending_op == 'd' || vis->pending_op == 'c') && key == 'E') {
        if (vi_find_bigword_end_exclusive_count(b->cur, vis->cursor_col, count, &end) == 0 &&
            end > vis->cursor_col) {
            vi_delete_span(b, vis, vis->cursor_col, end, vis->pending_op == 'c');
            if (vis->pending_op == 'c') {
                vi_set_last_change(vis, VI_REPEAT_C_END, count, 1);
            }
        } else {
            write(STDOUT_FILENO, "\a", 1);
        }
    } else if ((vis->pending_op == '>' || vis->pending_op == '<') && key == 'b') {
        line_t *target_line;
        int target_col;

        if (vi_simulate_motion_target(b, vis, vi_move_word_backward_count, count,
            &target_line, &target_col) != 0 ||
            vi_apply_charwise_linewise_motion(b, vis, line_no, target_line,
                target_col) != 0) {
            write(STDOUT_FILENO, "\a", 1);
        }
    } else if ((vis->pending_op == 'd' || vis->pending_op == 'c' || vis->pending_op == 'y') &&
        key == 'b') {
        line_t *target_line;
        int target_col;

        if (vi_simulate_motion_target(b, vis, vi_move_word_backward_count, count,
            &target_line, &target_col) != 0 ||
            (target_line == b->cur && target_col >= vis->cursor_col) ||
            vi_apply_backward_start_motion(b, vis, target_line, target_col) != 0) {
            write(STDOUT_FILENO, "\a", 1);
        } else if (vis->pending_op == 'c') {
            vi_set_last_change(vis, VI_REPEAT_C_MOTION, count, key);
        }
    } else if ((vis->pending_op == '>' || vis->pending_op == '<') && key == 'B') {
        line_t *target_line;
        int target_col;

        if (vi_simulate_motion_target(b, vis, vi_move_bigword_backward_count, count,
            &target_line, &target_col) != 0 ||
            vi_apply_charwise_linewise_motion(b, vis, line_no, target_line,
                target_col) != 0) {
            write(STDOUT_FILENO, "\a", 1);
        }
    } else if ((vis->pending_op == 'd' || vis->pending_op == 'c' || vis->pending_op == 'y') &&
        key == 'B') {
        line_t *target_line;
        int target_col;

        if (vi_simulate_motion_target(b, vis, vi_move_bigword_backward_count, count,
            &target_line, &target_col) != 0 ||
            (target_line == b->cur && target_col >= vis->cursor_col) ||
            vi_apply_backward_start_motion(b, vis, target_line, target_col) != 0) {
            write(STDOUT_FILENO, "\a", 1);
        } else if (vis->pending_op == 'c') {
            vi_set_last_change(vis, VI_REPEAT_C_MOTION, count, key);
        }
    } else if ((vis->pending_op == '>' || vis->pending_op == '<') && key == 'W') {
        line_t *target_line;
        int target_col;

        if (vi_simulate_motion_target(b, vis, vi_move_bigword_forward_count, count,
            &target_line, &target_col) != 0 ||
            vi_apply_charwise_linewise_motion(b, vis, line_no, target_line,
                target_col) != 0) {
            write(STDOUT_FILENO, "\a", 1);
        }
    } else if ((vis->pending_op == 'd' || vis->pending_op == 'c') && key == 'W') {
        int rc;

        if (vis->pending_op == 'c') {
            rc = vi_find_change_word_target(b->cur, vis->cursor_col, count, 1, &end);
        } else {
            vi_find_bigword_boundary_forward_count(b->cur, vis->cursor_col, count, &end);
            rc = (end >= vis->cursor_col) ? 0 : -1;
        }
        if (rc == 0 && end >= vis->cursor_col) {
            vi_delete_span(b, vis, vis->cursor_col, end, vis->pending_op == 'c');
            if (vis->pending_op == 'c') {
                vi_set_last_change(vis, VI_REPEAT_C_WORD, count, 1);
            }
        } else {
            write(STDOUT_FILENO, "\a", 1);
        }
    } else if ((vis->pending_op == 'd' || vis->pending_op == 'c' ||
        vis->pending_op == '>' || vis->pending_op == '<') &&
        (key == 'f' || key == 'F' || key == 't' || key == 'T')) {
        int ch;
        int start;

        ch = vi_read_visual_key(b, vis, ':', NULL);
        if (ch == -1 || ch == '\x1b' || ch == '\r' || ch == '\n') {
            write(STDOUT_FILENO, "\a", 1);
        } else if (vis->pending_op == 'c' && vi_change_find_is_zero_width_insert(b->cur,
            vis->cursor_col, ch, (key == 'f' || key == 't'), (key == 't' || key == 'T'),
            count)) {
            vis->last_find_char = ch;
            vis->last_find_forward = (key == 'f' || key == 't');
            vis->last_find_till = (key == 't' || key == 'T');
            vi_start_change_insert(b, vis);
            vi_set_last_change(vis, VI_REPEAT_C_FIND, count, key);
        } else if (((vis->pending_op == 'c')
                ? vi_find_change_motion_span(b->cur, vis->cursor_col, ch,
                    (key == 'f' || key == 't'),
                    (key == 't' || key == 'T'),
                    count, &start, &end)
                : vi_find_motion_span(b->cur, vis->cursor_col, ch,
                    (key == 'f' || key == 't'),
                    (key == 't' || key == 'T'),
                    count, &start, &end)) == 0) {
            if (vis->pending_op == '>' || vis->pending_op == '<') {
                if (vi_apply_linewise_operator(b, vis, line_no, line_no) != 0) {
                    write(STDOUT_FILENO, "\a", 1);
                } else {
                    vis->last_find_char = ch;
                    vis->last_find_forward = (key == 'f' || key == 't');
                    vis->last_find_till = (key == 't' || key == 'T');
                }
            } else {
                vi_delete_span(b, vis, start, end, vis->pending_op == 'c');
                vis->last_find_char = ch;
                vis->last_find_forward = (key == 'f' || key == 't');
                vis->last_find_till = (key == 't' || key == 'T');
                if (vis->pending_op == 'd') {
                    vi_set_last_change(vis, VI_REPEAT_D_FIND, count, 0);
                } else if (vis->pending_op == 'c') {
                    vi_set_last_change(vis, VI_REPEAT_C_FIND, count, key);
                }
            }
        } else {
            write(STDOUT_FILENO, "\a", 1);
        }
    } else if ((vis->pending_op == 'd' || vis->pending_op == 'c' || vis->pending_op == 'y' ||
        vis->pending_op == '>' || vis->pending_op == '<')
        && (key == ';' || key == ',')) {
        int start;
        int forward = vis->last_find_forward;

        if (!vis->last_find_char) {
            write(STDOUT_FILENO, "\a", 1);
        } else {
            if (key == ',') {
                forward = !forward;
            }
            if (((vis->pending_op == 'c')
                    ? vi_find_change_motion_span(b->cur, vis->cursor_col,
                        vis->last_find_char, forward, vis->last_find_till,
                        count, &start, &end)
                    : vi_find_motion_span(b->cur, vis->cursor_col,
                        vis->last_find_char, forward, vis->last_find_till,
                        count, &start, &end)) == 0) {
                if (vis->pending_op == '>' || vis->pending_op == '<') {
                    if (vi_apply_linewise_operator(b, vis, line_no, line_no) != 0) {
                        write(STDOUT_FILENO, "\a", 1);
                    }
                } else if (vis->pending_op == 'y') {
                    if (vi_yank_span(vis, b->cur, start, end) != 0) {
                        write(STDOUT_FILENO, "\a", 1);
                    }
                } else {
                    vi_delete_span(b, vis, start, end, vis->pending_op == 'c');
                    if (vis->pending_op == 'd') {
                        vi_set_last_change(vis, VI_REPEAT_D_FIND, count, 0);
                    } else if (vis->pending_op == 'c') {
                        vi_set_last_change(vis, VI_REPEAT_C_FIND, count, key);
                    }
                }
            } else {
                write(STDOUT_FILENO, "\a", 1);
            }
        }
    } else if ((vis->pending_op == 'd' || vis->pending_op == 'c' || vis->pending_op == 'y' ||
        vis->pending_op == '>' || vis->pending_op == '<') &&
        (key == ')' || key == '(' || key == '}' || key == '{')) {
        line_t *target_line;
        int target_col;
        int rc;
        int eof_fallback = 0;
        int try_operator_linewise;

        if ((vis->pending_op == '>' || vis->pending_op == '<') &&
            key == ')' && b->cur && vi_line_is_blank(b->cur)) {
            vis->pending_op = 0;
            vis->pending_op_count = 0;
            vis->pending_reg = 0;
            return;
        }
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
        if ((vis->pending_op == 'd' || vis->pending_op == 'c' ||
                vis->pending_op == 'y') && b->cur && vi_line_is_blank(b->cur) &&
            vis->cursor_col == 0 && (key == ')' || key == '}')) {
            if (key == ')') {
                target_line = b->cur->next;
                while (target_line && vi_line_is_blank(target_line)) {
                    target_line = target_line->next;
                }
                if (target_line) {
                    target_col = vi_first_nonblank_col(target_line);
                    rc = 0;
                }
            } else if (b->tail) {
                target_line = b->tail;
                target_col = (int)b->tail->len;
                rc = 0;
            }
        }
        if (rc != 0 && (vis->pending_op == 'd' || vis->pending_op == 'c' ||
                vis->pending_op == 'y') && (key == ')' || key == '}') && b->tail) {
            target_line = b->tail;
            if (key == '}' && !vi_line_is_blank(b->tail)) {
                target_col = (int)b->tail->len;
            } else {
                target_col = (int)b->tail->len;
            }
            rc = 0;
            eof_fallback = 1;
        } else if (rc == 0 && key == '{' && target_line &&
            !vi_line_is_blank(target_line) &&
            target_col == vi_first_nonblank_col(target_line) && target_line->prev &&
            vi_line_is_blank(target_line->prev)) {
            target_line = target_line->prev;
            target_col = 0;
        } else if (rc == 0 && (vis->pending_op == 'd' || vis->pending_op == 'c' ||
                vis->pending_op == 'y') && (key == ')' || key == '}') &&
            target_line && !vi_line_is_blank(target_line) &&
            target_col == vi_first_nonblank_col(target_line) && target_line->prev &&
            target_line->prev != b->cur &&
            vi_line_is_blank(target_line->prev)) {
            target_line = target_line->prev;
            target_col = 0;
        } else if (rc == 0 && (vis->pending_op == 'd' || vis->pending_op == 'c' ||
                vis->pending_op == 'y') && key == '}' && target_line == b->tail
            && target_line && !vi_line_is_blank(target_line)
            && target_col == vi_first_nonblank_col(target_line)) {
            target_col = (int)target_line->len;
        } else if (rc == 0 && eof_fallback && (vis->pending_op == 'd' || vis->pending_op == 'c' ||
                vis->pending_op == 'y') && key == ')' && target_line == b->tail) {
            target_col = (int)target_line->len;
        } else if ((vis->pending_op == 'd' || vis->pending_op == 'c' ||
                vis->pending_op == 'y') && b->cur && vi_line_is_blank(b->cur) &&
            vis->cursor_col == 0 && rc == 0 && target_line == b->cur && target_col == 0) {
            if (key == ')' && b->cur->next) {
                target_line = b->cur->next;
                while (target_line && vi_line_is_blank(target_line)) {
                    target_line = target_line->next;
                }
                if (!target_line) {
                    rc = -1;
                } else {
                    target_col = vi_first_nonblank_col(target_line);
                }
            } else if (key == '}' && b->tail) {
                target_line = b->tail;
                target_col = (int)b->tail->len;
            }
        } else if (rc == 0 && (vis->pending_op == '>' || vis->pending_op == '<') &&
            key == ')' && b->cur && vi_line_is_blank(b->cur) && vis->cursor_col == 0 &&
            target_line == b->cur && target_col == 0) {
            vis->pending_op = 0;
            vis->pending_op_count = 0;
            vis->pending_reg = 0;
            return;
        } else if (rc == 0 && (vis->pending_op == '>' || vis->pending_op == '<') &&
            key == '}' && b->cur && vi_line_is_blank(b->cur) && vis->cursor_col == 0 &&
            target_line && target_line != b->cur && vi_line_is_blank(target_line) &&
            target_col == 0) {
            int target_line_no = vi_line_number_for_mark(b, target_line);
            int start_line = line_no + 1;
            int end_line = target_line_no - 1;

            if (start_line <= end_line) {
                if (vi_apply_linewise_operator(b, vis, start_line, end_line) != 0) {
                    write(STDOUT_FILENO, "\a", 1);
                }
            }
            vis->pending_op = 0;
            vis->pending_op_count = 0;
            vis->pending_reg = 0;
            return;
        }
        try_operator_linewise = (vis->pending_op == 'd' || vis->pending_op == 'c' ||
            vis->pending_op == 'y') && b->cur && target_line && target_line != b->cur &&
            vis->cursor_col == 0 &&
            (target_col == 0 || target_col == vi_first_nonblank_col(target_line));
        if (rc != 0) {
            write(STDOUT_FILENO, "\a", 1);
        } else if (try_operator_linewise) {
            if (vi_apply_charwise_linewise_motion(b, vis, line_no, target_line,
                target_col) != 0) {
                write(STDOUT_FILENO, "\a", 1);
            } else if (vis->pending_op == 'c') {
                vi_set_last_change(vis, VI_REPEAT_C_MOTION, count, key);
            }
        } else if (vi_apply_search_linewise_motion(b, vis, line_no, target_line, target_col) != 0) {
            if (vis->pending_op == '>' || vis->pending_op == '<' ||
                vi_apply_charwise_motion(b, vis, target_line, target_col, 0) != 0) {
                write(STDOUT_FILENO, "\a", 1);
            } else if (vis->pending_op == 'c') {
                vi_set_last_change(vis, VI_REPEAT_C_MOTION, count, key);
            }
        } else if (vis->pending_op == 'c') {
            vi_set_last_change(vis, VI_REPEAT_C_MOTION, count, key);
        }
    } else if ((vis->pending_op == 'd' || vis->pending_op == 'c' || vis->pending_op == 'y' ||
        vis->pending_op == '>' || vis->pending_op == '<') &&
        (key == '/' || key == '?')) {
        char pattern[256];
        int forward = (key == '/');
        line_t *target_line;
        int target_col;

        pattern[0] = '\0';
        if (vi_prompt_input(b, vis, key, pattern, sizeof(pattern)) != 0) {
            /* Cancelled prompt: abandon the pending operator quietly. */
        } else if (vi_search_motion_target(b, vis, pattern, forward, count,
            &target_line, &target_col) != 0) {
            write(STDOUT_FILENO, "\a", 1);
            vi_set_search_failure_status(vis, pattern);
        } else if (vi_apply_search_linewise_motion(b, vis, line_no, target_line, target_col) == 0) {
            if (vis->pending_op == 'c') {
                vi_set_last_change(vis, VI_REPEAT_C_SEARCH_REPEAT, count, key);
            }
        } else if (vis->pending_op == '>' || vis->pending_op == '<') {
            if (vi_apply_search_linewise_motion(b, vis, line_no, target_line, target_col) != 0) {
                vi_set_search_failure_status(vis, pattern);
                write(STDOUT_FILENO, "\a", 1);
            }
        } else if (vis->pending_op == 'c' && target_line == b->cur &&
            target_col == vis->cursor_col) {
            vi_start_change_insert(b, vis);
            vi_set_last_change(vis, VI_REPEAT_C_SEARCH_REPEAT, count, key);
        } else if (vi_apply_search_charwise_motion(b, vis, target_line, target_col) != 0) {
            vi_set_search_failure_status(vis, pattern);
            write(STDOUT_FILENO, "\a", 1);
        } else if (vis->pending_op == 'c') {
            vi_set_last_change(vis, VI_REPEAT_C_SEARCH_REPEAT, count, key);
        }
    } else if ((vis->pending_op == 'd' || vis->pending_op == 'c' || vis->pending_op == 'y' ||
        vis->pending_op == '>' || vis->pending_op == '<') &&
        (key == '[' || key == ']')) {
        int key2;
        line_t *target_line = NULL;
        int target_col = 0;
        int target_no;
        int start_line;
        int end_line;
        int rc = -1;

        key2 = vi_read_visual_key(b, vis, ':', NULL);
        if (key2 == -1 || key2 == '\x1b') {
            write(STDOUT_FILENO, "\a", 1);
        } else {
            if (key == '[' && key2 == '[') {
                rc = vi_simulate_motion_target(b, vis, vi_move_section_start_backward, count,
                    &target_line, &target_col);
            } else if (key == ']' && key2 == ']') {
                rc = vi_simulate_motion_target(b, vis, vi_move_section_start_forward, count,
                    &target_line, &target_col);
            } else if (key == '[' && key2 == ']') {
                rc = vi_simulate_motion_target(b, vis, vi_move_section_end_backward, count,
                    &target_line, &target_col);
            } else if (key == ']' && key2 == '[') {
                rc = vi_simulate_motion_target(b, vis, vi_move_section_end_forward, count,
                    &target_line, &target_col);
            }
            if (rc != 0 || !target_line) {
                write(STDOUT_FILENO, "\a", 1);
            } else {
                target_no = vi_line_number_for_mark(b, target_line);
                if (target_no < 1 || target_no == line_no) {
                    write(STDOUT_FILENO, "\a", 1);
                } else {
                    if (target_no < line_no) {
                        start_line = target_no;
                        end_line = line_no - 1;
                    } else {
                        start_line = line_no;
                        end_line = target_no - 1;
                    }
                    if (end_line < start_line) {
                        write(STDOUT_FILENO, "\a", 1);
                    } else if (vi_apply_linewise_operator(b, vis, start_line, end_line) != 0) {
                        write(STDOUT_FILENO, "\a", 1);
                    }
                }
            }
        }
    } else if ((vis->pending_op == 'd' || vis->pending_op == 'c' || vis->pending_op == 'y' ||
        vis->pending_op == '>' || vis->pending_op == '<') &&
        (key == '\'' || key == '`')) {
        int mark;
        line_t *target_line;
        int target_col;

        mark = vi_read_visual_key(b, vis, ':', NULL);
        if (mark < 'a' || mark > 'z' || !b->marks[mark - 'a']) {
            write(STDOUT_FILENO, "\a", 1);
        } else {
            target_line = b->marks[mark - 'a'];
            if (key == '\'') {
                int target_no = vi_line_number_for_mark(b, target_line);
                int start_line;
                int end_line;

                if (target_no < 1 || target_no == line_no) {
                    write(STDOUT_FILENO, "\a", 1);
                } else {
                    if (target_no < line_no) {
                        start_line = target_no;
                        end_line = line_no - 1;
                    } else {
                        start_line = line_no;
                        end_line = target_no - 1;
                    }
                    if (end_line < start_line) {
                        write(STDOUT_FILENO, "\a", 1);
                    } else if (vi_apply_linewise_operator(b, vis, start_line, end_line) != 0) {
                        write(STDOUT_FILENO, "\a", 1);
                    }
                }
            } else {
                if (vis->pending_op == '>' || vis->pending_op == '<') {
                    int target_no = vi_line_number_for_mark(b, target_line);
                    int start_line;
                    int end_line;

                    if (target_no < 1) {
                        write(STDOUT_FILENO, "\a", 1);
                    } else {
                        start_line = line_no;
                        end_line = target_no;
                        if (start_line > end_line) {
                            int tmp = start_line;

                            start_line = end_line;
                            end_line = tmp;
                        }
                        if (vi_apply_linewise_operator(b, vis, start_line, end_line) != 0) {
                            write(STDOUT_FILENO, "\a", 1);
                        }
                    }
                } else {
                    target_col = b->mark_cols[mark - 'a'];
                    if ((size_t)target_col > target_line->len) {
                        target_col = (int)target_line->len;
                    }
                    if (vi_apply_charwise_motion(b, vis, target_line, target_col, 0) != 0) {
                        write(STDOUT_FILENO, "\a", 1);
                    }
                }
            }
        }
    } else if ((vis->pending_op == 'd' || vis->pending_op == 'c' || vis->pending_op == 'y' ||
        vis->pending_op == '>' || vis->pending_op == '<') &&
        (key == 'n' || key == 'N')) {
        int forward = (key == 'n') ? vis->last_search_forward : !vis->last_search_forward;
        line_t *target_line;
        int target_col;

        if (vi_search_motion_target(b, vis, "", forward, count,
            &target_line, &target_col) != 0) {
            write(STDOUT_FILENO, "\a", 1);
        } else if (vi_apply_search_linewise_motion(b, vis, line_no, target_line, target_col) == 0) {
            if (vis->pending_op == 'c') {
                vi_set_last_change(vis, VI_REPEAT_C_SEARCH_REPEAT, count, key);
            }
        } else if (vis->pending_op == '>' || vis->pending_op == '<') {
            if (vi_apply_search_linewise_motion(b, vis, line_no, target_line, target_col) != 0) {
                write(STDOUT_FILENO, "\a", 1);
            }
        } else if (vis->pending_op == 'c' && target_line == b->cur &&
            target_col == vis->cursor_col) {
            vi_start_change_insert(b, vis);
            vi_set_last_change(vis, VI_REPEAT_C_SEARCH_REPEAT, count, key);
        } else if (vi_apply_search_charwise_motion(b, vis, target_line, target_col) != 0) {
            write(STDOUT_FILENO, "\a", 1);
        } else if (vis->pending_op == 'c') {
            vi_set_last_change(vis, VI_REPEAT_C_SEARCH_REPEAT, count, key);
        }
    } else if ((vis->pending_op == 'd' || vis->pending_op == 'c' || vis->pending_op == 'y' ||
        vis->pending_op == '>' || vis->pending_op == '<') &&
        (key == '*' || key == '#')) {
        char *pattern = vi_current_word_pattern(b, vis);
        line_t *target_line;
        int target_col;

        if (!pattern) {
            write(STDOUT_FILENO, "\a", 1);
        } else if (vi_search_motion_target(b, vis, pattern, key == '*', count,
            &target_line, &target_col) != 0) {
            write(STDOUT_FILENO, "\a", 1);
        } else if (vi_apply_search_linewise_motion(b, vis, line_no, target_line, target_col) == 0) {
            if (vis->pending_op == 'c') {
                vi_set_last_change(vis, VI_REPEAT_C_SEARCH_REPEAT, count, key);
            }
        } else if (vis->pending_op == '>' || vis->pending_op == '<') {
            if (vi_apply_search_linewise_motion(b, vis, line_no, target_line, target_col) != 0) {
                write(STDOUT_FILENO, "\a", 1);
            }
        } else if (vis->pending_op == 'c' && target_line == b->cur &&
            target_col == vis->cursor_col) {
            vi_start_change_insert(b, vis);
            vi_set_last_change(vis, VI_REPEAT_C_SEARCH_REPEAT, count, key);
        } else if (vi_apply_search_charwise_motion(b, vis, target_line, target_col) != 0) {
            write(STDOUT_FILENO, "\a", 1);
        } else if (vis->pending_op == 'c') {
            vi_set_last_change(vis, VI_REPEAT_C_SEARCH_REPEAT, count, key);
        }
        free(pattern);
    } else if ((vis->pending_op == 'd' || vis->pending_op == 'c') && key == '0') {
        if (vis->cursor_col > 0) {
            vi_delete_span(b, vis, 0, vis->cursor_col, vis->pending_op == 'c');
            if (vis->pending_op == 'c') {
                vi_set_last_change(vis, VI_REPEAT_C_TO_COL0, count, 0);
            }
        } else if (vis->pending_op == 'c') {
            vi_start_change_insert(b, vis);
            vi_set_last_change(vis, VI_REPEAT_C_TO_COL0, count, 0);
        } else {
            write(STDOUT_FILENO, "\a", 1);
        }
    } else if ((vis->pending_op == 'd' || vis->pending_op == 'c') && key == '^') {
        int start = vi_first_nonblank_col(b->cur);

        if (start < vis->cursor_col) {
            vi_delete_span(b, vis, start, vis->cursor_col, vis->pending_op == 'c');
            if (vis->pending_op == 'c') {
                vi_set_last_change(vis, VI_REPEAT_C_TO_FIRST_NONBLANK, count, 0);
            }
        } else if (vis->pending_op == 'c' && start == vis->cursor_col && b->cur &&
            (size_t)start < b->cur->len) {
            vi_delete_span(b, vis, start, start + 1, 1);
            vi_set_last_change(vis, VI_REPEAT_C_TO_FIRST_NONBLANK, count, 0);
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
            } else {
                vi_set_last_change(vis, VI_REPEAT_C_EOL_CHANGE, 1, 0);
            }
        } else {
            write(STDOUT_FILENO, "\a", 1);
        }
    } else if ((vis->pending_op == '>' || vis->pending_op == '<') && key == '$') {
        line_t *target_line;
        int target_col;

        if (count > 1) {
            int start = line_no;
            int end_line;

            if (vi_eol_motion_target(b, count, &target_line, &target_col) != 0) {
                write(STDOUT_FILENO, "\a", 1);
            } else {
                end_line = vi_line_number_for_mark(b, target_line);
                if (end_line < start) {
                    int tmp = start;

                    start = end_line;
                    end_line = tmp;
                }
                if (vi_apply_linewise_operator(b, vis, start, end_line) != 0) {
                    write(STDOUT_FILENO, "\a", 1);
                }
            }
        } else if (vi_apply_linewise_operator(b, vis, line_no, line_no) != 0) {
            write(STDOUT_FILENO, "\a", 1);
        }
    } else if ((vis->pending_op == 'd' || vis->pending_op == 'c' || vis->pending_op == 'y' ||
        vis->pending_op == '>' || vis->pending_op == '<') &&
        key == 'G') {
        int start = line_no;
        int target = (raw_count > 0) ? raw_count :
            ((vis->pending_op_count > 1) ? vis->pending_op_count : b->line_count);
        int end_line = vi_clamp_line_target(b, target);

        if (start > end_line) {
            int tmp = start;

            start = end_line;
            end_line = tmp;
        }
        if (vi_apply_linewise_operator(b, vis, start, end_line) != 0) {
            write(STDOUT_FILENO, "\a", 1);
        }
    } else if ((vis->pending_op == 'd' || vis->pending_op == 'c' || vis->pending_op == 'y' ||
        vis->pending_op == '>' || vis->pending_op == '<')
        && key == '%') {
        int percent_count = (raw_count > 0) ? raw_count :
            ((vis->pending_op_count > 1) ? vis->pending_op_count : 0);

        if (percent_count > 0) {
            int target = (percent_count * b->line_count + 99) / 100;
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
            } else if (vis->pending_op == '>' || vis->pending_op == '<') {
                if (vi_apply_linewise_operator(b, vis, start, end_line) != 0) {
                    write(STDOUT_FILENO, "\a", 1);
                }
            } else {
                vi_linewise_change(b, vis, start, end_line);
            }
        } else {
            line_t *target_line;
            int target_col;
            int target_no;
            int scan_start_col;
            int start;
            int end_line;

            if (vi_find_scanned_cross_bracket_span(b->cur, vis->cursor_col, &scan_start_col,
                &target_line, &target_col) == 0) {
                if (vis->pending_op == '>' || vis->pending_op == '<') {
                    target_no = vi_line_number_for_mark(b, target_line);
                    if (target_no < 1) {
                        write(STDOUT_FILENO, "\a", 1);
                    } else {
                        start = line_no;
                        end_line = target_no;
                        if (start > end_line) {
                            int tmp = start;

                            start = end_line;
                            end_line = tmp;
                        }
                        if (vi_apply_linewise_operator(b, vis, start, end_line) != 0) {
                            write(STDOUT_FILENO, "\a", 1);
                        }
                    }
                } else if (vis->pending_op == 'y') {
                    if (vi_yank_range(b, vis, b->cur, scan_start_col, target_line,
                        target_col + 1) != 0) {
                        write(STDOUT_FILENO, "\a", 1);
                    } else {
                        vis->cursor_col = scan_start_col;
                    }
                } else {
                    vi_delete_range(b, vis, b->cur, scan_start_col, target_line,
                        target_col + 1, vis->pending_op == 'c');
                    if (vis->pending_op == 'c') {
                        vi_set_last_change(vis, VI_REPEAT_C_PERCENT, 1, 0);
                    }
                }
            } else if (vi_match_operator_target(b, vis, &target_line, &target_col) != 0 ||
                !target_line) {
                write(STDOUT_FILENO, "\a", 1);
            } else if (vis->pending_op == '>' || vis->pending_op == '<') {
                target_no = vi_line_number_for_mark(b, target_line);
                if (target_no < 1) {
                    write(STDOUT_FILENO, "\a", 1);
                } else {
                    start = line_no;
                    end_line = target_no;
                    if (start > end_line) {
                        int tmp = start;

                        start = end_line;
                        end_line = tmp;
                    }
                    if (vi_apply_linewise_operator(b, vis, start, end_line) != 0) {
                        write(STDOUT_FILENO, "\a", 1);
                    }
                }
            } else if (vi_apply_charwise_motion(b, vis, target_line, target_col, 1) != 0) {
                write(STDOUT_FILENO, "\a", 1);
            } else if (vis->pending_op == 'c') {
                vi_set_last_change(vis, VI_REPEAT_C_PERCENT, 1, 0);
            }
        }
    } else {
        write(STDOUT_FILENO, "\a", 1);
    }
    vis->pending_op = 0;
    vis->pending_op_count = 0;
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
        if (b->empty_origin && b->line_count == 1 && b->head == cur
                && b->tail == cur && cur->len > 0) {
            b->trailing_newline = 1;
        }
        vis->cursor_col++;
    }
    free(text);
}

static void
vi_replace_insert_char(buffer_t *b, vi_visual_t *vis, int ch)
{
    line_t *cur = vi_ensure_current_line(b);
    char *text;
    size_t old_len;
    char replaced;

    if (!cur) {
        return;
    }
    if ((size_t)vis->cursor_col >= cur->len) {
        old_len = cur->len;
        vi_insert_char(b, vis, ch);
        if (cur->len == old_len + 1 && (size_t)(vis->cursor_col - 1) == old_len) {
            vi_record_replace_insert(vis, cur, (int)old_len);
        }
        return;
    }
    replaced = cur->text[vis->cursor_col];
    text = strdup(cur->text);
    if (!text) {
        return;
    }
    text[vis->cursor_col] = (char)ch;
    if (vi_replace_current_text(b, text) == 0) {
        vi_record_replace_char(vis, cur, vis->cursor_col, replaced);
        vis->cursor_col++;
    }
    free(text);
}

static void
vi_insert_adjacent_char(buffer_t *b, vi_visual_t *vis, int below)
{
    line_t *cur = vi_ensure_current_line(b);
    line_t *adj;
    int display_col;
    int adj_idx;
    unsigned char ch;

    if (!cur) {
        return;
    }
    adj = below ? cur->next : cur->prev;
    if (!adj) {
        write(STDOUT_FILENO, "\a", 1);
        return;
    }
    display_col = vi_display_col_for_index(cur, vis->cursor_col);
    adj_idx = vi_index_for_display_col(adj, display_col);
    if ((size_t)adj_idx >= adj->len) {
        write(STDOUT_FILENO, "\a", 1);
        return;
    }
    ch = (unsigned char)adj->text[adj_idx];
    if (vis->replace_mode) {
        vi_replace_insert_char(b, vis, (int)ch);
    } else {
        vi_insert_char(b, vis, (int)ch);
    }
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
    if (vis->replace_mode) {
        vi_rewind_replace_to(b, vis, start_line, start_col);
        return;
    }
    vi_delete_range(b, vis, start_line, start_col, end_line, end_col, 1);
}

static void
vi_erase_word_forward_insert(buffer_t *b, vi_visual_t *vis)
{
    line_t *start_line = b->cur ? b->cur : b->head;
    int start_col = vis->cursor_col;
    line_t *end_line;
    int end_col;
    vi_visual_t tmp;

    if (!start_line) {
        return;
    }
    tmp = *vis;
    vi_move_word_forward_count(b, &tmp, 1);
    end_line = b->cur ? b->cur : b->head;
    end_col = tmp.cursor_col;
    b->cur = start_line;
    vis->cursor_col = start_col;
    if (!end_line) {
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
    if (vis->replace_mode) {
        vi_rewind_replace_to(b, vis, start_line, start_col);
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
    char *indent = NULL;
    char *left;
    char *right;
    char *newline_text = NULL;
    size_t indent_len = 0;
    size_t right_len;

    if (!cur) {
        return;
    }
    indent = vi_autoindent_prefix(cur, &indent_len);
    if (!indent) {
        return;
    }
    left = malloc((size_t)vis->cursor_col + 1);
    right = strdup(cur->text + vis->cursor_col);
    if (!left || !right) {
        free(indent);
        free(left);
        free(right);
        return;
    }
    memcpy(left, cur->text, (size_t)vis->cursor_col);
    left[vis->cursor_col] = '\0';
    right_len = strlen(right);
    newline_text = malloc(indent_len + right_len + 1);
    if (!newline_text) {
        free(indent);
        free(left);
        free(right);
        return;
    }
    if (indent_len > 0) {
        memcpy(newline_text, indent, indent_len);
    }
    memcpy(newline_text + indent_len, right, right_len + 1);
    if (vi_replace_current_text(b, left) != 0) {
        free(indent);
        free(left);
        free(right);
        free(newline_text);
        return;
    }
    b->cur = buf_insert_after(b, cur, newline_text);
    if (vis->replace_mode) {
        vi_record_replace_split(vis, cur, b->cur, vis->cursor_col);
    }
    if (b->started_empty && b->line_count == 2 && b->head && b->tail
            && b->head->len == 0 && b->tail->len == 0) {
        b->trailing_newline = 0;
    }
    vis->cursor_col = (int)indent_len;
    vi_set_insert_anchor(b, vis);
    free(indent);
    free(left);
    free(right);
    free(newline_text);
}

static void
vi_open_line(buffer_t *b, vi_visual_t *vis, int above)
{
    line_t *cur = b->cur ? b->cur : b->tail;
    line_t *pos;
    char *indent;
    size_t indent_len = 0;

    save_undo(b);
    indent = vi_autoindent_prefix(cur, &indent_len);
    if (!indent) {
        return;
    }
    if (!cur) {
        b->cur = buf_insert_after(b, NULL, indent);
    } else {
        pos = above ? cur->prev : cur;
        b->cur = buf_insert_after(b, pos, indent);
    }
    if (b->started_empty && b->line_count == 2 && b->head && b->tail
            && b->head->len == 0 && b->tail->len == 0) {
        b->trailing_newline = 0;
    }
    free(indent);
    vis->cursor_col = (int)indent_len;
    vis->insert_mode = 1;
    vis->replace_mode = 0;
    vis->insert_entry_key = above ? 'O' : 'o';
    vi_record_last_insert_site(b, vis);
    vi_set_insert_anchor(b, vis);
}

static int
vi_prompt_input(buffer_t *b, vi_visual_t *vis, char prefix, char *buf, size_t buf_size)
{
    char **history;
    int *history_len;
    int history_index = -1;
    char scratch[256];
    size_t len = strlen(buf);

    if (prefix == ':') {
        history = vis->cmd_history;
        history_len = &vis->cmd_history_len;
    } else {
        history = vis->search_history;
        history_len = &vis->search_history_len;
    }

    scratch[0] = '\0';

    for (;;) {
        int key;

        vi_render(b, vis, prefix, buf);
        key = vi_read_visual_key(b, vis, prefix, buf);
        if (key == -1 || key == '\x1b') {
            return -1;
        }
        if (key == '\r' || key == '\n') {
            buf[len] = '\0';
            vi_prompt_history_add(history, history_len, buf);
            return 0;
        }
        if (key == VI_KEY_UP || key == 0x10) {
            if (*history_len == 0) {
                write(STDOUT_FILENO, "\a", 1);
                continue;
            }
            if (history_index < 0) {
                snprintf(scratch, sizeof(scratch), "%s", buf);
                history_index = *history_len - 1;
            } else if (history_index > 0) {
                history_index--;
            }
            snprintf(buf, buf_size, "%s", history[history_index]);
            len = strlen(buf);
            continue;
        }
        if (key == VI_KEY_DOWN || key == 0x0e) {
            if (history_index < 0) {
                write(STDOUT_FILENO, "\a", 1);
                continue;
            }
            if (history_index + 1 < *history_len) {
                history_index++;
                snprintf(buf, buf_size, "%s", history[history_index]);
            } else {
                history_index = -1;
                snprintf(buf, buf_size, "%s", scratch);
            }
            len = strlen(buf);
            continue;
        }
        if (key == 127 || key == '\b') {
            history_index = -1;
            if (len > 0) {
                buf[--len] = '\0';
            }
            continue;
        }
        if (key == 0x15) {
            history_index = -1;
            len = 0;
            buf[0] = '\0';
            continue;
        }
        if (key == 0x17) {
            history_index = -1;
            while (len > 0 && isspace((unsigned char)buf[len - 1])) {
                buf[--len] = '\0';
            }
            while (len > 0 && !isspace((unsigned char)buf[len - 1])) {
                buf[--len] = '\0';
            }
            continue;
        }
        if (isprint(key) && len + 1 < buf_size) {
            history_index = -1;
            buf[len++] = (char)key;
            buf[len] = '\0';
        }
    }
}

static void
vi_command_prompt(buffer_t *b, vi_visual_t *vis)
{
    char cmd[256];
    int have_status = 0;

    cmd[0] = '\0';
    if (vi_prompt_input(b, vis, ':', cmd, sizeof(cmd)) != 0) {
        return;
    }
    exvi_execute_command(b, cmd);
    vis->screen_dirty = 1;
    vis->status_dirty = 1;
    if (exvi_take_pending_status(vis->status_msg, sizeof(vis->status_msg))) {
        vis->status_once = 1;
        have_status = 1;
    }
    vi_clamp_cursor(b, vis);
    if (have_status) {
        vi_render(b, vis, ':', NULL);
    }
}

static void
vi_start_change_insert(buffer_t *b, vi_visual_t *vis)
{
    save_undo(b);
    vis->insert_mode = 1;
    vis->replace_mode = 0;
    vis->insert_entry_key = 0;
    vi_record_last_insert_site(b, vis);
    vi_set_insert_anchor(b, vis);
}

static void
vi_finish_repeat_insert(buffer_t *b, vi_visual_t *vis)
{
    if (vis->replace_mode) {
        vi_update_last_insert_text(b, vis);
        vi_record_last_insert_site(b, vis);
        vis->insert_entry_key = 0;
        vi_reset_replace_mode(vis);
    } else {
        vi_update_last_insert_text(b, vis);
        vi_record_last_insert_site(b, vis);
        vis->insert_entry_key = 0;
        vis->insert_mode = 0;
        if (vis->cursor_col > 0) {
            vis->cursor_col--;
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
    case VI_REPEAT_INSERT:
        if (!vis->last_insert_text || vis->last_insert_text[0] == '\0') {
            write(STDOUT_FILENO, "\a", 1);
            return;
        }
        while (count-- > 0) {
            if (vi_begin_repeat_insert(b, vis, vis->last_change_char) != 0) {
                write(STDOUT_FILENO, "\a", 1);
                return;
            }
            vi_replay_insert_text(b, vis, vis->last_insert_text);
            vi_finish_repeat_insert(b, vis);
        }
        break;
    case VI_REPEAT_C_WORD:
        if (!cur) {
            write(STDOUT_FILENO, "\a", 1);
            return;
        }
        if (vi_find_change_word_target(cur, vis->cursor_col, count,
            vis->last_change_char, &end) != 0 || end < vis->cursor_col) {
            write(STDOUT_FILENO, "\a", 1);
            return;
        }
        vi_delete_span(b, vis, vis->cursor_col, end, 1);
        vi_replay_insert_text(b, vis, vis->last_insert_text);
        vi_finish_repeat_insert(b, vis);
        break;
    case VI_REPEAT_S:
        if (cur && (size_t)vis->cursor_col < cur->len) {
            vi_delete_span(b, vis, vis->cursor_col, vis->cursor_col + 1, 1);
        } else {
            save_undo(b);
            vis->insert_mode = 1;
            vis->replace_mode = 0;
            vis->insert_entry_key = 0;
            vi_record_last_insert_site(b, vis);
            vi_set_insert_anchor(b, vis);
        }
        vi_replay_insert_text(b, vis, vis->last_insert_text);
        vi_finish_repeat_insert(b, vis);
        break;
    case VI_REPEAT_C_EOL_CHANGE:
        vi_delete_span(b, vis, vis->cursor_col,
            cur ? (int)cur->len : vis->cursor_col, 1);
        vi_replay_insert_text(b, vis, vis->last_insert_text);
        vi_finish_repeat_insert(b, vis);
        break;
    case VI_REPEAT_S_LINE:
        vi_substitute_line(b, vis, 1);
        vi_replay_insert_text(b, vis, vis->last_insert_text);
        vi_finish_repeat_insert(b, vis);
        break;
    case VI_REPEAT_CC:
        last = vi_clamp_line_target(b, line_no + count - 1);
        vi_linewise_change(b, vis, line_no, last);
        vi_replay_insert_text(b, vis, vis->last_insert_text);
        vi_finish_repeat_insert(b, vis);
        break;
    case VI_REPEAT_C_CHARS:
        if (vis->last_change_char < 0) {
            if (vis->cursor_col >= count) {
                vi_delete_span(b, vis, vis->cursor_col - count, vis->cursor_col, 1);
            } else {
                save_undo(b);
                vis->insert_mode = 1;
                vis->replace_mode = 0;
                vis->insert_entry_key = 0;
                vi_record_last_insert_site(b, vis);
                vi_set_insert_anchor(b, vis);
            }
        } else if (cur && (size_t)vis->cursor_col < cur->len) {
            end = vis->cursor_col + count;
            if (end > (int)cur->len) {
                end = (int)cur->len;
            }
            vi_delete_span(b, vis, vis->cursor_col, end, 1);
        } else {
            save_undo(b);
            vis->insert_mode = 1;
            vis->replace_mode = 0;
            vis->insert_entry_key = 0;
            vi_record_last_insert_site(b, vis);
            vi_set_insert_anchor(b, vis);
        }
        vi_replay_insert_text(b, vis, vis->last_insert_text);
        vi_finish_repeat_insert(b, vis);
        break;
    case VI_REPEAT_C_END:
        if (!cur) {
            write(STDOUT_FILENO, "\a", 1);
            return;
        }
        if (((vis->last_change_char != 0)
                ? vi_find_bigword_end_exclusive_count(cur, vis->cursor_col, count, &end)
                : vi_find_word_end_exclusive_count(cur, vis->cursor_col, count, &end)) != 0 ||
            end <= vis->cursor_col) {
            write(STDOUT_FILENO, "\a", 1);
            return;
        }
        vi_delete_span(b, vis, vis->cursor_col, end, 1);
        vi_replay_insert_text(b, vis, vis->last_insert_text);
        vi_finish_repeat_insert(b, vis);
        break;
    case VI_REPEAT_C_FIND:
        {
            int forward = vis->last_find_forward;
            int till = vis->last_find_till;
            int start;

            if (vis->last_change_char == 'f' || vis->last_change_char == 'F' ||
                vis->last_change_char == 't' || vis->last_change_char == 'T') {
                forward = (vis->last_change_char == 'f' || vis->last_change_char == 't');
                till = (vis->last_change_char == 't' || vis->last_change_char == 'T');
            } else if (vis->last_change_char == ',') {
                forward = !forward;
            }
            if (!cur) {
                write(STDOUT_FILENO, "\a", 1);
                return;
            }
            if (vi_change_find_is_zero_width_insert(cur, vis->cursor_col, vis->last_find_char,
                forward, till, count)) {
                vi_start_change_insert(b, vis);
                vi_replay_insert_text(b, vis, vis->last_insert_text);
                vi_finish_repeat_insert(b, vis);
                break;
            }
            if (vi_find_change_motion_span(cur, vis->cursor_col, vis->last_find_char,
                forward, till, count, &start, &end) != 0) {
                write(STDOUT_FILENO, "\a", 1);
                return;
            }
            vi_delete_span(b, vis, start, end, 1);
            vi_replay_insert_text(b, vis, vis->last_insert_text);
            vi_finish_repeat_insert(b, vis);
        }
        break;
    case VI_REPEAT_C_BACK_END:
        {
            line_t *target_line;
            int target_col;

            if (vi_backward_end_motion_target(b, vis, count, vis->last_change_char,
                &target_line, &target_col) != 0 || target_line != b->cur ||
                target_col > vis->cursor_col) {
                write(STDOUT_FILENO, "\a", 1);
                return;
            }
            if (target_col == vis->cursor_col) {
                vi_start_change_insert(b, vis);
            } else {
                vi_delete_span(b, vis, target_col, vis->cursor_col + 1, 1);
            }
            vi_replay_insert_text(b, vis, vis->last_insert_text);
            vi_finish_repeat_insert(b, vis);
        }
        break;
    case VI_REPEAT_C_MOTION:
        {
            int saved_pending_op = vis->pending_op;
            int saved_pending_op_count = vis->pending_op_count;
            int saved_pending_count = vis->pending_count;
            int saved_pending_reg = vis->pending_reg;

            vis->pending_op = 'c';
            vis->pending_op_count = 1;
            vis->pending_count = count;
            vis->pending_reg = 0;
            vi_handle_pending_operator(b, vis, vis->last_change_char);
            vis->pending_op = saved_pending_op;
            vis->pending_op_count = saved_pending_op_count;
            vis->pending_count = saved_pending_count;
            vis->pending_reg = saved_pending_reg;
            if (!vis->insert_mode) {
                return;
            }
            vi_replay_insert_text(b, vis, vis->last_insert_text);
            vi_finish_repeat_insert(b, vis);
        }
        break;
    case VI_REPEAT_C_TO_COL0:
    case VI_REPEAT_C_TO_FIRST_NONBLANK:
        if (!cur) {
            write(STDOUT_FILENO, "\a", 1);
            return;
        }
        end = (vis->last_change == VI_REPEAT_C_TO_FIRST_NONBLANK)
            ? vi_first_nonblank_col(cur) : 0;
        if (end < vis->cursor_col) {
            vi_delete_span(b, vis, end, vis->cursor_col, 1);
        } else {
            save_undo(b);
            vis->cursor_col = end;
            vis->insert_mode = 1;
            vis->replace_mode = 0;
            vis->insert_entry_key = 0;
            vi_record_last_insert_site(b, vis);
            vi_set_insert_anchor(b, vis);
        }
        vi_replay_insert_text(b, vis, vis->last_insert_text);
        vi_finish_repeat_insert(b, vis);
        break;
    case VI_REPEAT_C_SEARCH_REPEAT:
        {
            int current_line_no = buf_current_line(b);
            int saved_op = vis->pending_op;
            int forward;
            line_t *target_line;
            int target_col;

            if (vis->last_change_char == 'n') {
                forward = vis->last_search_forward;
            } else if (vis->last_change_char == 'N') {
                forward = !vis->last_search_forward;
            } else if (vis->last_change_char == '*') {
                forward = 1;
            } else if (vis->last_change_char == '#') {
                forward = 0;
            } else if (vis->last_change_char == '/') {
                forward = 1;
            } else if (vis->last_change_char == '?') {
                forward = 0;
            } else {
                write(STDOUT_FILENO, "\a", 1);
                return;
            }
            vis->pending_op = 'c';
            if (vi_search_motion_target(b, vis, "", forward, count,
                &target_line, &target_col) != 0) {
                vis->pending_op = saved_op;
                write(STDOUT_FILENO, "\a", 1);
                return;
            }
            if (vi_apply_search_linewise_motion(b, vis, current_line_no, target_line,
                target_col) != 0) {
                if (target_line == b->cur && target_col == vis->cursor_col) {
                    vi_start_change_insert(b, vis);
                } else if (vi_apply_charwise_motion(b, vis, target_line, target_col, 0) != 0) {
                    vis->pending_op = saved_op;
                    write(STDOUT_FILENO, "\a", 1);
                    return;
                }
            }
            vis->pending_op = saved_op;
            vi_replay_insert_text(b, vis, vis->last_insert_text);
            vi_finish_repeat_insert(b, vis);
        }
        break;
    case VI_REPEAT_C_PERCENT:
        {
            int saved_op = vis->pending_op;
            line_t *target_line;
            int target_col;
            int scan_start_col;

            if (!b->cur) {
                write(STDOUT_FILENO, "\a", 1);
                return;
            }
            vis->pending_op = 'c';
            if (vi_find_scanned_cross_bracket_span(b->cur, vis->cursor_col, &scan_start_col,
                &target_line, &target_col) == 0) {
                vi_delete_range(b, vis, b->cur, scan_start_col, target_line,
                    target_col + 1, 1);
            } else if (vi_match_operator_target(b, vis, &target_line, &target_col) != 0 ||
                !target_line || vi_apply_charwise_motion(b, vis, target_line, target_col, 1) != 0) {
                vis->pending_op = saved_op;
                write(STDOUT_FILENO, "\a", 1);
                return;
            }
            vis->pending_op = saved_op;
            vi_replay_insert_text(b, vis, vis->last_insert_text);
            vi_finish_repeat_insert(b, vis);
        }
        break;
    case VI_REPEAT_C_MARK_LINE:
        {
            int mark_idx = vis->last_change_char - 'a';
            int mark_line;
            int start;
            int saved_op = vis->pending_op;

            if (vis->last_change_char < 'a' || vis->last_change_char > 'z' ||
                !b->marks[mark_idx]) {
                write(STDOUT_FILENO, "\a", 1);
                return;
            }
            mark_line = vi_line_number_for_mark(b, b->marks[mark_idx]);
            if (mark_line < 1 || mark_line == line_no) {
                write(STDOUT_FILENO, "\a", 1);
                return;
            }
            if (mark_line < line_no) {
                start = mark_line;
                last = line_no;
            } else {
                start = line_no;
                last = mark_line;
            }
            vis->pending_op = 'c';
            if (last < start || vi_apply_linewise_operator(b, vis, start, last) != 0) {
                vis->pending_op = saved_op;
                write(STDOUT_FILENO, "\a", 1);
                return;
            }
            vis->pending_op = saved_op;
            vi_replay_insert_text(b, vis, vis->last_insert_text);
            vi_finish_repeat_insert(b, vis);
        }
        break;
    case VI_REPEAT_C_MARK_EXACT:
        {
            int mark_idx = vis->last_change_char - 'a';

            if (vis->last_change_char < 'a' || vis->last_change_char > 'z' ||
                !b->marks[mark_idx] ||
                vi_apply_charwise_motion(b, vis, b->marks[mark_idx],
                    b->mark_cols[mark_idx], 0) != 0) {
                write(STDOUT_FILENO, "\a", 1);
                return;
            }
            vi_replay_insert_text(b, vis, vis->last_insert_text);
            vi_finish_repeat_insert(b, vis);
        }
        break;
    case VI_REPEAT_C_LINE_MOTION:
        {
            int saved_op = vis->pending_op;
            int start = line_no;
            int target = vi_clamp_line_target(b, line_no + vis->last_change_aux);

            if (start > target) {
                int tmp = start;

                start = target;
                target = tmp;
            }
            vis->pending_op = 'c';
            if (vi_apply_linewise_operator(b, vis, start, target) != 0) {
                vis->pending_op = saved_op;
                write(STDOUT_FILENO, "\a", 1);
                return;
            }
            vis->pending_op = saved_op;
            vi_replay_insert_text(b, vis, vis->last_insert_text);
            vi_finish_repeat_insert(b, vis);
        }
        break;
    default:
        write(STDOUT_FILENO, "\a", 1);
        break;
    }
}

static void
vi_apply_search(buffer_t *b, vi_visual_t *vis, const char *pattern, int forward,
    int count)
{
    line_t *target_line;
    int target_col;

    if (vi_search_target(b, vis, pattern, forward, count, &target_line, &target_col) != 0) {
        write(STDOUT_FILENO, "\a", 1);
        return;
    }
    b->cur = target_line;
    vis->cursor_col = target_col;
    vi_set_status(vis, NULL);
}

static void
vi_repeat_search(buffer_t *b, vi_visual_t *vis, int forward, int count)
{
    line_t *target_line;
    int target_col;

    if (vi_search_target(b, vis, "", forward, count, &target_line, &target_col) != 0) {
        write(STDOUT_FILENO, "\a", 1);
        return;
    }
    b->cur = target_line;
    vis->cursor_col = target_col;
    vi_set_status(vis, NULL);
}

static void
vi_search_prompt(buffer_t *b, vi_visual_t *vis, int forward)
{
    char pattern[256];
    char prefix = forward ? '/' : '?';

    pattern[0] = '\0';
    if (vi_prompt_input(b, vis, prefix, pattern, sizeof(pattern)) == 0) {
        vi_apply_search(b, vis, pattern, forward, 1);
    }
}

static void
vi_copy_current_word(buffer_t *b, vi_visual_t *vis, int escape_regex,
    char **out_text)
{
    line_t *cur = b->cur;
    int start;
    int end;
    int pos;
    size_t len;
    const char *meta = ".^$*+?()[{\\|";
    char *text;
    size_t out_len = 0;

    *out_text = NULL;
    if (!cur || cur->len == 0) {
        return;
    }
    pos = vis->cursor_col;
    if (pos >= (int)cur->len) {
        pos = (int)cur->len - 1;
    }
    if (pos < 0 || !vi_is_word_char((unsigned char)cur->text[pos])) {
        return;
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
        if (escape_regex && strchr(meta, cur->text[start + (int)i])) {
            out_len++;
        }
        out_len++;
    }
    text = malloc(out_len + 1);
    if (!text) {
        return;
    }
    out_len = 0;
    for (size_t i = 0; i < len; i++) {
        char ch = cur->text[start + (int)i];

        if (escape_regex && strchr(meta, ch)) {
            text[out_len++] = '\\';
        }
        text[out_len++] = ch;
    }
    text[out_len] = '\0';
    *out_text = text;
}

static char *
vi_current_word_text(buffer_t *b, vi_visual_t *vis)
{
    char *text;

    vi_copy_current_word(b, vis, 0, &text);
    return text;
}

static char *
vi_current_word_pattern(buffer_t *b, vi_visual_t *vis)
{
    char *word;
    char *pattern;
    size_t len;

    vi_copy_current_word(b, vis, 1, &word);
    if (!word) {
        return NULL;
    }
    len = strlen(word);
    pattern = malloc(len + 5);
    if (!pattern) {
        free(word);
        return NULL;
    }
    snprintf(pattern, len + 5, "\\<%s\\>", word);
    free(word);
    return pattern;
}

static void
vi_visual_apply_tag_target(buffer_t *b, char *cmd)
{
    char *ptr = cmd;
    int addr = -1;

    while (*ptr && isspace((unsigned char)*ptr)) {
        ptr++;
    }
    if (!*ptr) {
        return;
    }

    addr = parse_address(b, &ptr);
    while (*ptr && isspace((unsigned char)*ptr)) {
        ptr++;
    }
    if (*ptr == '\0' && addr > 0) {
        b->cur = buf_get_line(b, addr);
        return;
    }

    exvi_execute_command(b, cmd);
}

static void
vi_search_current_word(buffer_t *b, vi_visual_t *vis, int forward, int count)
{
    char *pattern = vi_current_word_pattern(b, vis);

    if (!pattern) {
        write(STDOUT_FILENO, "\a", 1);
        return;
    }
    vi_apply_search(b, vis, pattern, forward, count);
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
    int resume_insert_mode = 0;
    int resume_replace_mode = 0;

    memset(&vis, 0, sizeof(vis));
    vis.top_line = 1;
    vis.last_search_forward = 1;
    vis.screen_dirty = 1;
    vis.status_dirty = 1;
    vis.clear_screen = 1;
    vis.rendered_cur_line = -1;
    vi_ensure_visible_line(b);
    if (vi_enable_raw(&vis) != 0) {
        return 1;
    }
    if (exvi_take_pending_status(vis.status_msg, sizeof(vis.status_msg))) {
        vis.status_once = 1;
    }
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = vi_handle_winch;
    sigemptyset(&sa.sa_mask);
    if (sigaction(SIGWINCH, &sa, &old_winch) == 0) {
        have_winch = 1;
    }
    vi_resize_pending = 0;

    for (;;) {
        line_t *cur;
        int clear_status = 0;

        vi_ensure_visible_line(b);
        if (vis.status_once && vis.status_msg[0] != '\0') {
            clear_status = 1;
        }
        vi_render(b, &vis, ':', NULL);
        key = vi_read_visual_key(b, &vis, ':', NULL);
        if (clear_status) {
            vi_set_status(&vis, NULL);
        }
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
                if (vi_current_insert_span_nonempty(b, &vis) ||
                    vis.replace_edit_count > 0) {
                    vi_update_last_insert_text(b, &vis);
                    if (vis.insert_entry_key != 0) {
                        vi_set_last_change(&vis, VI_REPEAT_INSERT, 1,
                            vis.insert_entry_key);
                    }
                }
                vi_record_last_insert_site(b, &vis);
                vis.insert_entry_key = 0;
                vi_reset_replace_mode(&vis);
                if (vis.cursor_col > 0) {
                    vis.cursor_col--;
                }
                break;
            case 0x0f:
                resume_replace_mode = 1;
                resume_insert_mode = 0;
                vis.replace_mode = 0;
                key = vi_read_visual_key(b, &vis, ':', NULL);
                if (key == -1) {
                    goto done;
                }
                goto process_normal_key;
            case '\r':
            case '\n':
                vi_split_line(b, &vis);
                break;
            case 127:
            case '\b':
                if (vi_undo_replace_edit(b, &vis) != 0) {
                    write(STDOUT_FILENO, "\a", 1);
                }
                break;
            case 0x17:
            case VI_KEY_CTRL_BACKSPACE:
                vi_erase_word_backward_insert(b, &vis);
                break;
            case VI_KEY_CTRL_DELETE:
                vi_erase_word_forward_insert(b, &vis);
                break;
            case 0x15:
                vi_erase_to_insert_anchor(b, &vis);
                break;
            case 0x12:
                vi_insert_register_text(b, &vis);
                break;
            case 0x16:
                vi_insert_quoted_key(b, &vis);
                break;
            case 0x01:
                vi_replay_insert_text(b, &vis, vis.last_insert_text);
                break;
            case 0x14:
                vi_reindent_current_line(b, &vis, 1);
                break;
            case 0x19:
                vi_insert_adjacent_char(b, &vis, 0);
                break;
            case 0x05:
                vi_insert_adjacent_char(b, &vis, 1);
                break;
            case 0x04:
                vi_reindent_current_line(b, &vis, 0);
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
                if (vi_current_insert_span_nonempty(b, &vis)) {
                    vi_update_last_insert_text(b, &vis);
                    if (vis.insert_entry_key != 0) {
                        vi_set_last_change(&vis, VI_REPEAT_INSERT, 1,
                            vis.insert_entry_key);
                    }
                }
                vi_record_last_insert_site(b, &vis);
                vis.insert_entry_key = 0;
                vis.insert_mode = 0;
                if (vis.cursor_col > 0) {
                    vis.cursor_col--;
                }
                break;
            case 0x0f:
                resume_insert_mode = 1;
                resume_replace_mode = 0;
                vis.insert_mode = 0;
                key = vi_read_visual_key(b, &vis, ':', NULL);
                if (key == -1) {
                    goto done;
                }
                goto process_normal_key;
            case '\r':
            case '\n':
                vi_split_line(b, &vis);
                break;
            case 127:
            case '\b':
                vi_backspace_char(b, &vis);
                break;
            case 0x17:
            case VI_KEY_CTRL_BACKSPACE:
                vi_erase_word_backward_insert(b, &vis);
                break;
            case VI_KEY_CTRL_DELETE:
                vi_erase_word_forward_insert(b, &vis);
                break;
            case 0x15:
                vi_erase_to_insert_anchor(b, &vis);
                break;
            case 0x12:
                vi_insert_register_text(b, &vis);
                break;
            case 0x16:
                vi_insert_quoted_key(b, &vis);
                break;
            case 0x01:
                vi_replay_insert_text(b, &vis, vis.last_insert_text);
                break;
            case 0x14:
                vi_reindent_current_line(b, &vis, 1);
                break;
            case 0x19:
                vi_insert_adjacent_char(b, &vis, 0);
                break;
            case 0x05:
                vi_insert_adjacent_char(b, &vis, 1);
                break;
            case 0x04:
                vi_reindent_current_line(b, &vis, 0);
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
process_normal_key:
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
                goto maybe_restore_insert_mode;
            }
        if (key == '0' && vis.pending_count > 0) {
            vi_append_count(&vis, 0);
            goto maybe_restore_insert_mode;
        }
        vi_handle_pending_operator(b, &vis, key);
            goto maybe_restore_insert_mode;
        }
        if (vis.pending_z) {
            int count = vis.pending_count;

            vis.pending_count = 0;

            vis.pending_z = 0;
            switch (key) {
            case 'z':
                vi_reposition_target(b, &vis,
                    count > 0 ? count : buf_current_line(b), 0, 1);
                break;
            case '.':
                vi_reposition_target(b, &vis,
                    count > 0 ? count : buf_current_line(b), 0, 0);
                break;
            case '\r':
            case '\n':
                vi_reposition_target(b, &vis,
                    count > 0 ? count : buf_current_line(b), 1, 0);
                break;
            case 't':
                vi_reposition_target(b, &vis,
                    count > 0 ? count : buf_current_line(b), 1, 1);
                break;
            case '-':
                vi_reposition_target(b, &vis,
                    count > 0 ? count : buf_current_line(b), 2, 0);
                break;
            case 'b':
                vi_reposition_target(b, &vis,
                    count > 0 ? count : buf_current_line(b), 2, 1);
                break;
            case '+':
                if (count > 0) {
                    vi_reposition_target(b, &vis, count, 1, 0);
                } else {
                    vi_reposition_target(b, &vis,
                        vis.top_line + (vis.rows - 1), 1, 0);
                }
                break;
            case '^':
                if (count > 0) {
                    int visible_rows = vis.rows - 1;
                    int target_line = count - visible_rows + 1;

                    vi_reposition_target(b, &vis, target_line, 2, 0);
                } else {
                    vi_reposition_target(b, &vis, vis.top_line - 1, 2, 0);
                }
                break;
            default:
                write(STDOUT_FILENO, "\a", 1);
                break;
            }
            goto maybe_restore_insert_mode;
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
                    if (!exvi_write_allowed(b, b->filename, 0)) {
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
            goto maybe_restore_insert_mode;
        }

        if (key >= '1' && key <= '9') {
            vis.pending_g = 0;
            vi_append_count(&vis, key - '0');
            goto maybe_restore_insert_mode;
        }

        vis.screen_dirty = 1;
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
            vis.screen_dirty = 0;
            {
                int count = vi_take_count(&vis);

                while (count-- > 0 && vis.cursor_col > 0) {
                    vis.cursor_col--;
                }
            }
            break;
        case 'l':
            vis.pending_g = 0;
            vis.screen_dirty = 0;
            {
                int count = vi_take_count(&vis);

                while (count-- > 0 && cur && vis.cursor_col < (int)cur->len) {
                    vis.cursor_col++;
                }
            }
            break;
        case 'j':
            vis.pending_g = 0;
            vis.screen_dirty = 0;
            vi_move_vertical(b, vi_take_count(&vis));
            break;
        case 'k':
            vis.pending_g = 0;
            vis.screen_dirty = 0;
            vi_move_vertical(b, -vi_take_count(&vis));
            break;
        case ')':
            vis.pending_g = 0;
            vis.screen_dirty = 0;
            vi_move_sentence_forward(b, &vis, vi_take_count(&vis));
            break;
        case '(':
            vis.pending_g = 0;
            vis.screen_dirty = 0;
            vi_move_sentence_backward(b, &vis, vi_take_count(&vis));
            break;
        case '}':
            vis.pending_g = 0;
            vis.screen_dirty = 0;
            vi_move_paragraph_forward(b, &vis, vi_take_count(&vis));
            break;
        case '{':
            vis.pending_g = 0;
            vis.screen_dirty = 0;
            vi_move_paragraph_backward(b, &vis, vi_take_count(&vis));
            break;
        case '[':
        case ']':
            {
                int count = vi_take_count(&vis);
                int key2;

                vis.pending_g = 0;
                vis.screen_dirty = 0;
                key2 = vi_read_visual_key(b, &vis, ':', NULL);
                if (key2 == -1 || key2 == '\x1b') {
                    write(STDOUT_FILENO, "\a", 1);
                    vis.screen_dirty = 1;
                    break;
                }
                if (key == '[' && key2 == '[') {
                    vi_move_section_boundary(b, &vis, 0, 0, count);
                } else if (key == ']' && key2 == ']') {
                    vi_move_section_boundary(b, &vis, 1, 0, count);
                } else if (key == '[' && key2 == ']') {
                    vi_move_section_boundary(b, &vis, 0, 1, count);
                } else if (key == ']' && key2 == '[') {
                    vi_move_section_boundary(b, &vis, 1, 1, count);
                } else {
                    vis.screen_dirty = 1;
                    write(STDOUT_FILENO, "\a", 1);
                }
            }
            break;
        case '+':
        case '\r':
        case '\n':
            vis.pending_g = 0;
            vis.screen_dirty = 0;
            vi_move_line_first_nonblank(b, &vis, vi_take_count(&vis));
            break;
        case '-':
            vis.pending_g = 0;
            vis.screen_dirty = 0;
            vi_move_line_first_nonblank(b, &vis, -vi_take_count(&vis));
            break;
        case 'w':
            vis.pending_g = 0;
            vis.screen_dirty = 0;
            vi_move_word_forward_count(b, &vis, vi_take_count(&vis));
            break;
        case 'b':
            vis.pending_g = 0;
            vis.screen_dirty = 0;
            vi_move_word_backward_count(b, &vis, vi_take_count(&vis));
            break;
        case 'e':
            vis.screen_dirty = 0;
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
            vis.screen_dirty = 0;
            vi_move_bigword_forward_count(b, &vis, vi_take_count(&vis));
            break;
        case 'B':
            vis.pending_g = 0;
            vis.screen_dirty = 0;
            vi_move_bigword_backward_count(b, &vis, vi_take_count(&vis));
            break;
        case 'E':
            vis.screen_dirty = 0;
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
            vis.screen_dirty = 0;
            key = vi_read_visual_key(b, &vis, ':', NULL);
            if (key == -1 || key == '\x1b' || key == '\r' || key == '\n') {
                vis.screen_dirty = 1;
                write(STDOUT_FILENO, "\a", 1);
                break;
            }
            vi_find_char_motion(b, &vis, key, 1, 0, vi_take_count(&vis));
            break;
        case 'F':
            vis.pending_g = 0;
            vis.screen_dirty = 0;
            key = vi_read_visual_key(b, &vis, ':', NULL);
            if (key == -1 || key == '\x1b' || key == '\r' || key == '\n') {
                vis.screen_dirty = 1;
                write(STDOUT_FILENO, "\a", 1);
                break;
            }
            vi_find_char_motion(b, &vis, key, 0, 0, vi_take_count(&vis));
            break;
        case 't':
            vis.pending_g = 0;
            vis.screen_dirty = 0;
            key = vi_read_visual_key(b, &vis, ':', NULL);
            if (key == -1 || key == '\x1b' || key == '\r' || key == '\n') {
                vis.screen_dirty = 1;
                write(STDOUT_FILENO, "\a", 1);
                break;
            }
            vi_find_char_motion(b, &vis, key, 1, 1, vi_take_count(&vis));
            break;
        case 'T':
            vis.pending_g = 0;
            vis.screen_dirty = 0;
            key = vi_read_visual_key(b, &vis, ':', NULL);
            if (key == -1 || key == '\x1b' || key == '\r' || key == '\n') {
                vis.screen_dirty = 1;
                write(STDOUT_FILENO, "\a", 1);
                break;
            }
            vi_find_char_motion(b, &vis, key, 0, 1, vi_take_count(&vis));
            break;
        case ';':
            vis.pending_g = 0;
            vis.screen_dirty = 0;
            vi_repeat_find_motion(b, &vis, 0, vi_take_count(&vis));
            break;
        case ',':
            vis.pending_g = 0;
            vis.screen_dirty = 0;
            vi_repeat_find_motion(b, &vis, 1, vi_take_count(&vis));
            break;
        case '0':
            if (vis.pending_count > 0) {
                vi_append_count(&vis, 0);
            } else {
                vis.pending_g = 0;
                vis.screen_dirty = 0;
                vis.cursor_col = 0;
            }
            break;
        case '^':
            vis.pending_g = 0;
            vis.screen_dirty = 0;
            vi_take_count(&vis);
            vis.cursor_col = vi_first_nonblank_col(cur);
            break;
        case '_':
            vis.screen_dirty = 0;
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
            vis.screen_dirty = 0;
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
            vis.screen_dirty = 0;
            vi_move_to_eol_count(b, &vis, vi_take_count(&vis));
            break;
        case '%':
            vis.pending_g = 0;
            vis.screen_dirty = 0;
            if (vis.pending_count > 0) {
                vi_move_to_percent(b, &vis, vi_take_count(&vis));
            } else {
                vi_match_motion(b, &vis);
            }
            break;
        case 'i':
            if (vis.pending_g) {
                vis.pending_g = 0;
                vis.pending_count = 0;
                if (vis.last_insert_line_no > 0 && b->line_count > 0) {
                    b->cur = buf_get_line(b,
                        vi_clamp_line_target(b, vis.last_insert_line_no));
                    if (b->cur) {
                        if (vis.last_insert_col < 0) {
                            vis.cursor_col = 0;
                        } else if ((size_t)vis.last_insert_col > b->cur->len) {
                            vis.cursor_col = (int)b->cur->len;
                        } else {
                            vis.cursor_col = vis.last_insert_col;
                        }
                    } else {
                        vis.cursor_col = 0;
                    }
                    save_undo(b);
                    vis.insert_mode = 1;
                    vis.replace_mode = 0;
                    vis.insert_entry_key = 0;
                    vi_record_last_insert_site(b, &vis);
                    vi_set_insert_anchor(b, &vis);
                } else {
                    write(STDOUT_FILENO, "\a", 1);
                }
                break;
            }
            vis.pending_g = 0;
            save_undo(b);
            vis.insert_mode = 1;
            vis.insert_entry_key = 'i';
            vi_record_last_insert_site(b, &vis);
            vi_set_insert_anchor(b, &vis);
            break;
        case 'I':
            if (vis.pending_g) {
                vis.pending_g = 0;
                vi_take_count(&vis);
                vis.cursor_col = 0;
                save_undo(b);
                vis.insert_mode = 1;
                vis.replace_mode = 0;
                vis.insert_entry_key = 0;
                vi_record_last_insert_site(b, &vis);
                vi_set_insert_anchor(b, &vis);
                break;
            }
            vis.pending_g = 0;
            vis.cursor_col = vi_first_nonblank_col(cur);
            save_undo(b);
            vis.insert_mode = 1;
            vis.insert_entry_key = 'I';
            vi_record_last_insert_site(b, &vis);
            vi_set_insert_anchor(b, &vis);
            break;
        case 'a':
            vis.pending_g = 0;
            if (cur && vis.cursor_col < (int)cur->len) {
                vis.cursor_col++;
            }
            save_undo(b);
            vis.insert_mode = 1;
            vis.insert_entry_key = 'a';
            vi_record_last_insert_site(b, &vis);
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
            vis.insert_entry_key = 'A';
            vi_record_last_insert_site(b, &vis);
            vi_set_insert_anchor(b, &vis);
            break;
        case 'R':
            vis.pending_g = 0;
            save_undo(b);
            vis.insert_mode = 0;
            vi_clear_replace_edits(&vis);
            vis.replace_mode = 1;
            vis.insert_entry_key = 'R';
            vi_record_last_insert_site(b, &vis);
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
                vis.replace_mode = 0;
                vis.insert_entry_key = 0;
                vi_record_last_insert_site(b, &vis);
                vi_set_insert_anchor(b, &vis);
            }
            vi_set_last_change(&vis, VI_REPEAT_S, 1, 0);
            break;
        case 'S':
            vis.pending_g = 0;
            vi_substitute_line(b, &vis, 1);
            vi_set_last_change(&vis, VI_REPEAT_S_LINE, 1, 0);
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
            vis.pending_op_count = vi_take_count(&vis);
            break;
        case 'd':
            vis.pending_g = 0;
            vis.pending_op = 'd';
            vis.pending_op_count = vi_take_count(&vis);
            break;
        case 'y':
            vis.pending_g = 0;
            vis.pending_op = 'y';
            vis.pending_op_count = vi_take_count(&vis);
            break;
        case '>':
            vis.pending_g = 0;
            vis.pending_op = '>';
            vis.pending_op_count = vi_take_count(&vis);
            break;
        case '<':
            vis.pending_g = 0;
            vis.pending_op = '<';
            vis.pending_op_count = vi_take_count(&vis);
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

                vis.screen_dirty = 0;
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
            vis.screen_dirty = 0;
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
                vis.screen_dirty = 1;
                write(STDOUT_FILENO, "\a", 1);
            }
            break;
        case 0x1d:
            vis.pending_g = 0;
            vis.pending_count = 0;
            {
                char *tag = vi_current_word_text(b, &vis);

                if (!tag) {
                    write(STDOUT_FILENO, "\a", 1);
                    break;
                }
                handle_tag_command(b, tag, vi_visual_apply_tag_target);
                free(tag);
                if (b->cur) {
                    vis.cursor_col = vi_first_nonblank_col(b->cur);
                } else {
                    vis.cursor_col = 0;
                }
            }
            break;
        case 0x14:
            vis.pending_g = 0;
            vis.pending_count = 0;
            handle_pop_command(b, 0);
            if (b->cur) {
                vis.cursor_col = vi_first_nonblank_col(b->cur);
            } else {
                vis.cursor_col = 0;
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
        case 'Q':
            vis.pending_g = 0;
            vis.pending_count = 0;
            goto ex_mode;
        case 'G':
            vis.pending_g = 0;
            vis.screen_dirty = 0;
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
            vis.screen_dirty = 0;
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
            vis.screen_dirty = 0;
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
            vis.screen_dirty = 0;
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
            vis.screen_dirty = 0;
            vi_command_prompt(b, &vis);
            break;
        case '/':
            vis.pending_g = 0;
            vis.pending_count = 0;
            vis.screen_dirty = 0;
            vi_search_prompt(b, &vis, 1);
            break;
        case '?':
            vis.pending_g = 0;
            vis.pending_count = 0;
            vis.screen_dirty = 0;
            vi_search_prompt(b, &vis, 0);
            break;
        case '*':
            vis.pending_g = 0;
            vis.screen_dirty = 0;
            vi_search_current_word(b, &vis, 1, vi_take_count(&vis));
            break;
        case '#':
            vis.pending_g = 0;
            vis.screen_dirty = 0;
            vi_search_current_word(b, &vis, 0, vi_take_count(&vis));
            break;
        case 'n':
            vis.pending_g = 0;
            vis.screen_dirty = 0;
            vi_repeat_search(b, &vis, vis.last_search_forward, vi_take_count(&vis));
            break;
        case 'N':
            vis.pending_g = 0;
            vis.screen_dirty = 0;
            vi_repeat_search(b, &vis, !vis.last_search_forward, vi_take_count(&vis));
            break;
        case '\f':
            vis.pending_g = 0;
            vis.pending_count = 0;
            vis.screen_dirty = 1;
            vis.clear_screen = 1;
            break;
        case 0x02:
            vis.pending_g = 0;
            vis.screen_dirty = 0;
            vi_page_scroll(b, &vis, -vi_take_count(&vis));
            break;
        case 0x04:
            vis.pending_g = 0;
            vis.screen_dirty = 0;
            vi_half_page_scroll(b, &vis, vi_take_count(&vis));
            break;
        case 0x05:
            vis.pending_g = 0;
            vis.screen_dirty = 0;
            {
                int count = vi_take_count(&vis);

                while (count-- > 0) {
                    vi_line_scroll(b, &vis, 1);
                }
            }
            break;
        case 0x06:
            vis.pending_g = 0;
            vis.screen_dirty = 0;
            vi_page_scroll(b, &vis, vi_take_count(&vis));
            break;
        case 0x15:
            vis.pending_g = 0;
            vis.screen_dirty = 0;
            vi_half_page_scroll(b, &vis, -vi_take_count(&vis));
            break;
        case 0x19:
            vis.pending_g = 0;
            vis.screen_dirty = 0;
            {
                int count = vi_take_count(&vis);

                while (count-- > 0) {
                    vi_line_scroll(b, &vis, -1);
                }
            }
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
            vi_set_last_change(&vis, VI_REPEAT_C_EOL_CHANGE, 1, 0);
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
maybe_restore_insert_mode:
        if (resume_insert_mode && !vis.insert_mode && !vis.replace_mode &&
            !vis.pending_op && !vis.pending_z && !vis.pending_big_z && !vis.pending_g) {
            vis.insert_mode = 1;
            vi_set_insert_anchor(b, &vis);
            resume_insert_mode = 0;
        } else if (resume_replace_mode && !vis.insert_mode && !vis.replace_mode &&
            !vis.pending_op && !vis.pending_z && !vis.pending_big_z && !vis.pending_g) {
            vis.replace_mode = 1;
            vi_set_insert_anchor(b, &vis);
            resume_replace_mode = 0;
        }
        if (vis.pending_reg && key != '"' && key != 'd' && key != 'y' && key != 'c') {
            vis.pending_reg = 0;
        }
    }

done:
    if (have_winch) {
        sigaction(SIGWINCH, &old_winch, NULL);
    }
    free(vis.last_insert_text);
    vi_clear_replace_edits(&vis);
    vi_prompt_history_free(vis.cmd_history, vis.cmd_history_len);
    vi_prompt_history_free(vis.search_history, vis.search_history_len);
    vi_restore_terminal();
    return 0;

ex_mode:
    if (have_winch) {
        sigaction(SIGWINCH, &old_winch, NULL);
    }
    vi_clear_replace_edits(&vis);
    vi_prompt_history_free(vis.cmd_history, vis.cmd_history_len);
    vi_prompt_history_free(vis.search_history, vis.search_history_len);
    vi_restore_terminal();
    return EXVI_EXIT_EX_HANDOFF;
}
