#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <errno.h>
#include <sys/select.h>
#include <dirent.h>
#include <sys/stat.h>
#include "el.h"

/* Forward declarations for functions used before their definitions */
static size_t get_terminal_columns(EditLine *el);
static void refresh_line(EditLine *el);

static int is_word_char(unsigned char ch) {
    return isalnum(ch) || ch == '_';
}

static int read_esc_byte(EditLine *el, char *out, int timeout_ms) {
    fd_set rfds;
    struct timeval tv;
    int fd;
    int ret;

    if (!el || !out) return 0;
    fd = fileno(el->fin);

    FD_ZERO(&rfds);
    FD_SET(fd, &rfds);
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;

    ret = select(fd + 1, &rfds, NULL, NULL, &tv);
    if (ret <= 0) return 0;
    if (read(fd, out, 1) != 1) return 0;
    return 1;
}

static void kill_ring_push(EditLine *el, const char *text, size_t len, int append) {
    size_t idx;
    char *copy;

    if (!el || !text || len == 0) return;

    if (append && el->last_cmd_was_kill && el->kill_ring_count > 0) {
        char *old = el->kill_ring[el->kill_ring_head];
        size_t old_len = old ? strlen(old) : 0;
        char *merged = realloc(old, old_len + len + 1);
        if (!merged) return;
        memcpy(merged + old_len, text, len);
        merged[old_len + len] = '\0';
        el->kill_ring[el->kill_ring_head] = merged;
        return;
    }

    copy = malloc(len + 1);
    if (!copy) return;
    memcpy(copy, text, len);
    copy[len] = '\0';

    idx = (el->kill_ring_count == 0) ? 0 : ((el->kill_ring_head + 1) % EL_KILL_RING_SIZE);
    if (el->kill_ring_count == EL_KILL_RING_SIZE) {
        free(el->kill_ring[idx]);
    } else {
        el->kill_ring_count++;
    }
    el->kill_ring[idx] = copy;
    el->kill_ring_head = idx;
}

static void delete_range(EditLine *el, size_t start, size_t end) {
    if (!el) return;
    if (start >= end || end > el->line.len) return;

    memmove(el->line.buffer + start,
            el->line.buffer + end,
            el->line.len - end + 1);
    el->line.len -= (end - start);
    if (el->line.cursor > end) {
        el->line.cursor -= (end - start);
    } else if (el->line.cursor > start) {
        el->line.cursor = start;
    }
}

static int kill_line(EditLine *el) {
    size_t end;
    size_t kill_len;

    if (!el || el->line.cursor >= el->line.len) return 0;
    end = el->line.len;
    kill_len = end - el->line.cursor;
    kill_ring_push(el, el->line.buffer + el->line.cursor, kill_len, 1);
    delete_range(el, el->line.cursor, end);
    el->last_cmd_was_kill = 1;
    return 1;
}

static int backward_kill_line(EditLine *el) {
    size_t kill_len;

    if (!el || el->line.cursor == 0) return 0;
    kill_len = el->line.cursor;
    kill_ring_push(el, el->line.buffer, kill_len, 1);
    delete_range(el, 0, el->line.cursor);
    el->line.cursor = 0;
    el->last_cmd_was_kill = 1;
    return 1;
}

static int kill_word(EditLine *el) {
    size_t i;
    size_t start;
    size_t end;

    if (!el || el->line.cursor >= el->line.len) return 0;

    i = el->line.cursor;
    if (!is_word_char((unsigned char)el->line.buffer[i])) {
        while (i < el->line.len && !is_word_char((unsigned char)el->line.buffer[i])) i++;
    }
    while (i < el->line.len && is_word_char((unsigned char)el->line.buffer[i])) i++;

    start = el->line.cursor;
    end = i;
    if (end <= start) return 0;

    kill_ring_push(el, el->line.buffer + start, end - start, 1);
    delete_range(el, start, end);
    el->last_cmd_was_kill = 1;
    return 1;
}

static int backward_kill_word(EditLine *el) {
    size_t i;
    size_t start;
    size_t end;

    if (!el || el->line.cursor == 0) return 0;

    i = el->line.cursor;
    while (i > 0 && !is_word_char((unsigned char)el->line.buffer[i - 1])) i--;
    while (i > 0 && is_word_char((unsigned char)el->line.buffer[i - 1])) i--;

    start = i;
    end = el->line.cursor;
    if (end <= start) return 0;

    kill_ring_push(el, el->line.buffer + start, end - start, 1);
    delete_range(el, start, end);
    el->line.cursor = start;
    el->last_cmd_was_kill = 1;
    return 1;
}

static void undo_push(EditLine *el) {
    struct undo_entry entry;
    size_t i;

    if (!el) return;
    entry.buffer = malloc(el->line.len + 1);
    if (!entry.buffer) return;
    memcpy(entry.buffer, el->line.buffer, el->line.len + 1);
    entry.len = el->line.len;
    entry.cursor = el->line.cursor;

    if (el->undo_depth == EL_UNDO_DEPTH) {
        free(el->undo_stack[0].buffer);
        for (i = 1; i < el->undo_depth; i++) {
            el->undo_stack[i - 1] = el->undo_stack[i];
        }
        el->undo_depth--;
    }
    el->undo_stack[el->undo_depth++] = entry;
}

static void undo_discard_last(EditLine *el) {
    if (!el || el->undo_depth == 0) return;
    free(el->undo_stack[el->undo_depth - 1].buffer);
    el->undo_stack[el->undo_depth - 1].buffer = NULL;
    el->undo_depth--;
}

static int undo_pop(EditLine *el) {
    struct undo_entry entry;

    if (!el || el->undo_depth == 0) return 0;

    entry = el->undo_stack[--el->undo_depth];
    if (line_ensure_capacity(el, entry.len + 1) == -1) {
        free(entry.buffer);
        return 0;
    }
    memcpy(el->line.buffer, entry.buffer, entry.len + 1);
    el->line.len = entry.len;
    el->line.cursor = (entry.cursor <= entry.len) ? entry.cursor : entry.len;
    free(entry.buffer);
    return 1;
}

static size_t kill_ring_prev_index(EditLine *el, size_t start_idx) {
    size_t idx;
    size_t i;

    if (!el || el->kill_ring_count == 0) return start_idx;
    idx = start_idx;
    for (i = 0; i < EL_KILL_RING_SIZE; i++) {
        idx = (idx + EL_KILL_RING_SIZE - 1) % EL_KILL_RING_SIZE;
        if (el->kill_ring[idx]) return idx;
    }
    return start_idx;
}

static int yank_from_kill_ring(EditLine *el) {
    const char *text;
    size_t len;
    size_t start;

    if (!el || el->kill_ring_count == 0 || !el->kill_ring[el->kill_ring_head]) return 0;
    text = el->kill_ring[el->kill_ring_head];
    len = strlen(text);
    start = el->line.cursor;
    if (line_ensure_capacity(el, el->line.len + len + 1) == -1) return 0;

    memmove(el->line.buffer + start + len,
            el->line.buffer + start,
            el->line.len - start + 1);
    memcpy(el->line.buffer + start, text, len);
    el->line.len += len;
    el->line.cursor += len;
    el->yank_active = 1;
    el->yank_start = start;
    el->yank_len = len;
    el->yank_ring_index = el->kill_ring_head;
    return 1;
}

static int yank_pop(EditLine *el) {
    size_t next_idx;
    const char *text;
    size_t len;

    if (!el || !el->yank_active || el->kill_ring_count < 2) return 0;

    next_idx = kill_ring_prev_index(el, el->yank_ring_index);
    if (next_idx == el->yank_ring_index || !el->kill_ring[next_idx]) return 0;

    text = el->kill_ring[next_idx];
    len = strlen(text);

    delete_range(el, el->yank_start, el->yank_start + el->yank_len);
    if (line_ensure_capacity(el, el->line.len + len + 1) == -1) return 0;
    memmove(el->line.buffer + el->yank_start + len,
            el->line.buffer + el->yank_start,
            el->line.len - el->yank_start + 1);
    memcpy(el->line.buffer + el->yank_start, text, len);
    el->line.len += len;
    el->line.cursor = el->yank_start + len;
    el->yank_len = len;
    el->yank_ring_index = next_idx;
    return 1;
}

static int transpose_chars(EditLine *el) {
    size_t left;
    char tmp;

    if (!el || el->line.len < 2 || el->line.cursor == 0) return 0;

    left = (el->line.cursor == el->line.len) ? el->line.cursor - 2 : el->line.cursor - 1;
    tmp = el->line.buffer[left];
    el->line.buffer[left] = el->line.buffer[left + 1];
    el->line.buffer[left + 1] = tmp;
    if (el->line.cursor < el->line.len) el->line.cursor++;
    return 1;
}

static void move_forward_word(EditLine *el) {
    size_t i;

    if (!el) return;
    i = el->line.cursor;
    while (i < el->line.len && !is_word_char((unsigned char)el->line.buffer[i])) i++;
    while (i < el->line.len && is_word_char((unsigned char)el->line.buffer[i])) i++;
    el->line.cursor = i;
}

static void move_backward_word(EditLine *el) {
    size_t i;

    if (!el) return;
    i = el->line.cursor;
    while (i > 0 && !is_word_char((unsigned char)el->line.buffer[i - 1])) i--;
    while (i > 0 && is_word_char((unsigned char)el->line.buffer[i - 1])) i--;
    el->line.cursor = i;
}

static int apply_word_case(EditLine *el, int mode) {
    size_t i;
    size_t start;

    if (!el) return 0;
    i = el->line.cursor;
    while (i < el->line.len && !is_word_char((unsigned char)el->line.buffer[i])) i++;
    if (i >= el->line.len) return 0;
    start = i;

    while (i < el->line.len && is_word_char((unsigned char)el->line.buffer[i])) {
        unsigned char ch = (unsigned char)el->line.buffer[i];
        if (mode == 0) {
            el->line.buffer[i] = (char)((i == start) ? toupper(ch) : tolower(ch));
        } else if (mode > 0) {
            el->line.buffer[i] = (char)toupper(ch);
        } else {
            el->line.buffer[i] = (char)tolower(ch);
        }
        i++;
    }
    el->line.cursor = i;
    return 1;
}

static int yank_last_arg(EditLine *el) {
    HistEvent ev;
    const char *s;
    size_t len;
    size_t start;
    size_t end;

    if (!el || !el->history) return 0;
    if (history(el->history, &ev, H_LAST) != 0 || !ev.str) return 0;

    s = ev.str;
    len = strlen(s);
    while (len > 0 && isspace((unsigned char)s[len - 1])) len--;
    if (len == 0) return 0;
    end = len;
    while (len > 0 && !isspace((unsigned char)s[len - 1])) len--;
    start = len;
    if (end <= start) return 0;

    if (line_ensure_capacity(el, el->line.len + (end - start) + 1) == -1) return 0;
    memmove(el->line.buffer + el->line.cursor + (end - start),
            el->line.buffer + el->line.cursor,
            el->line.len - el->line.cursor + 1);
    memcpy(el->line.buffer + el->line.cursor, s + start, end - start);
    el->line.len += (end - start);
    el->line.cursor += (end - start);
    return 1;
}

static int insert_span(EditLine *el, const char *text, size_t len) {
    if (!el || !text || len == 0) return 0;
    if (line_ensure_capacity(el, el->line.len + len + 1) == -1) return 0;
    memmove(el->line.buffer + el->line.cursor + len,
            el->line.buffer + el->line.cursor,
            el->line.len - el->line.cursor + 1);
    memcpy(el->line.buffer + el->line.cursor, text, len);
    el->line.len += len;
    el->line.cursor += len;
    return 1;
}

static int default_filename_complete(EditLine *el) {
    size_t word_start;
    size_t word_len;
    char *word;
    char *expanded;
    char *slash;
    const char *dir_path;
    const char *base;
    size_t base_len;
    DIR *dir;
    struct dirent *de;
    char *first_match;
    size_t common_len;
    int match_count;

    if (!el) return 0;

    word_start = el->line.cursor;
    while (word_start > 0 && !isspace((unsigned char)el->line.buffer[word_start - 1])) {
        word_start--;
    }
    word_len = el->line.cursor - word_start;

    word = malloc(word_len + 1);
    if (!word) return 0;
    memcpy(word, el->line.buffer + word_start, word_len);
    word[word_len] = '\0';

    /* Tilde expansion: ~/ → $HOME/ */
    expanded = NULL;
    if (word[0] == '~' && (word[1] == '/' || word[1] == '\0')) {
        const char *home = getenv("HOME");
        if (home) {
            size_t hlen = strlen(home);
            size_t rest = strlen(word + 1);
            expanded = malloc(hlen + rest + 1);
            if (expanded) {
                memcpy(expanded, home, hlen);
                memcpy(expanded + hlen, word + 1, rest + 1);
                free(word);
                word = expanded;
                word_len = hlen + rest;
            }
        }
    }

    slash = strrchr(word, '/');
    if (slash) {
        *slash = '\0';
        dir_path = (*word == '\0') ? "/" : word;
        base = slash + 1;
    } else {
        dir_path = ".";
        base = word;
    }
    base_len = strlen(base);

    dir = opendir(dir_path);
    if (!dir) {
        free(word);
        return 0;
    }

    first_match = NULL;
    common_len = 0;
    match_count = 0;
    while ((de = readdir(dir)) != NULL) {
        size_t nlen = strlen(de->d_name);
        size_t i;
        if (strncmp(de->d_name, base, base_len) != 0) continue;

        if (match_count == 0) {
            first_match = strdup(de->d_name);
            if (!first_match) break;
            common_len = strlen(first_match);
        } else {
            for (i = 0; i < common_len && de->d_name[i] != '\0' && first_match[i] == de->d_name[i]; i++) {
            }
            common_len = i;
        }
        match_count++;
        if (nlen == 0) continue;
    }
    closedir(dir);

    if (match_count == 0 || !first_match) {
        free(first_match);
        free(word);
        return 0;
    }

    if (match_count == 1) {
        char full_path[4096];
        struct stat st;
        size_t flen = strlen(first_match);

        if (flen > base_len) {
            (void)insert_span(el, first_match + base_len, flen - base_len);
        }

        if (slash) {
            snprintf(full_path, sizeof(full_path), "%s/%s", dir_path, first_match);
        } else {
            snprintf(full_path, sizeof(full_path), "%s", first_match);
        }
        if (stat(full_path, &st) == 0 && S_ISDIR(st.st_mode)) {
            (void)insert_span(el, "/", 1);
        } else {
            (void)insert_span(el, " ", 1);
        }
    } else if (common_len > base_len) {
        (void)insert_span(el, first_match + base_len, common_len - base_len);
    } else if (el->last_action_was_complete) {
        /* Second tab with no common prefix: display alternatives in columns */
        size_t cols = get_terminal_columns(el);
        size_t max_name = 0;
        size_t ncols, nrows, r, cc;
        char **names;
        int ni;

        /* Collect matching names */
        names = malloc((size_t)match_count * sizeof(char *));
        if (!names) { free(first_match); free(word); return 0; }
        ni = 0;
        dir = opendir(dir_path);
        if (dir) {
            while ((de = readdir(dir)) != NULL && ni < match_count) {
                if (strncmp(de->d_name, base, base_len) != 0) continue;
                names[ni] = strdup(de->d_name);
                if (names[ni]) {
                    size_t nl = strlen(names[ni]);
                    if (nl > max_name) max_name = nl;
                    ni++;
                }
            }
            closedir(dir);
        }

        /* Display in columns */
        if (ni > 0 && max_name > 0) {
            max_name += 2; /* padding */
            ncols = cols / max_name;
            if (ncols == 0) ncols = 1;
            nrows = ((size_t)ni + ncols - 1) / ncols;

            terminal_puts(el, "\r\n");
            for (r = 0; r < nrows; r++) {
                for (cc = 0; cc < ncols; cc++) {
                    size_t idx = cc * nrows + r;
                    if (idx < (size_t)ni) {
                        size_t nl = strlen(names[idx]);
                        size_t pad;
                        terminal_write(el, names[idx], nl);
                        if (cc + 1 < ncols) {
                            for (pad = nl; pad < max_name; pad++)
                                terminal_putc(el, ' ');
                        }
                    }
                }
                terminal_puts(el, "\r\n");
            }
            terminal_flush(el);
            el->refresh_rows = 1;
            refresh_line(el);
        }

        {
            int j;
            for (j = 0; j < ni; j++) free(names[j]);
        }
        free(names);
    } else {
        free(first_match);
        free(word);
        return 0;
    }

    free(first_match);
    free(word);
    return 1;
}

static size_t get_terminal_columns(EditLine *el) {
    if (el && el->term.cols > 0)
        return (size_t)el->term.cols;
    return 80;
}

static size_t line_dirty_from_cache(EditLine *el) {
    size_t i;
    size_t limit;

    if (!el || !el->render_cache) return 0;

    limit = (el->render_cache_len < el->line.len) ? el->render_cache_len : el->line.len;
    for (i = 0; i < limit; i++) {
        if (el->render_cache[i] != el->line.buffer[i]) return i;
    }
    return limit;
}

static void line_update_cache(EditLine *el) {
    char *new_cache;

    if (!el) return;

    if (el->render_cache_cap < el->line.len + 1) {
        size_t needed = el->line.len + 1;
        new_cache = realloc(el->render_cache, needed);
        if (!new_cache) return;
        el->render_cache = new_cache;
        el->render_cache_cap = needed;
    }
    if (el->line.len > 0) memcpy(el->render_cache, el->line.buffer, el->line.len);
    el->render_cache[el->line.len] = '\0';
    el->render_cache_len = el->line.len;
    el->render_cache_cursor = el->line.cursor;
}

static void refresh_line(EditLine *el) {
    size_t cols;
    size_t prompt_len;
    size_t total_len;
    size_t cursor_total;
    size_t end_row;
    size_t rows;
    size_t cursor_row;
    size_t cursor_col;
    size_t dirty_from;
    size_t i;

    if (!el || !el->fout) return;

    cols = get_terminal_columns(el);
    if (cols == 0) cols = 80;

    prompt_len = el->prompt ? strlen(el->prompt) : 0;
    total_len = prompt_len + el->line.len;
    cursor_total = prompt_len + el->line.cursor;
    end_row = total_len / cols;
    rows = end_row + 1;
    cursor_row = cursor_total / cols;
    cursor_col = cursor_total % cols;
    dirty_from = line_dirty_from_cache(el);

    /*
     * Dirty-region fast path for single-row refreshes.
     * Keep full redraw logic for wrapped output.
     */
    if (el->refresh_rows <= 1 && rows <= 1 &&
        dirty_from < el->line.len &&
        el->render_cache_cursor <= cols &&
        prompt_len <= cols) {
        size_t target_col = prompt_len + dirty_from;
        terminal_putc(el, '\r');
        if (target_col > 0) terminal_printf(el, "\033[%zuC", target_col);
        terminal_write(el, el->line.buffer + dirty_from, el->line.len - dirty_from);
        if (el->render_cache_len > el->line.len) terminal_puts(el, "\033[K");
        terminal_putc(el, '\r');
        if (cursor_total > 0) terminal_printf(el, "\033[%zuC", cursor_total);
        terminal_flush(el);
        el->refresh_rows = 1;
        line_update_cache(el);
        return;
    }

    /* Return to first physical row of the previous render block. */
    terminal_putc(el, '\r');
    if (el->refresh_rows > 1) terminal_printf(el, "\033[%zuA", el->refresh_rows - 1);
    terminal_putc(el, '\r');

    /* Clear the old wrapped rows. */
    for (i = 0; i < el->refresh_rows; i++) {
        terminal_puts(el, "\033[2K");
        if (i + 1 < el->refresh_rows) terminal_putc(el, '\n');
    }
    if (el->refresh_rows > 1) terminal_printf(el, "\033[%zuA", el->refresh_rows - 1);
    terminal_putc(el, '\r');

    if (el->prompt) terminal_write(el, el->prompt, prompt_len);
    if (el->line.len > 0) terminal_write(el, el->line.buffer, el->line.len);

    /* Reposition cursor from render end to logical cursor location. */
    if (end_row > cursor_row) terminal_printf(el, "\033[%zuA", end_row - cursor_row);
    terminal_putc(el, '\r');
    if (cursor_col > 0) terminal_printf(el, "\033[%zuC", cursor_col);

    terminal_flush(el);
    el->refresh_rows = rows;
    line_update_cache(el);
}

static void line_load_history(EditLine *el, const char *text) {
    size_t len;

    if (!el || !text) return;

    len = strlen(text);
    if (line_ensure_capacity(el, len + 1) == -1) return;

    memcpy(el->line.buffer, text, len + 1);
    el->line.len = len;
    el->line.cursor = len;
}

static int line_set_text(EditLine *el, const char *text) {
    size_t len;

    if (!el || !text) return -1;

    len = strlen(text);
    if (line_ensure_capacity(el, len + 1) == -1) return -1;
    memcpy(el->line.buffer, text, len + 1);
    el->line.len = len;
    el->line.cursor = len;
    return 0;
}

static int history_save_current_input(EditLine *el) {
    char *new_saved;
    size_t needed;

    if (!el) return -1;
    needed = el->line.len + 1;
    if (el->saved_input_cap < needed) {
        new_saved = realloc(el->saved_input, needed);
        if (!new_saved) return -1;
        el->saved_input = new_saved;
        el->saved_input_cap = needed;
    }
    memcpy(el->saved_input, el->line.buffer, needed);
    return 0;
}

static void history_restore_current_input(EditLine *el) {
    if (!el || !el->saved_input) {
        el_reset(el);
        return;
    }
    (void)line_set_text(el, el->saved_input);
}

static const char *history_find_match(EditLine *el, const char *query, int reverse) {
    HistEvent ev;
    const char *last;

    if (!el || !el->history || !query || !*query) return NULL;

    if (reverse) {
        if (history(el->history, &ev, H_LAST) != 0 || !ev.str) return NULL;
        if (strstr(ev.str, query)) return ev.str;
        last = ev.str;
        while (history(el->history, &ev, H_PREV) == 0 && ev.str && ev.str != last) {
            if (strstr(ev.str, query)) return ev.str;
            last = ev.str;
        }
        return NULL;
    }

    if (history(el->history, &ev, H_FIRST) != 0 || !ev.str) return NULL;
    do {
        if (strstr(ev.str, query)) return ev.str;
    } while (history(el->history, &ev, H_NEXT) == 0 && ev.str);

    return NULL;
}

static void history_incremental_search(EditLine *el, int reverse) {
    char query[256];
    size_t qlen;
    const char *match;
    int ch;

    if (!el || !el->history) return;

    query[0] = '\0';
    qlen = 0;
    match = NULL;

    for (;;) {
        terminal_printf(el, "\r\033[K(%s-i-search)`%s': %s",
                reverse ? "reverse" : "forward",
                query,
                match ? match : "");
        terminal_flush(el);

        if (read(fileno(el->fin), &ch, 1) != 1) break;
        ch &= 0xFF;

        if (ch == '\n' || ch == '\r') {
            if (match) {
                line_load_history(el, match);
                el->history_browsing = 1;
            }
            break;
        }
        if (ch == 0x1B || ch == 0x07) break;
        if (ch == 0x08 || ch == 0x7F) {
            if (qlen > 0) query[--qlen] = '\0';
        } else if (ch >= 32 && ch < 127) {
            if (qlen + 1 < sizeof(query)) {
                query[qlen++] = (char)ch;
                query[qlen] = '\0';
            }
        }

        match = (qlen > 0) ? history_find_match(el, query, reverse) : NULL;
    }

    refresh_line(el);
}

/* ------------------------------------------------------------------ */
/* Vi-specific word motion helpers.                                    */
/* Vi "word" = alnum/_ chars; "WORD" = non-blank chars.               */
/* ------------------------------------------------------------------ */

static int is_vi_word(unsigned char ch) {
    return isalnum(ch) || ch == '_';
}

static size_t vi_forward_word(EditLine *el, size_t pos) {
    size_t i = pos;
    if (i >= el->line.len) return i;
    if (is_vi_word((unsigned char)el->line.buffer[i])) {
        while (i < el->line.len && is_vi_word((unsigned char)el->line.buffer[i])) i++;
    } else if (!isspace((unsigned char)el->line.buffer[i])) {
        while (i < el->line.len && !is_vi_word((unsigned char)el->line.buffer[i])
               && !isspace((unsigned char)el->line.buffer[i])) i++;
    }
    while (i < el->line.len && isspace((unsigned char)el->line.buffer[i])) i++;
    return i;
}

static size_t vi_forward_WORD(EditLine *el, size_t pos) {
    size_t i = pos;
    while (i < el->line.len && !isspace((unsigned char)el->line.buffer[i])) i++;
    while (i < el->line.len && isspace((unsigned char)el->line.buffer[i])) i++;
    return i;
}

static size_t vi_backward_word(EditLine *el, size_t pos) {
    size_t i = pos;
    if (i == 0) return 0;
    i--;
    while (i > 0 && isspace((unsigned char)el->line.buffer[i])) i--;
    if (is_vi_word((unsigned char)el->line.buffer[i])) {
        while (i > 0 && is_vi_word((unsigned char)el->line.buffer[i - 1])) i--;
    } else {
        while (i > 0 && !is_vi_word((unsigned char)el->line.buffer[i - 1])
               && !isspace((unsigned char)el->line.buffer[i - 1])) i--;
    }
    return i;
}

static size_t vi_backward_WORD(EditLine *el, size_t pos) {
    size_t i = pos;
    if (i == 0) return 0;
    i--;
    while (i > 0 && isspace((unsigned char)el->line.buffer[i])) i--;
    while (i > 0 && !isspace((unsigned char)el->line.buffer[i - 1])) i--;
    return i;
}

static size_t vi_end_word(EditLine *el, size_t pos) {
    size_t i = pos;
    if (i + 1 >= el->line.len) return el->line.len > 0 ? el->line.len - 1 : 0;
    i++;
    while (i < el->line.len && isspace((unsigned char)el->line.buffer[i])) i++;
    if (i < el->line.len && is_vi_word((unsigned char)el->line.buffer[i])) {
        while (i + 1 < el->line.len && is_vi_word((unsigned char)el->line.buffer[i + 1])) i++;
    } else {
        while (i + 1 < el->line.len && !is_vi_word((unsigned char)el->line.buffer[i + 1])
               && !isspace((unsigned char)el->line.buffer[i + 1])) i++;
    }
    return i;
}

static size_t vi_end_WORD(EditLine *el, size_t pos) {
    size_t i = pos;
    if (i + 1 >= el->line.len) return el->line.len > 0 ? el->line.len - 1 : 0;
    i++;
    while (i < el->line.len && isspace((unsigned char)el->line.buffer[i])) i++;
    while (i + 1 < el->line.len && !isspace((unsigned char)el->line.buffer[i + 1])) i++;
    return i;
}

static size_t vi_find_char_fwd(EditLine *el, char ch, size_t pos) {
    size_t i;
    for (i = pos + 1; i < el->line.len; i++) {
        if (el->line.buffer[i] == ch) return i;
    }
    return pos; /* not found */
}

static size_t vi_find_char_bwd(EditLine *el, char ch, size_t pos) {
    size_t i;
    if (pos == 0) return 0;
    for (i = pos - 1; ; i--) {
        if (el->line.buffer[i] == ch) return i;
        if (i == 0) break;
    }
    return pos; /* not found */
}

static size_t vi_first_nonblank(EditLine *el) {
    size_t i;
    for (i = 0; i < el->line.len; i++) {
        if (!isspace((unsigned char)el->line.buffer[i])) return i;
    }
    return el->line.len;
}

/*
 * Compute the target position for a vi motion character.
 * Returns the resulting cursor position, or (size_t)-1 if invalid.
 * For delete/change/yank, the range is [min(cursor, target), max(cursor, target)).
 */
static size_t vi_motion_target(EditLine *el, int motion, int count) {
    size_t pos = el->line.cursor;
    int i;

    switch (motion) {
    case 'h':
        return pos > (size_t)count ? pos - count : 0;
    case 'l':
        return (pos + count < el->line.len) ? pos + count : el->line.len;
    case 'w':
        for (i = 0; i < count; i++) pos = vi_forward_word(el, pos);
        return pos;
    case 'W':
        for (i = 0; i < count; i++) pos = vi_forward_WORD(el, pos);
        return pos;
    case 'b':
        for (i = 0; i < count; i++) pos = vi_backward_word(el, pos);
        return pos;
    case 'B':
        for (i = 0; i < count; i++) pos = vi_backward_WORD(el, pos);
        return pos;
    case 'e':
        for (i = 0; i < count; i++) pos = vi_end_word(el, pos);
        return pos < el->line.len ? pos + 1 : el->line.len;
    case 'E':
        for (i = 0; i < count; i++) pos = vi_end_WORD(el, pos);
        return pos < el->line.len ? pos + 1 : el->line.len;
    case '0':
        return 0;
    case '$':
        return el->line.len;
    case '^':
        return vi_first_nonblank(el);
    case 'f': case 'F': case 't': case 'T': {
        char fc;
        if (!read_esc_byte(el, &fc, 80)) return (size_t)-1;
        el->vi_find.ch = fc;
        el->vi_find.forward = (motion == 'f' || motion == 't');
        el->vi_find.till = (motion == 't' || motion == 'T');
        size_t target;
        for (i = 0; i < count; i++) {
            if (el->vi_find.forward)
                target = vi_find_char_fwd(el, fc, i == 0 ? pos : target);
            else
                target = vi_find_char_bwd(el, fc, i == 0 ? pos : target);
        }
        if (target == pos) return (size_t)-1;
        if (el->vi_find.till) {
            if (el->vi_find.forward && target > 0) target--;
            else if (!el->vi_find.forward) target++;
        }
        /* For forward motions used with d/c/y, include the target char */
        if (el->vi_find.forward) target++;
        return target;
    }
    default:
        return (size_t)-1;
    }
}

/*
 * Record the last edit command for Vi dot-repeat.
 */
static void vi_record_repeat(EditLine *el, char cmd, char arg, int count) {
    el->vi_repeat.cmd = cmd;
    el->vi_repeat.arg = arg;
    el->vi_repeat.count = count;
    free(el->vi_repeat.insert_text);
    el->vi_repeat.insert_text = NULL;
    el->vi_repeat.insert_len = 0;
}

/*
 * Capture inserted text for dot-repeat (from vi_insert_start to cursor).
 */
static void vi_capture_insert(EditLine *el) {
    size_t start = el->vi_insert_start;
    size_t end = el->line.cursor;
    size_t len;

    free(el->vi_repeat.insert_text);
    el->vi_repeat.insert_text = NULL;
    el->vi_repeat.insert_len = 0;

    if (end <= start) return;
    len = end - start;
    el->vi_repeat.insert_text = malloc(len + 1);
    if (!el->vi_repeat.insert_text) return;
    memcpy(el->vi_repeat.insert_text, el->line.buffer + start, len);
    el->vi_repeat.insert_text[len] = '\0';
    el->vi_repeat.insert_len = len;
}

static void vi_enter_insert(EditLine *el) {
    el->vi_mode = VI_INSERT;
    el->vi_insert_start = el->line.cursor;
}

/* ------------------------------------------------------------------ */
/* Vi history search: reads /pattern or ?pattern from user.           */
/* ------------------------------------------------------------------ */
static void vi_history_search(EditLine *el, int reverse) {
    char query[256];
    size_t qlen = 0;
    const char *match;
    int ch;

    query[0] = '\0';

    for (;;) {
        terminal_printf(el, "\r\033[K%c%s",
                reverse ? '/' : '?', query);
        terminal_flush(el);

        if (read(fileno(el->fin), &ch, 1) != 1) break;
        ch &= 0xFF;

        if (ch == '\n' || ch == '\r') {
            if (qlen > 0) {
                memcpy(el->vi_search.pattern, query, qlen + 1);
                el->vi_search.reverse = reverse;
            }
            match = history_find_match(el, el->vi_search.pattern, el->vi_search.reverse);
            if (match) {
                line_load_history(el, match);
                el->history_browsing = 1;
            }
            break;
        }
        if (ch == 0x1B || ch == 0x03) break; /* ESC or ^C cancels */
        if (ch == 0x08 || ch == 0x7F) {
            if (qlen > 0) query[--qlen] = '\0';
        } else if (ch >= 32 && ch < 127 && qlen + 1 < sizeof(query)) {
            query[qlen++] = (char)ch;
            query[qlen] = '\0';
        }
    }
    refresh_line(el);
}

/* ================================================================== */
/* Action functions for keymap dispatch                                */
/*                                                                    */
/* Each function has signature: unsigned char fn(EditLine *, int)      */
/* Returns CC_NORM, CC_NEWLINE, CC_EOF, CC_REFRESH, or CC_ERROR       */
/* ================================================================== */

/* -- Editor-wide actions (ed_*) -- */

static unsigned char ed_newline(EditLine *el, int c) {
    (void)c;
    el->last_cmd_was_kill = 0;
    el->yank_active = 0;
    if (el->editor_mode == ED_VI) {
        vi_capture_insert(el);
        el->vi_mode = VI_INSERT;
    }
    return CC_NEWLINE;
}

static unsigned char ed_insert(EditLine *el, int c) {
    if (c < 32 || c >= 127) return CC_NORM;
    if (line_ensure_capacity(el, el->line.len + 2) != 0) return CC_ERROR;
    el->last_cmd_was_kill = 0;
    el->yank_active = 0;
    undo_push(el);
    if (el->overwrite_mode && el->line.cursor < el->line.len) {
        el->line.buffer[el->line.cursor] = (char)c;
        el->line.cursor++;
    } else {
        memmove(el->line.buffer + el->line.cursor + 1,
                el->line.buffer + el->line.cursor,
                el->line.len - el->line.cursor + 1);
        el->line.buffer[el->line.cursor] = (char)c;
        el->line.len++;
        el->line.cursor++;
    }
    refresh_line(el);
    return CC_NORM;
}

static unsigned char ed_delete_next_char(EditLine *el, int c) {
    (void)c;
    el->last_cmd_was_kill = 0;
    el->yank_active = 0;
    if (el->line.len == 0) return CC_EOF;
    if (el->line.cursor < el->line.len) {
        undo_push(el);
        memmove(el->line.buffer + el->line.cursor,
                el->line.buffer + el->line.cursor + 1,
                el->line.len - el->line.cursor);
        el->line.len--;
        refresh_line(el);
    } else if (el->completion) {
        el->completion(el, c);
        refresh_line(el);
    }
    return CC_NORM;
}

static unsigned char ed_tty_sigint(EditLine *el, int c) {
    (void)c;
    el->last_cmd_was_kill = 0;
    el->yank_active = 0;
    terminal_puts(el, "^C\r\n");
    el_reset(el);
    if (el->prompt) {
        terminal_puts(el, el->prompt);
        terminal_flush(el);
    }
    return CC_NORM;
}

static unsigned char ed_clear_screen(EditLine *el, int c) {
    (void)c;
    el->last_cmd_was_kill = 0;
    el->yank_active = 0;
    terminal_puts(el, "\033[H\033[2J");
    refresh_line(el);
    return CC_NORM;
}

static unsigned char ed_move_to_beg(EditLine *el, int c) {
    (void)c;
    el->last_cmd_was_kill = 0;
    el->yank_active = 0;
    if (el->line.cursor != 0) {
        undo_push(el);
        el->line.cursor = 0;
        refresh_line(el);
    }
    return CC_NORM;
}

static unsigned char ed_prev_char(EditLine *el, int c) {
    (void)c;
    el->last_cmd_was_kill = 0;
    el->yank_active = 0;
    if (el->line.cursor > 0) {
        undo_push(el);
        el->line.cursor--;
        refresh_line(el);
    }
    return CC_NORM;
}

static unsigned char ed_move_to_end(EditLine *el, int c) {
    (void)c;
    el->last_cmd_was_kill = 0;
    el->yank_active = 0;
    if (el->line.cursor != el->line.len) {
        undo_push(el);
        el->line.cursor = el->line.len;
        refresh_line(el);
    }
    return CC_NORM;
}

static unsigned char ed_next_char(EditLine *el, int c) {
    (void)c;
    el->last_cmd_was_kill = 0;
    el->yank_active = 0;
    if (el->line.cursor < el->line.len) {
        undo_push(el);
        el->line.cursor++;
        refresh_line(el);
    }
    return CC_NORM;
}

static unsigned char ed_complete(EditLine *el, int c) {
    el->last_cmd_was_kill = 0;
    el->yank_active = 0;
    if (el->completion) {
        undo_push(el);
        el->completion(el, c);
        refresh_line(el);
    } else {
        undo_push(el);
        if (default_filename_complete(el)) {
            refresh_line(el);
        } else {
            undo_discard_last(el);
            terminal_putc(el, '\a');
            terminal_flush(el);
        }
    }
    el->last_action_was_complete = 1;
    return CC_NORM;
}

static unsigned char ed_kill_line(EditLine *el, int c) {
    (void)c;
    el->yank_active = 0;
    undo_push(el);
    if (kill_line(el)) refresh_line(el);
    return CC_NORM;
}

static unsigned char ed_next_history(EditLine *el, int c) {
    HistEvent ev;
    (void)c;
    el->last_cmd_was_kill = 0;
    el->yank_active = 0;
    if (!el->history) return CC_NORM;
    if (history(el->history, &ev, H_NEXT) == 0 && ev.str) {
        undo_push(el);
        line_load_history(el, ev.str);
    } else {
        undo_push(el);
        history_restore_current_input(el);
        el->history_browsing = 0;
    }
    refresh_line(el);
    return CC_NORM;
}

static unsigned char ed_prev_history(EditLine *el, int c) {
    HistEvent ev;
    (void)c;
    el->last_cmd_was_kill = 0;
    el->yank_active = 0;
    if (!el->history) return CC_NORM;
    if (!el->history_browsing) {
        (void)history_save_current_input(el);
        el->history_browsing = 1;
    }
    if (history(el->history, &ev, H_PREV) == 0 && ev.str) {
        undo_push(el);
        line_load_history(el, ev.str);
        refresh_line(el);
    }
    return CC_NORM;
}

static unsigned char ed_transpose_chars(EditLine *el, int c) {
    (void)c;
    el->last_cmd_was_kill = 0;
    el->yank_active = 0;
    undo_push(el);
    if (transpose_chars(el)) refresh_line(el);
    return CC_NORM;
}

static unsigned char ed_undo(EditLine *el, int c) {
    (void)c;
    el->last_cmd_was_kill = 0;
    el->yank_active = 0;
    if (undo_pop(el)) refresh_line(el);
    return CC_NORM;
}

static unsigned char ed_toggle_overwrite(EditLine *el, int c) {
    (void)c;
    el->overwrite_mode = !el->overwrite_mode;
    return CC_NORM;
}

static unsigned char ed_beginning_of_history(EditLine *el, int c) {
    HistEvent ev;
    (void)c;
    el->last_cmd_was_kill = 0;
    el->yank_active = 0;
    if (!el->history) return CC_NORM;
    if (!el->history_browsing) {
        (void)history_save_current_input(el);
        el->history_browsing = 1;
    }
    if (history(el->history, &ev, H_FIRST) == 0 && ev.str) {
        line_load_history(el, ev.str);
        refresh_line(el);
    }
    return CC_NORM;
}

static unsigned char em_end_of_history(EditLine *el, int c) {
    HistEvent ev;
    (void)c;
    el->last_cmd_was_kill = 0;
    el->yank_active = 0;
    if (!el->history) return CC_NORM;
    if (!el->history_browsing) {
        (void)history_save_current_input(el);
        el->history_browsing = 1;
    }
    if (history(el->history, &ev, H_LAST) == 0 && ev.str) {
        line_load_history(el, ev.str);
    } else {
        history_restore_current_input(el);
        el->history_browsing = 0;
    }
    refresh_line(el);
    return CC_NORM;
}

/* -- Emacs-specific actions (em_*) -- */

static unsigned char em_delete_prev_char(EditLine *el, int c) {
    (void)c;
    el->last_cmd_was_kill = 0;
    el->yank_active = 0;
    if (el->line.cursor > 0) {
        undo_push(el);
        memmove(el->line.buffer + el->line.cursor - 1,
                el->line.buffer + el->line.cursor,
                el->line.len - el->line.cursor + 1);
        el->line.len--;
        el->line.cursor--;
        refresh_line(el);
    }
    return CC_NORM;
}

static unsigned char em_kill_region(EditLine *el, int c) {
    (void)c;
    el->yank_active = 0;
    undo_push(el);
    if (backward_kill_line(el)) refresh_line(el);
    return CC_NORM;
}

static unsigned char em_delete_prev_word(EditLine *el, int c) {
    (void)c;
    el->yank_active = 0;
    undo_push(el);
    if (backward_kill_word(el)) refresh_line(el);
    return CC_NORM;
}

static unsigned char em_yank(EditLine *el, int c) {
    (void)c;
    el->last_cmd_was_kill = 0;
    undo_push(el);
    if (yank_from_kill_ring(el)) refresh_line(el);
    return CC_NORM;
}

static unsigned char em_search_prev(EditLine *el, int c) {
    (void)c;
    el->last_cmd_was_kill = 0;
    el->yank_active = 0;
    history_incremental_search(el, 1);
    return CC_NORM;
}

static unsigned char em_search_next(EditLine *el, int c) {
    (void)c;
    el->last_cmd_was_kill = 0;
    el->yank_active = 0;
    history_incremental_search(el, 0);
    return CC_NORM;
}

static unsigned char em_capitalize_word(EditLine *el, int c) {
    (void)c;
    el->last_cmd_was_kill = 0;
    el->yank_active = 0;
    undo_push(el);
    if (apply_word_case(el, 0)) refresh_line(el);
    else undo_discard_last(el);
    return CC_NORM;
}

static unsigned char em_lower_case_word(EditLine *el, int c) {
    (void)c;
    el->last_cmd_was_kill = 0;
    el->yank_active = 0;
    undo_push(el);
    if (apply_word_case(el, -1)) refresh_line(el);
    else undo_discard_last(el);
    return CC_NORM;
}

static unsigned char em_upper_case_word(EditLine *el, int c) {
    (void)c;
    el->last_cmd_was_kill = 0;
    el->yank_active = 0;
    undo_push(el);
    if (apply_word_case(el, 1)) refresh_line(el);
    else undo_discard_last(el);
    return CC_NORM;
}

static unsigned char em_yank_last_arg(EditLine *el, int c) {
    (void)c;
    el->last_cmd_was_kill = 0;
    el->yank_active = 0;
    undo_push(el);
    if (yank_last_arg(el)) refresh_line(el);
    else undo_discard_last(el);
    return CC_NORM;
}

static unsigned char em_kill_word(EditLine *el, int c) {
    (void)c;
    el->yank_active = 0;
    undo_push(el);
    if (kill_word(el)) refresh_line(el);
    return CC_NORM;
}

static unsigned char em_next_word(EditLine *el, int c) {
    size_t old_cursor;
    (void)c;
    el->last_cmd_was_kill = 0;
    el->yank_active = 0;
    undo_push(el);
    old_cursor = el->line.cursor;
    move_forward_word(el);
    if (el->line.cursor != old_cursor) refresh_line(el);
    else undo_discard_last(el);
    return CC_NORM;
}

static unsigned char em_prev_word(EditLine *el, int c) {
    size_t old_cursor;
    (void)c;
    el->last_cmd_was_kill = 0;
    el->yank_active = 0;
    undo_push(el);
    old_cursor = el->line.cursor;
    move_backward_word(el);
    if (el->line.cursor != old_cursor) refresh_line(el);
    else undo_discard_last(el);
    return CC_NORM;
}

static unsigned char em_yank_pop(EditLine *el, int c) {
    (void)c;
    el->last_cmd_was_kill = 0;
    undo_push(el);
    if (yank_pop(el)) refresh_line(el);
    return CC_NORM;
}

static unsigned char em_backward_kill_word(EditLine *el, int c) {
    (void)c;
    el->yank_active = 0;
    undo_push(el);
    if (backward_kill_word(el)) refresh_line(el);
    return CC_NORM;
}

/*
 * CSI digit dispatch: handles ESC[<digit>~ and ESC[1;mod;final
 * Called from the CSI sub-keymap when a digit 0-9 is received.
 */
static unsigned char em_csi_dispatch(EditLine *el, int c) {
    char seq2;
    if (!read_esc_byte(el, &seq2, 80)) return CC_NORM;
    if (seq2 == '~') {
        switch (c) {
        case '1': return ed_move_to_beg(el, c);
        case '2': return ed_toggle_overwrite(el, c);
        case '3': /* Delete key: delete char, never EOF */
            el->last_cmd_was_kill = 0;
            el->yank_active = 0;
            if (el->line.cursor < el->line.len) {
                undo_push(el);
                memmove(el->line.buffer + el->line.cursor,
                        el->line.buffer + el->line.cursor + 1,
                        el->line.len - el->line.cursor);
                el->line.len--;
                refresh_line(el);
            }
            return CC_NORM;
        case '4': return ed_move_to_end(el, c);
        case '5': return ed_beginning_of_history(el, c);
        case '6': return em_end_of_history(el, c);
        default: return CC_NORM;
        }
    }
    if (c == '1' && seq2 == ';') {
        char mod, final;
        if (!read_esc_byte(el, &mod, 80)) return CC_NORM;
        if (!read_esc_byte(el, &final, 80)) return CC_NORM;
        (void)mod;
        switch (final) {
        case 'A': return ed_prev_history(el, final);
        case 'B': return ed_next_history(el, final);
        case 'C': return ed_next_char(el, final);
        case 'D': return ed_prev_char(el, final);
        case 'H': return ed_move_to_beg(el, final);
        case 'F': return ed_move_to_end(el, final);
        default: return CC_NORM;
        }
    }
    return CC_NORM;
}

/* -- Vi-specific actions (vi_*) -- */

static unsigned char vi_to_command_mode(EditLine *el, int c) {
    (void)c;
    if (el->vi_mode == VI_INSERT) vi_capture_insert(el);
    el->vi_mode = VI_COMMAND;
    el->vi_count = 0;
    if (el->line.cursor > 0) el->line.cursor--;
    refresh_line(el);
    return CC_NORM;
}

static unsigned char vi_arg_digit(EditLine *el, int c) {
    el->vi_count = el->vi_count * 10 + (c - '0');
    return CC_NORM;
}

static unsigned char vi_motion_h(EditLine *el, int c) {
    int count = el->vi_count > 0 ? el->vi_count : 1;
    (void)c;
    el->vi_count = 0;
    if (el->line.cursor > 0) {
        el->line.cursor -= ((size_t)count <= el->line.cursor) ? (size_t)count : el->line.cursor;
        refresh_line(el);
    }
    return CC_NORM;
}

static unsigned char vi_motion_l(EditLine *el, int c) {
    int count = el->vi_count > 0 ? el->vi_count : 1;
    size_t max_move;
    (void)c;
    el->vi_count = 0;
    if (el->line.cursor < el->line.len) {
        max_move = el->line.len - el->line.cursor;
        el->line.cursor += ((size_t)count <= max_move) ? (size_t)count : max_move;
        if (el->line.cursor >= el->line.len && el->line.len > 0)
            el->line.cursor = el->line.len - 1;
        refresh_line(el);
    }
    return CC_NORM;
}

static unsigned char vi_motion_w(EditLine *el, int c) {
    int count = el->vi_count > 0 ? el->vi_count : 1;
    int i;
    (void)c;
    el->vi_count = 0;
    for (i = 0; i < count; i++) el->line.cursor = vi_forward_word(el, el->line.cursor);
    if (el->line.cursor >= el->line.len && el->line.len > 0)
        el->line.cursor = el->line.len - 1;
    refresh_line(el);
    return CC_NORM;
}

static unsigned char vi_motion_W(EditLine *el, int c) {
    int count = el->vi_count > 0 ? el->vi_count : 1;
    int i;
    (void)c;
    el->vi_count = 0;
    for (i = 0; i < count; i++) el->line.cursor = vi_forward_WORD(el, el->line.cursor);
    if (el->line.cursor >= el->line.len && el->line.len > 0)
        el->line.cursor = el->line.len - 1;
    refresh_line(el);
    return CC_NORM;
}

static unsigned char vi_motion_b(EditLine *el, int c) {
    int count = el->vi_count > 0 ? el->vi_count : 1;
    int i;
    (void)c;
    el->vi_count = 0;
    for (i = 0; i < count; i++) el->line.cursor = vi_backward_word(el, el->line.cursor);
    refresh_line(el);
    return CC_NORM;
}

static unsigned char vi_motion_B(EditLine *el, int c) {
    int count = el->vi_count > 0 ? el->vi_count : 1;
    int i;
    (void)c;
    el->vi_count = 0;
    for (i = 0; i < count; i++) el->line.cursor = vi_backward_WORD(el, el->line.cursor);
    refresh_line(el);
    return CC_NORM;
}

static unsigned char vi_motion_e(EditLine *el, int c) {
    int count = el->vi_count > 0 ? el->vi_count : 1;
    int i;
    (void)c;
    el->vi_count = 0;
    for (i = 0; i < count; i++) el->line.cursor = vi_end_word(el, el->line.cursor);
    refresh_line(el);
    return CC_NORM;
}

static unsigned char vi_motion_E(EditLine *el, int c) {
    int count = el->vi_count > 0 ? el->vi_count : 1;
    int i;
    (void)c;
    el->vi_count = 0;
    for (i = 0; i < count; i++) el->line.cursor = vi_end_WORD(el, el->line.cursor);
    refresh_line(el);
    return CC_NORM;
}

static unsigned char vi_beginning_of_line(EditLine *el, int c) {
    (void)c;
    if (el->vi_count > 0) {
        /* Part of a count sequence: 10, 20, etc. */
        el->vi_count = el->vi_count * 10;
        return CC_NORM;
    }
    el->line.cursor = 0;
    refresh_line(el);
    return CC_NORM;
}

static unsigned char vi_end_of_line(EditLine *el, int c) {
    (void)c;
    el->vi_count = 0;
    el->line.cursor = el->line.len > 0 ? el->line.len - 1 : 0;
    refresh_line(el);
    return CC_NORM;
}

static unsigned char vi_first_nonblank_action(EditLine *el, int c) {
    (void)c;
    el->vi_count = 0;
    el->line.cursor = vi_first_nonblank(el);
    refresh_line(el);
    return CC_NORM;
}

static unsigned char vi_find_char_action(EditLine *el, int c) {
    char fc;
    int count = el->vi_count > 0 ? el->vi_count : 1;
    int i;
    size_t target;
    el->vi_count = 0;
    if (!read_esc_byte(el, &fc, 80)) return CC_NORM;
    el->vi_find.ch = fc;
    el->vi_find.forward = (c == 'f' || c == 't');
    el->vi_find.till = (c == 't' || c == 'T');
    target = el->line.cursor;
    for (i = 0; i < count; i++) {
        if (el->vi_find.forward)
            target = vi_find_char_fwd(el, fc, target);
        else
            target = vi_find_char_bwd(el, fc, target);
    }
    if (target != el->line.cursor) {
        if (el->vi_find.till) {
            if (el->vi_find.forward && target > 0) target--;
            else if (!el->vi_find.forward && target < el->line.len) target++;
        }
        el->line.cursor = target;
        refresh_line(el);
    }
    return CC_NORM;
}

static unsigned char vi_repeat_find_action(EditLine *el, int c) {
    int count = el->vi_count > 0 ? el->vi_count : 1;
    int i;
    size_t target;
    (void)c;
    el->vi_count = 0;
    if (!el->vi_find.ch) return CC_NORM;
    target = el->line.cursor;
    for (i = 0; i < count; i++) {
        if (el->vi_find.forward)
            target = vi_find_char_fwd(el, el->vi_find.ch, target);
        else
            target = vi_find_char_bwd(el, el->vi_find.ch, target);
    }
    if (target != el->line.cursor) {
        if (el->vi_find.till) {
            if (el->vi_find.forward && target > 0) target--;
            else if (!el->vi_find.forward && target < el->line.len) target++;
        }
        el->line.cursor = target;
        refresh_line(el);
    }
    return CC_NORM;
}

static unsigned char vi_reverse_find_action(EditLine *el, int c) {
    int count = el->vi_count > 0 ? el->vi_count : 1;
    int i;
    size_t target;
    (void)c;
    el->vi_count = 0;
    if (!el->vi_find.ch) return CC_NORM;
    target = el->line.cursor;
    for (i = 0; i < count; i++) {
        if (!el->vi_find.forward)
            target = vi_find_char_fwd(el, el->vi_find.ch, target);
        else
            target = vi_find_char_bwd(el, el->vi_find.ch, target);
    }
    if (target != el->line.cursor) {
        if (el->vi_find.till) {
            if (!el->vi_find.forward && target > 0) target--;
            else if (el->vi_find.forward && target < el->line.len) target++;
        }
        el->line.cursor = target;
        refresh_line(el);
    }
    return CC_NORM;
}

static unsigned char vi_insert_mode(EditLine *el, int c) {
    int count = el->vi_count > 0 ? el->vi_count : 1;
    (void)c;
    el->vi_count = 0;
    vi_record_repeat(el, 'i', 0, count);
    vi_enter_insert(el);
    return CC_NORM;
}

static unsigned char vi_append_mode(EditLine *el, int c) {
    int count = el->vi_count > 0 ? el->vi_count : 1;
    (void)c;
    el->vi_count = 0;
    vi_record_repeat(el, 'a', 0, count);
    if (el->line.cursor < el->line.len) el->line.cursor++;
    vi_enter_insert(el);
    return CC_NORM;
}

static unsigned char vi_insert_beg(EditLine *el, int c) {
    int count = el->vi_count > 0 ? el->vi_count : 1;
    (void)c;
    el->vi_count = 0;
    vi_record_repeat(el, 'I', 0, count);
    el->line.cursor = 0;
    vi_enter_insert(el);
    refresh_line(el);
    return CC_NORM;
}

static unsigned char vi_append_end(EditLine *el, int c) {
    int count = el->vi_count > 0 ? el->vi_count : 1;
    (void)c;
    el->vi_count = 0;
    vi_record_repeat(el, 'A', 0, count);
    el->line.cursor = el->line.len;
    vi_enter_insert(el);
    refresh_line(el);
    return CC_NORM;
}

static unsigned char vi_delete_char_action(EditLine *el, int c) {
    int count = el->vi_count > 0 ? el->vi_count : 1;
    int i;
    (void)c;
    el->vi_count = 0;
    undo_push(el);
    vi_record_repeat(el, 'x', 0, count);
    for (i = 0; i < count && el->line.cursor < el->line.len; i++) {
        kill_ring_push(el, el->line.buffer + el->line.cursor, 1, i > 0);
        memmove(el->line.buffer + el->line.cursor,
                el->line.buffer + el->line.cursor + 1,
                el->line.len - el->line.cursor);
        el->line.len--;
    }
    if (el->line.cursor >= el->line.len && el->line.len > 0)
        el->line.cursor = el->line.len - 1;
    refresh_line(el);
    return CC_NORM;
}

static unsigned char vi_backward_delete_char(EditLine *el, int c) {
    int count = el->vi_count > 0 ? el->vi_count : 1;
    int i;
    (void)c;
    el->vi_count = 0;
    undo_push(el);
    vi_record_repeat(el, 'X', 0, count);
    for (i = 0; i < count && el->line.cursor > 0; i++) {
        el->line.cursor--;
        kill_ring_push(el, el->line.buffer + el->line.cursor, 1, i > 0);
        memmove(el->line.buffer + el->line.cursor,
                el->line.buffer + el->line.cursor + 1,
                el->line.len - el->line.cursor);
        el->line.len--;
    }
    refresh_line(el);
    return CC_NORM;
}

static unsigned char vi_replace_char_action(EditLine *el, int c) {
    char rc;
    int count = el->vi_count > 0 ? el->vi_count : 1;
    (void)c;
    el->vi_count = 0;
    if (!read_esc_byte(el, &rc, 80)) return CC_NORM;
    if (el->line.cursor < el->line.len && rc >= 32 && rc < 127) {
        undo_push(el);
        vi_record_repeat(el, 'r', rc, count);
        el->line.buffer[el->line.cursor] = rc;
        refresh_line(el);
    }
    return CC_NORM;
}

static unsigned char vi_replace_mode_action(EditLine *el, int c) {
    int count = el->vi_count > 0 ? el->vi_count : 1;
    (void)c;
    el->vi_count = 0;
    vi_record_repeat(el, 'R', 0, count);
    el->vi_mode = VI_REPLACE;
    return CC_NORM;
}

static unsigned char vi_substitute_char(EditLine *el, int c) {
    int count = el->vi_count > 0 ? el->vi_count : 1;
    int i;
    (void)c;
    el->vi_count = 0;
    undo_push(el);
    vi_record_repeat(el, 's', 0, count);
    for (i = 0; i < count && el->line.cursor < el->line.len; i++) {
        memmove(el->line.buffer + el->line.cursor,
                el->line.buffer + el->line.cursor + 1,
                el->line.len - el->line.cursor);
        el->line.len--;
    }
    vi_enter_insert(el);
    refresh_line(el);
    return CC_NORM;
}

static unsigned char vi_substitute_line(EditLine *el, int c) {
    (void)c;
    el->vi_count = 0;
    undo_push(el);
    vi_record_repeat(el, 'S', 0, 1);
    kill_ring_push(el, el->line.buffer, el->line.len, 0);
    el->line.len = 0;
    el->line.cursor = 0;
    el->line.buffer[0] = '\0';
    vi_enter_insert(el);
    refresh_line(el);
    return CC_NORM;
}

static unsigned char vi_delete_motion(EditLine *el, int c) {
    int motion;
    int count = el->vi_count > 0 ? el->vi_count : 1;
    size_t target, start, end;
    (void)c;
    el->vi_count = 0;
    if (read(fileno(el->fin), &motion, 1) != 1) return CC_NORM;
    motion &= 0xFF;
    if (motion == 'd') {
        undo_push(el);
        vi_record_repeat(el, 'd', 'd', count);
        kill_ring_push(el, el->line.buffer, el->line.len, 0);
        el->line.len = 0;
        el->line.cursor = 0;
        el->line.buffer[0] = '\0';
        refresh_line(el);
        return CC_NORM;
    }
    target = vi_motion_target(el, motion, count);
    if (target == (size_t)-1) return CC_NORM;
    start = (el->line.cursor < target) ? el->line.cursor : target;
    end = (el->line.cursor < target) ? target : el->line.cursor;
    if (end > el->line.len) end = el->line.len;
    if (start < end) {
        undo_push(el);
        vi_record_repeat(el, 'd', (char)motion, count);
        kill_ring_push(el, el->line.buffer + start, end - start, 0);
        delete_range(el, start, end);
        el->line.cursor = start;
        if (el->line.cursor >= el->line.len && el->line.len > 0)
            el->line.cursor = el->line.len - 1;
        refresh_line(el);
    }
    return CC_NORM;
}

static unsigned char vi_delete_to_end(EditLine *el, int c) {
    (void)c;
    el->vi_count = 0;
    if (el->line.cursor < el->line.len) {
        undo_push(el);
        vi_record_repeat(el, 'D', 0, 1);
        kill_ring_push(el, el->line.buffer + el->line.cursor,
                       el->line.len - el->line.cursor, 0);
        el->line.len = el->line.cursor;
        el->line.buffer[el->line.len] = '\0';
        if (el->line.cursor > 0) el->line.cursor--;
        refresh_line(el);
    }
    return CC_NORM;
}

static unsigned char vi_change_motion(EditLine *el, int c) {
    int motion;
    int count = el->vi_count > 0 ? el->vi_count : 1;
    size_t target, start, end;
    (void)c;
    el->vi_count = 0;
    if (read(fileno(el->fin), &motion, 1) != 1) return CC_NORM;
    motion &= 0xFF;
    if (motion == 'c') {
        undo_push(el);
        vi_record_repeat(el, 'c', 'c', count);
        kill_ring_push(el, el->line.buffer, el->line.len, 0);
        el->line.len = 0;
        el->line.cursor = 0;
        el->line.buffer[0] = '\0';
        vi_enter_insert(el);
        refresh_line(el);
        return CC_NORM;
    }
    target = vi_motion_target(el, motion, count);
    if (target == (size_t)-1) return CC_NORM;
    start = (el->line.cursor < target) ? el->line.cursor : target;
    end = (el->line.cursor < target) ? target : el->line.cursor;
    if (end > el->line.len) end = el->line.len;
    if (start < end) {
        undo_push(el);
        vi_record_repeat(el, 'c', (char)motion, count);
        kill_ring_push(el, el->line.buffer + start, end - start, 0);
        delete_range(el, start, end);
        el->line.cursor = start;
        vi_enter_insert(el);
        refresh_line(el);
    }
    return CC_NORM;
}

static unsigned char vi_change_to_end(EditLine *el, int c) {
    (void)c;
    el->vi_count = 0;
    undo_push(el);
    vi_record_repeat(el, 'C', 0, 1);
    if (el->line.cursor < el->line.len) {
        kill_ring_push(el, el->line.buffer + el->line.cursor,
                       el->line.len - el->line.cursor, 0);
        el->line.len = el->line.cursor;
        el->line.buffer[el->line.len] = '\0';
    }
    vi_enter_insert(el);
    refresh_line(el);
    return CC_NORM;
}

static unsigned char vi_yank_motion(EditLine *el, int c) {
    int motion;
    int count = el->vi_count > 0 ? el->vi_count : 1;
    size_t target, start, end;
    (void)c;
    el->vi_count = 0;
    if (read(fileno(el->fin), &motion, 1) != 1) return CC_NORM;
    motion &= 0xFF;
    if (motion == 'y') {
        kill_ring_push(el, el->line.buffer, el->line.len, 0);
        return CC_NORM;
    }
    target = vi_motion_target(el, motion, count);
    if (target == (size_t)-1) return CC_NORM;
    start = (el->line.cursor < target) ? el->line.cursor : target;
    end = (el->line.cursor < target) ? target : el->line.cursor;
    if (end > el->line.len) end = el->line.len;
    if (start < end)
        kill_ring_push(el, el->line.buffer + start, end - start, 0);
    return CC_NORM;
}

static unsigned char vi_paste_after(EditLine *el, int c) {
    (void)c;
    el->vi_count = 0;
    if (el->kill_ring_count > 0 && el->kill_ring[el->kill_ring_head]) {
        undo_push(el);
        vi_record_repeat(el, 'p', 0, 1);
        if (el->line.cursor < el->line.len) el->line.cursor++;
        (void)insert_span(el, el->kill_ring[el->kill_ring_head],
                          strlen(el->kill_ring[el->kill_ring_head]));
        if (el->line.cursor > 0) el->line.cursor--;
        refresh_line(el);
    }
    return CC_NORM;
}

static unsigned char vi_paste_before(EditLine *el, int c) {
    (void)c;
    el->vi_count = 0;
    if (el->kill_ring_count > 0 && el->kill_ring[el->kill_ring_head]) {
        undo_push(el);
        vi_record_repeat(el, 'P', 0, 1);
        (void)insert_span(el, el->kill_ring[el->kill_ring_head],
                          strlen(el->kill_ring[el->kill_ring_head]));
        if (el->line.cursor > 0) el->line.cursor--;
        refresh_line(el);
    }
    return CC_NORM;
}

static unsigned char vi_dot_repeat(EditLine *el, int c) {
    char rcmd = el->vi_repeat.cmd;
    char rarg = el->vi_repeat.arg;
    int rcount = el->vi_repeat.count > 0 ? el->vi_repeat.count : 1;
    (void)c;
    el->vi_count = 0;
    if (!rcmd) return CC_NORM;

    undo_push(el);

    switch (rcmd) {
    case 'x': {
        int i;
        for (i = 0; i < rcount && el->line.cursor < el->line.len; i++) {
            memmove(el->line.buffer + el->line.cursor,
                    el->line.buffer + el->line.cursor + 1,
                    el->line.len - el->line.cursor);
            el->line.len--;
        }
        if (el->line.cursor >= el->line.len && el->line.len > 0)
            el->line.cursor = el->line.len - 1;
        break;
    }
    case 'X': {
        int i;
        for (i = 0; i < rcount && el->line.cursor > 0; i++) {
            el->line.cursor--;
            memmove(el->line.buffer + el->line.cursor,
                    el->line.buffer + el->line.cursor + 1,
                    el->line.len - el->line.cursor);
            el->line.len--;
        }
        break;
    }
    case 'r':
        if (el->line.cursor < el->line.len && rarg >= 32 && rarg < 127)
            el->line.buffer[el->line.cursor] = rarg;
        break;
    case 'D':
        if (el->line.cursor < el->line.len) {
            el->line.len = el->line.cursor;
            el->line.buffer[el->line.len] = '\0';
            if (el->line.cursor > 0) el->line.cursor--;
        }
        break;
    case 'S':
        el->line.len = 0;
        el->line.cursor = 0;
        el->line.buffer[0] = '\0';
        vi_enter_insert(el);
        break;
    case 's': {
        int i;
        for (i = 0; i < rcount && el->line.cursor < el->line.len; i++) {
            memmove(el->line.buffer + el->line.cursor,
                    el->line.buffer + el->line.cursor + 1,
                    el->line.len - el->line.cursor);
            el->line.len--;
        }
        vi_enter_insert(el);
        break;
    }
    case '~':
        if (el->line.cursor < el->line.len) {
            unsigned char ch = (unsigned char)el->line.buffer[el->line.cursor];
            if (isupper(ch)) el->line.buffer[el->line.cursor] = (char)tolower(ch);
            else if (islower(ch)) el->line.buffer[el->line.cursor] = (char)toupper(ch);
            if (el->line.cursor + 1 < el->line.len) el->line.cursor++;
        }
        break;
    default:
        break;
    }

    /* Replay saved insert text for insert-mode commands */
    if (el->vi_repeat.insert_text && el->vi_repeat.insert_len > 0 &&
        (rcmd == 'i' || rcmd == 'a' || rcmd == 'I' || rcmd == 'A' ||
         rcmd == 'c' || rcmd == 'C' || rcmd == 's' || rcmd == 'S')) {
        if (rcmd == 'a' && el->line.cursor < el->line.len) el->line.cursor++;
        if (rcmd == 'I') el->line.cursor = 0;
        if (rcmd == 'A') el->line.cursor = el->line.len;
        (void)insert_span(el, el->vi_repeat.insert_text, el->vi_repeat.insert_len);
        el->vi_mode = VI_COMMAND;
        if (el->line.cursor > 0) el->line.cursor--;
    }

    refresh_line(el);
    return CC_NORM;
}

static unsigned char vi_toggle_case(EditLine *el, int c) {
    unsigned char ch;
    (void)c;
    el->vi_count = 0;
    if (el->line.cursor < el->line.len) {
        undo_push(el);
        vi_record_repeat(el, '~', 0, 1);
        ch = (unsigned char)el->line.buffer[el->line.cursor];
        if (isupper(ch)) el->line.buffer[el->line.cursor] = (char)tolower(ch);
        else if (islower(ch)) el->line.buffer[el->line.cursor] = (char)toupper(ch);
        if (el->line.cursor + 1 < el->line.len) el->line.cursor++;
        refresh_line(el);
    }
    return CC_NORM;
}

static unsigned char vi_search_forward_action(EditLine *el, int c) {
    (void)c;
    el->vi_count = 0;
    vi_history_search(el, 1);
    return CC_NORM;
}

static unsigned char vi_search_backward_action(EditLine *el, int c) {
    (void)c;
    el->vi_count = 0;
    vi_history_search(el, 0);
    return CC_NORM;
}

static unsigned char vi_search_next_action(EditLine *el, int c) {
    (void)c;
    el->vi_count = 0;
    if (el->vi_search.pattern[0]) {
        const char *match = history_find_match(el, el->vi_search.pattern,
                                               el->vi_search.reverse);
        if (match) {
            line_load_history(el, match);
            el->history_browsing = 1;
            refresh_line(el);
        }
    }
    return CC_NORM;
}

static unsigned char vi_search_prev_action(EditLine *el, int c) {
    (void)c;
    el->vi_count = 0;
    if (el->vi_search.pattern[0]) {
        const char *match = history_find_match(el, el->vi_search.pattern,
                                               !el->vi_search.reverse);
        if (match) {
            line_load_history(el, match);
            el->history_browsing = 1;
            refresh_line(el);
        }
    }
    return CC_NORM;
}

static unsigned char vi_edit_external(EditLine *el, int c) {
    const char *editor;
    char tmpfile[64];
    char cmd[512];
    FILE *fp;
    char buf[4096];
    size_t nread;
    int fd;
    (void)c;
    el->vi_count = 0;

    editor = getenv("VISUAL");
    if (!editor) editor = getenv("EDITOR");
    if (!editor) editor = "vi";

    terminal_set_orig(el);

    snprintf(tmpfile, sizeof(tmpfile), "/tmp/el_edit.XXXXXX");
    fd = mkstemp(tmpfile);
    if (fd < 0) {
        terminal_set_raw(el);
        return CC_NORM;
    }
    (void)write(fd, el->line.buffer, el->line.len);
    close(fd);

    snprintf(cmd, sizeof(cmd), "%s %s", editor, tmpfile);
    (void)system(cmd);

    fp = fopen(tmpfile, "r");
    if (fp) {
        nread = fread(buf, 1, sizeof(buf) - 1, fp);
        fclose(fp);
        buf[nread] = '\0';
        while (nread > 0 && (buf[nread - 1] == '\n' || buf[nread - 1] == '\r'))
            buf[--nread] = '\0';
        undo_push(el);
        (void)line_set_text(el, buf);
    }
    unlink(tmpfile);

    terminal_set_raw(el);
    refresh_line(el);
    return CC_NEWLINE;
}

static unsigned char vi_comment_line(EditLine *el, int c) {
    (void)c;
    el->vi_count = 0;
    undo_push(el);
    el->line.cursor = 0;
    if (line_ensure_capacity(el, el->line.len + 2) == 0) {
        memmove(el->line.buffer + 1, el->line.buffer, el->line.len + 1);
        el->line.buffer[0] = '#';
        el->line.len++;
    }
    el->vi_mode = VI_INSERT;
    return CC_NEWLINE;
}

static unsigned char vi_replace_back(EditLine *el, int c) {
    (void)c;
    if (el->line.cursor > 0) {
        undo_push(el);
        el->line.cursor--;
        refresh_line(el);
    }
    return CC_NORM;
}

static unsigned char vi_replace_insert(EditLine *el, int c) {
    if (c < 32 || c >= 127) return CC_NORM;
    undo_push(el);
    if (el->line.cursor < el->line.len) {
        el->line.buffer[el->line.cursor] = (char)c;
        el->line.cursor++;
    } else {
        if (line_ensure_capacity(el, el->line.len + 2) == 0) {
            el->line.buffer[el->line.cursor] = (char)c;
            el->line.len++;
            el->line.cursor++;
            el->line.buffer[el->line.len] = '\0';
        }
    }
    refresh_line(el);
    return CC_NORM;
}

/* ================================================================== */
/* Builtin action registry                                            */
/* ================================================================== */

static const struct action_entry builtin_actions[] = {
    /* Editor-wide */
    {"ed-newline",              "Accept line",                ed_newline},
    {"ed-insert",               "Self-insert character",      ed_insert},
    {"ed-delete-next-char",     "Delete or EOF",              ed_delete_next_char},
    {"ed-tty-sigint",           "Send SIGINT (^C)",           ed_tty_sigint},
    {"ed-clear-screen",         "Clear screen",               ed_clear_screen},
    {"ed-move-to-beg",          "Cursor to beginning",        ed_move_to_beg},
    {"ed-prev-char",            "Cursor left",                ed_prev_char},
    {"ed-move-to-end",          "Cursor to end",              ed_move_to_end},
    {"ed-next-char",            "Cursor right",               ed_next_char},
    {"ed-complete",             "Complete filename",           ed_complete},
    {"ed-kill-line",            "Kill to end of line",         ed_kill_line},
    {"ed-next-history",         "Next history entry",          ed_next_history},
    {"ed-prev-history",         "Previous history entry",      ed_prev_history},
    {"ed-transpose-chars",      "Transpose characters",        ed_transpose_chars},
    {"ed-undo",                 "Undo last change",            ed_undo},
    {"ed-toggle-overwrite",     "Toggle insert/overwrite",     ed_toggle_overwrite},
    {"ed-beginning-of-history", "First history entry",         ed_beginning_of_history},
    {"em-end-of-history",       "Last history entry",          em_end_of_history},
    /* Emacs-specific */
    {"em-delete-prev-char",     "Delete backward",             em_delete_prev_char},
    {"em-kill-region",          "Kill backward to line start", em_kill_region},
    {"em-delete-prev-word",     "Delete word backward",        em_delete_prev_word},
    {"em-yank",                 "Yank from kill ring",         em_yank},
    {"em-search-prev",          "Incremental search backward", em_search_prev},
    {"em-search-next",          "Incremental search forward",  em_search_next},
    {"em-capitalize-word",      "Capitalize word",             em_capitalize_word},
    {"em-lower-case-word",      "Lowercase word",              em_lower_case_word},
    {"em-upper-case-word",      "Uppercase word",              em_upper_case_word},
    {"em-yank-last-arg",        "Yank last argument",          em_yank_last_arg},
    {"em-kill-word",            "Kill word forward",           em_kill_word},
    {"em-next-word",            "Move forward word",           em_next_word},
    {"em-prev-word",            "Move backward word",          em_prev_word},
    {"em-yank-pop",             "Yank pop (cycle kill ring)",  em_yank_pop},
    {"em-backward-kill-word",   "Kill word backward",          em_backward_kill_word},
    {"em-csi-dispatch",         "CSI sequence dispatch",       em_csi_dispatch},
    /* Vi mode */
    {"vi-to-command-mode",      "Enter vi command mode",       vi_to_command_mode},
    {"vi-arg-digit",            "Vi digit argument",           vi_arg_digit},
    {"vi-motion-h",             "Vi cursor left",              vi_motion_h},
    {"vi-motion-l",             "Vi cursor right",             vi_motion_l},
    {"vi-motion-w",             "Vi forward word",             vi_motion_w},
    {"vi-motion-W",             "Vi forward WORD",             vi_motion_W},
    {"vi-motion-b",             "Vi backward word",            vi_motion_b},
    {"vi-motion-B",             "Vi backward WORD",            vi_motion_B},
    {"vi-motion-e",             "Vi end of word",              vi_motion_e},
    {"vi-motion-E",             "Vi end of WORD",              vi_motion_E},
    {"vi-beginning-of-line",    "Vi beginning of line (0)",    vi_beginning_of_line},
    {"vi-end-of-line",          "Vi end of line ($)",          vi_end_of_line},
    {"vi-first-nonblank",       "Vi first non-blank (^)",      vi_first_nonblank_action},
    {"vi-find-char",            "Vi find character (f/F/t/T)", vi_find_char_action},
    {"vi-repeat-find",          "Vi repeat find (;)",          vi_repeat_find_action},
    {"vi-reverse-find",         "Vi reverse find (,)",         vi_reverse_find_action},
    {"vi-insert-mode",          "Vi insert mode (i)",          vi_insert_mode},
    {"vi-append-mode",          "Vi append mode (a)",          vi_append_mode},
    {"vi-insert-beg",           "Vi insert at beginning (I)",  vi_insert_beg},
    {"vi-append-end",           "Vi append at end (A)",        vi_append_end},
    {"vi-delete-char",          "Vi delete char (x)",          vi_delete_char_action},
    {"vi-backward-delete-char", "Vi backward delete (X)",      vi_backward_delete_char},
    {"vi-replace-char",         "Vi replace char (r)",         vi_replace_char_action},
    {"vi-replace-mode",         "Vi replace mode (R)",         vi_replace_mode_action},
    {"vi-substitute-char",      "Vi substitute char (s)",      vi_substitute_char},
    {"vi-substitute-line",      "Vi substitute line (S)",      vi_substitute_line},
    {"vi-delete-motion",        "Vi delete over motion (d)",   vi_delete_motion},
    {"vi-delete-to-end",        "Vi delete to end (D)",        vi_delete_to_end},
    {"vi-change-motion",        "Vi change over motion (c)",   vi_change_motion},
    {"vi-change-to-end",        "Vi change to end (C)",        vi_change_to_end},
    {"vi-yank-motion",          "Vi yank over motion (y)",     vi_yank_motion},
    {"vi-paste-after",          "Vi paste after (p)",          vi_paste_after},
    {"vi-paste-before",         "Vi paste before (P)",         vi_paste_before},
    {"vi-dot-repeat",           "Vi dot repeat (.)",           vi_dot_repeat},
    {"vi-toggle-case",          "Vi toggle case (~)",          vi_toggle_case},
    {"vi-next-history",         "Vi next history (j)",         ed_next_history},
    {"vi-prev-history",         "Vi prev history (k)",         ed_prev_history},
    {"vi-search-forward",       "Vi search forward (/)",       vi_search_forward_action},
    {"vi-search-backward",      "Vi search backward (?)",      vi_search_backward_action},
    {"vi-search-next",          "Vi search next (n)",          vi_search_next_action},
    {"vi-search-prev",          "Vi search prev (N)",          vi_search_prev_action},
    {"vi-edit-external",        "Vi external editor (v)",      vi_edit_external},
    {"vi-comment-line",         "Vi comment line (#)",         vi_comment_line},
    {"vi-replace-back",         "Vi replace backspace",        vi_replace_back},
    {"vi-replace-insert",       "Vi replace self-insert",      vi_replace_insert},
};

static const int n_builtin_actions = (int)(sizeof(builtin_actions) / sizeof(builtin_actions[0]));

const struct action_entry *el_builtin_actions(int *count) {
    if (count) *count = n_builtin_actions;
    return builtin_actions;
}

const struct action_entry *el_find_action(EditLine *el, const char *name) {
    int i;
    if (!name) return NULL;
    for (i = 0; i < n_builtin_actions; i++) {
        if (strcmp(builtin_actions[i].name, name) == 0)
            return &builtin_actions[i];
    }
    if (el) {
        for (i = 0; i < el->n_user_actions; i++) {
            if (strcmp(el->user_actions[i].name, name) == 0)
                return &el->user_actions[i];
        }
    }
    return NULL;
}

/* Expose read_esc_byte for keymap dispatch (cross-module use) */
int el_read_esc_byte(EditLine *el, char *out, int timeout_ms) {
    return read_esc_byte(el, out, timeout_ms);
}

/* ================================================================== */
/* el_gets: main entry point — keymap-based dispatch                  */
/* ================================================================== */
const char *el_gets(EditLine *el, int *count) {
    size_t i;
    int c;
    unsigned char result;
    ssize_t n;
    struct keymap_entry *current_keymap;

    if (count) *count = 0;
    if (terminal_set_raw(el) == -1) return NULL;

    terminal_get_size(el);

    el->line.len = 0;
    el->line.cursor = 0;
    el->line.buffer[0] = '\0';
    el->refresh_rows = 1;
    el->render_cache_len = 0;
    el->render_cache_cursor = 0;
    el->history_browsing = 0;
    el->yank_active = 0;
    el->vi_count = 0;
    el->last_action_was_complete = 0;
    for (i = 0; i < el->undo_depth; i++) {
        if (el->undo_stack[i].buffer) {
            free(el->undo_stack[i].buffer);
            el->undo_stack[i].buffer = NULL;
        }
    }
    el->undo_depth = 0;
    if (el->render_cache) el->render_cache[0] = '\0';

    /* Vi mode starts in insert mode */
    if (el->editor_mode == ED_VI)
        el->vi_mode = VI_INSERT;

    /* Install signal handlers; save old dispositions */
    el_signals_install(el);

    /* Display prompt (flushed before blocking read) */
    if (el->prompt) {
        terminal_puts(el, el->prompt);
        terminal_flush(el);
    }

    for (;;) {
        n = read(fileno(el->fin), &c, 1);
        if (n == -1) {
            if (errno == EINTR) {
                el_signal_handle(el);
                refresh_line(el);
                continue;
            }
            break;  /* real error */
        }
        if (n == 0) break;  /* EOF */

        c &= 0xFF;

        /* Select keymap based on current mode */
        if (el->editor_mode == ED_EMACS) {
            current_keymap = (struct keymap_entry *)el->emacs_keymap;
        } else {
            switch (el->vi_mode) {
            case VI_COMMAND:
                current_keymap = (struct keymap_entry *)el->vi_command_keymap;
                break;
            case VI_REPLACE:
                current_keymap = (struct keymap_entry *)el->vi_replace_keymap;
                break;
            default: /* VI_INSERT */
                current_keymap = (struct keymap_entry *)el->vi_insert_keymap;
                break;
            }
        }

        /* Dispatch through keymap (falls back to CC_NORM for unbound keys) */
        if (current_keymap)
            result = keymap_dispatch(el, current_keymap, c);
        else
            result = CC_NORM;

        if (result == CC_NEWLINE) {
            terminal_puts(el, "\r\n");
            terminal_flush(el);
            el_signals_restore(el);
            terminal_set_orig(el);
            if (count) *count = (int)el->line.len;
            return el->line.buffer;
        }
        if (result == CC_EOF) {
            el_signals_restore(el);
            terminal_set_orig(el);
            return NULL;
        }
    }

    el_signals_restore(el);
    terminal_set_orig(el);
    return NULL;
}
