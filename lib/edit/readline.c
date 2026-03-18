#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <sys/select.h>
#include <dirent.h>
#include <sys/stat.h>
#include "el.h"

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
        fprintf(el->fout, "\r");
        if (target_col > 0) fprintf(el->fout, "\033[%zuC", target_col);
        fwrite(el->line.buffer + dirty_from, 1, el->line.len - dirty_from, el->fout);
        if (el->render_cache_len > el->line.len) fprintf(el->fout, "\033[K");
        fprintf(el->fout, "\r");
        if (cursor_total > 0) fprintf(el->fout, "\033[%zuC", cursor_total);
        fflush(el->fout);
        el->refresh_rows = 1;
        line_update_cache(el);
        return;
    }

    /* Return to first physical row of the previous render block. */
    fprintf(el->fout, "\r");
    if (el->refresh_rows > 1) fprintf(el->fout, "\033[%zuA", el->refresh_rows - 1);
    fprintf(el->fout, "\r");

    /* Clear the old wrapped rows. */
    for (i = 0; i < el->refresh_rows; i++) {
        fprintf(el->fout, "\033[2K");
        if (i + 1 < el->refresh_rows) fprintf(el->fout, "\n");
    }
    if (el->refresh_rows > 1) fprintf(el->fout, "\033[%zuA", el->refresh_rows - 1);
    fprintf(el->fout, "\r");

    if (el->prompt) fwrite(el->prompt, 1, prompt_len, el->fout);
    if (el->line.len > 0) fwrite(el->line.buffer, 1, el->line.len, el->fout);

    /* Reposition cursor from render end to logical cursor location. */
    if (end_row > cursor_row) fprintf(el->fout, "\033[%zuA", end_row - cursor_row);
    fprintf(el->fout, "\r");
    if (cursor_col > 0) fprintf(el->fout, "\033[%zuC", cursor_col);

    fflush(el->fout);
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
        fprintf(el->fout, "\r\033[K(%s-i-search)`%s': %s",
                reverse ? "reverse" : "forward",
                query,
                match ? match : "");
        fflush(el->fout);

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

const char *el_gets(EditLine *el, int *count) {
    size_t i;

    if (count) *count = 0;
    if (terminal_set_raw(el) == -1) return NULL;

    /* Refresh terminal dimensions on each gets entry */
    terminal_get_size(el);

    el->line.len = 0;
    el->line.cursor = 0;
    el->line.buffer[0] = '\0';
    el->refresh_rows = 1;
    el->render_cache_len = 0;
    el->render_cache_cursor = 0;
    el->history_browsing = 0;
    el->yank_active = 0;
    for (i = 0; i < el->undo_depth; i++) {
        if (el->undo_stack[i].buffer) {
            free(el->undo_stack[i].buffer);
            el->undo_stack[i].buffer = NULL;
        }
    }
    el->undo_depth = 0;
    if (el->render_cache) el->render_cache[0] = '\0';

    if (el->prompt) {
        fprintf(el->fout, "%s", el->prompt);
        fflush(el->fout);
    }

    int c;
    while (read(fileno(el->fin), &c, 1) == 1) {
        c &= 0xFF;

        if (c == '\n' || c == '\r') {
            el->last_cmd_was_kill = 0;
            el->yank_active = 0;
            fprintf(el->fout, "\r\n");
            terminal_set_orig(el);
            if (count) *count = el->line.len;
            return el->line.buffer;
        } else if (c == 0x7F || c == 0x08) { /* Backspace */
            if (el->line.cursor > 0) {
                el->last_cmd_was_kill = 0;
                el->yank_active = 0;
                undo_push(el);
                memmove(el->line.buffer + el->line.cursor - 1,
                        el->line.buffer + el->line.cursor,
                        el->line.len - el->line.cursor + 1);
                el->line.len--;
                el->line.cursor--;
                refresh_line(el);
            }
        } else if (c == 0x04) { /* ^D - EOF */
            if (el->line.len == 0) {
                el->last_cmd_was_kill = 0;
                el->yank_active = 0;
                terminal_set_orig(el);
                return NULL;
            }
            if (el->line.cursor < el->line.len) {
                el->last_cmd_was_kill = 0;
                el->yank_active = 0;
                undo_push(el);
                memmove(el->line.buffer + el->line.cursor,
                        el->line.buffer + el->line.cursor + 1,
                        el->line.len - el->line.cursor);
                el->line.len--;
                refresh_line(el);
            } else if (el->completion) {
                el->yank_active = 0;
                el->completion(el, c);
                refresh_line(el);
            }
        } else if (c == 0x03) { /* ^C - Clear line */
            el->last_cmd_was_kill = 0;
            el->yank_active = 0;
            fprintf(el->fout, "^C\r\n");
            el_reset(el);
            if (el->prompt) {
                fprintf(el->fout, "%s", el->prompt);
                fflush(el->fout);
            }
        } else if (c == 0x0C) { /* ^L - Clear screen */
            el->last_cmd_was_kill = 0;
            el->yank_active = 0;
            fprintf(el->fout, "\033[H\033[2J");
            refresh_line(el);
        } else if (c == 0x01) { /* ^A */
            el->last_cmd_was_kill = 0;
            el->yank_active = 0;
            if (el->line.cursor != 0) {
                undo_push(el);
                el->line.cursor = 0;
                refresh_line(el);
            }
        } else if (c == 0x02) { /* ^B */
            el->last_cmd_was_kill = 0;
            el->yank_active = 0;
            if (el->line.cursor > 0) {
                undo_push(el);
                el->line.cursor--;
                refresh_line(el);
            }
        } else if (c == 0x05) { /* ^E */
            el->last_cmd_was_kill = 0;
            el->yank_active = 0;
            if (el->line.cursor != el->line.len) {
                undo_push(el);
                el->line.cursor = el->line.len;
                refresh_line(el);
            }
        } else if (c == 0x06) { /* ^F */
            el->last_cmd_was_kill = 0;
            el->yank_active = 0;
            if (el->line.cursor < el->line.len) {
                undo_push(el);
                el->line.cursor++;
                refresh_line(el);
            }
        } else if (c == 0x09) { /* Tab complete */
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
                    fputc('\a', el->fout);
                    fflush(el->fout);
                }
            }
        } else if (c == 0x0E) { /* ^N next-history */
            el->last_cmd_was_kill = 0;
            el->yank_active = 0;
            if (el->history) {
                HistEvent ev;
                if (history(el->history, &ev, H_NEXT) == 0 && ev.str) {
                    undo_push(el);
                    line_load_history(el, ev.str);
                    refresh_line(el);
                } else {
                    undo_push(el);
                    history_restore_current_input(el);
                    el->history_browsing = 0;
                    refresh_line(el);
                }
            }
        } else if (c == 0x10) { /* ^P previous-history */
            el->last_cmd_was_kill = 0;
            el->yank_active = 0;
            if (el->history) {
                HistEvent ev;
                if (!el->history_browsing) {
                    (void)history_save_current_input(el);
                    el->history_browsing = 1;
                }
                if (history(el->history, &ev, H_PREV) == 0 && ev.str) {
                    undo_push(el);
                    line_load_history(el, ev.str);
                    refresh_line(el);
                }
            }
        } else if (c == 0x12) { /* ^R history-search-backward */
            el->last_cmd_was_kill = 0;
            el->yank_active = 0;
            history_incremental_search(el, 1);
        } else if (c == 0x13) { /* ^S history-search-forward */
            el->last_cmd_was_kill = 0;
            el->yank_active = 0;
            history_incremental_search(el, 0);
        } else if (c == 0x0B) { /* ^K kill-line */
            el->yank_active = 0;
            undo_push(el);
            if (kill_line(el)) refresh_line(el);
        } else if (c == 0x15) { /* ^U backward-kill-line */
            el->yank_active = 0;
            undo_push(el);
            if (backward_kill_line(el)) refresh_line(el);
        } else if (c == 0x17) { /* ^W unix-word-rubout */
            el->yank_active = 0;
            undo_push(el);
            if (backward_kill_word(el)) refresh_line(el);
        } else if (c == 0x14) { /* ^T transpose-chars */
            el->last_cmd_was_kill = 0;
            el->yank_active = 0;
            undo_push(el);
            if (transpose_chars(el)) refresh_line(el);
        } else if (c == 0x19) { /* ^Y yank */
            el->last_cmd_was_kill = 0;
            undo_push(el);
            if (yank_from_kill_ring(el)) refresh_line(el);
        } else if (c == 0x1F) { /* ^_ undo */
            el->last_cmd_was_kill = 0;
            el->yank_active = 0;
            if (undo_pop(el)) refresh_line(el);
        } else if (c == 0x1B) { /* ESC */
            char seq0;
            if (!read_esc_byte(el, &seq0, 80)) {
                el->last_cmd_was_kill = 0;
                el->yank_active = 0;
                continue;
            }
            if (seq0 == '[') {
                char seq1;
                if (!read_esc_byte(el, &seq1, 80)) continue;

                if (seq1 == 'C') { /* Right */
                    el->last_cmd_was_kill = 0;
                    el->yank_active = 0;
                    if (el->line.cursor < el->line.len) {
                        undo_push(el);
                        el->line.cursor++;
                        refresh_line(el);
                    }
                } else if (seq1 == 'D') { /* Left */
                    el->last_cmd_was_kill = 0;
                    el->yank_active = 0;
                    if (el->line.cursor > 0) {
                        undo_push(el);
                        el->line.cursor--;
                        refresh_line(el);
                    }
                } else if (seq1 == 'H') { /* Home */
                    el->last_cmd_was_kill = 0;
                    el->yank_active = 0;
                    if (el->line.cursor != 0) {
                        undo_push(el);
                        el->line.cursor = 0;
                        refresh_line(el);
                    }
                } else if (seq1 == 'F') { /* End */
                    el->last_cmd_was_kill = 0;
                    el->yank_active = 0;
                    if (el->line.cursor != el->line.len) {
                        undo_push(el);
                        el->line.cursor = el->line.len;
                        refresh_line(el);
                    }
                } else if (seq1 == 'A') { /* Up (History) */
                    el->last_cmd_was_kill = 0;
                    el->yank_active = 0;
                    if (el->history) {
                        HistEvent ev;
                        if (!el->history_browsing) {
                            (void)history_save_current_input(el);
                            el->history_browsing = 1;
                        }
                        if (history(el->history, &ev, H_PREV) == 0 && ev.str) {
                            line_load_history(el, ev.str);
                            refresh_line(el);
                        }
                    }
                } else if (seq1 == 'B') { /* Down (History) */
                    el->last_cmd_was_kill = 0;
                    el->yank_active = 0;
                    if (el->history) {
                        HistEvent ev;
                        if (history(el->history, &ev, H_NEXT) == 0 && ev.str) {
                            line_load_history(el, ev.str);
                            refresh_line(el);
                        } else {
                            history_restore_current_input(el);
                            el->history_browsing = 0;
                            refresh_line(el);
                        }
                    }
                } else if (seq1 >= '0' && seq1 <= '9') {
                    char seq2;
                    if (!read_esc_byte(el, &seq2, 80)) continue;

                        if (seq2 == '~') {
                            el->last_cmd_was_kill = 0;
                            el->yank_active = 0;
                            if (seq1 == '1') {
                                if (el->line.cursor != 0) {
                                    undo_push(el);
                                    el->line.cursor = 0;
                                    refresh_line(el);
                                }
                            } else if (seq1 == '2') {
                                el->overwrite_mode = !el->overwrite_mode;
                            } else if (seq1 == '3') { /* Delete */
                            if (el->line.cursor < el->line.len) {
                                undo_push(el);
                                memmove(el->line.buffer + el->line.cursor,
                                        el->line.buffer + el->line.cursor + 1,
                                        el->line.len - el->line.cursor);
                                el->line.len--;
                                refresh_line(el);
                            }
                            } else if (seq1 == '4') {
                                if (el->line.cursor != el->line.len) {
                                    undo_push(el);
                                    el->line.cursor = el->line.len;
                                    refresh_line(el);
                                }
                        } else if (seq1 == '5') { /* PgUp */
                            if (el->history) {
                                HistEvent ev;
                                if (!el->history_browsing) {
                                    (void)history_save_current_input(el);
                                    el->history_browsing = 1;
                                }
                                if (history(el->history, &ev, H_FIRST) == 0 && ev.str) {
                                    line_load_history(el, ev.str);
                                    refresh_line(el);
                                }
                            }
                        } else if (seq1 == '6') { /* PgDn */
                            if (el->history) {
                                HistEvent ev;
                                if (!el->history_browsing) {
                                    (void)history_save_current_input(el);
                                    el->history_browsing = 1;
                                }
                                if (history(el->history, &ev, H_LAST) == 0 && ev.str) {
                                    line_load_history(el, ev.str);
                                    refresh_line(el);
                                }
                            }
                        }
                    } else if (seq1 == '1' && seq2 == ';') { /* ESC [ 1 ; <mod> <final> */
                        char mod;
                        char final;
                        if (!read_esc_byte(el, &mod, 80)) continue;
                        if (!read_esc_byte(el, &final, 80)) continue;
                        (void)mod;
                        el->last_cmd_was_kill = 0;
                        el->yank_active = 0;
                        if (final == 'A') {
                            if (el->history) {
                                HistEvent ev;
                                if (!el->history_browsing) {
                                    (void)history_save_current_input(el);
                                    el->history_browsing = 1;
                                }
                                if (history(el->history, &ev, H_PREV) == 0 && ev.str) {
                                    line_load_history(el, ev.str);
                                    refresh_line(el);
                                }
                            }
                        } else if (final == 'B') {
                            if (el->history) {
                                HistEvent ev;
                                if (history(el->history, &ev, H_NEXT) == 0 && ev.str) {
                                    line_load_history(el, ev.str);
                                    refresh_line(el);
                                } else {
                                    history_restore_current_input(el);
                                    el->history_browsing = 0;
                                    refresh_line(el);
                                }
                            }
                        } else if (final == 'C') {
                            if (el->line.cursor < el->line.len) {
                                undo_push(el);
                                el->line.cursor++;
                                refresh_line(el);
                            }
                        } else if (final == 'D') {
                            if (el->line.cursor > 0) {
                                undo_push(el);
                                el->line.cursor--;
                                refresh_line(el);
                            }
                        } else if (final == 'H') {
                            if (el->line.cursor != 0) {
                                undo_push(el);
                                el->line.cursor = 0;
                                refresh_line(el);
                            }
                        } else if (final == 'F') {
                            if (el->line.cursor != el->line.len) {
                                undo_push(el);
                                el->line.cursor = el->line.len;
                                refresh_line(el);
                            }
                        }
                    }
                }
            } else if (seq0 == 'O') { /* SS3 alternate arrows */
                char seq1;
                if (!read_esc_byte(el, &seq1, 80)) continue;
                el->last_cmd_was_kill = 0;
                el->yank_active = 0;
                if (seq1 == 'C') {
                    if (el->line.cursor < el->line.len) {
                        undo_push(el);
                        el->line.cursor++;
                        refresh_line(el);
                    }
                } else if (seq1 == 'D') {
                    if (el->line.cursor > 0) {
                        undo_push(el);
                        el->line.cursor--;
                        refresh_line(el);
                    }
                } else if (seq1 == 'A') {
                    if (el->history) {
                        HistEvent ev;
                        if (!el->history_browsing) {
                            (void)history_save_current_input(el);
                            el->history_browsing = 1;
                        }
                        if (history(el->history, &ev, H_PREV) == 0 && ev.str) {
                            line_load_history(el, ev.str);
                            refresh_line(el);
                        }
                    }
                } else if (seq1 == 'B') {
                    if (el->history) {
                        HistEvent ev;
                        if (history(el->history, &ev, H_NEXT) == 0 && ev.str) {
                            line_load_history(el, ev.str);
                            refresh_line(el);
                        } else {
                            history_restore_current_input(el);
                            el->history_browsing = 0;
                            refresh_line(el);
                        }
                    }
                }
            } else if (seq0 == 'c' || seq0 == 'C') { /* M-c capitalize-word */
                el->last_cmd_was_kill = 0;
                el->yank_active = 0;
                undo_push(el);
                if (apply_word_case(el, 0)) {
                    refresh_line(el);
                } else {
                    undo_discard_last(el);
                }
            } else if (seq0 == 'l' || seq0 == 'L') { /* M-l downcase-word */
                el->last_cmd_was_kill = 0;
                el->yank_active = 0;
                undo_push(el);
                if (apply_word_case(el, -1)) {
                    refresh_line(el);
                } else {
                    undo_discard_last(el);
                }
            } else if (seq0 == 'u' || seq0 == 'U') { /* M-u upcase-word */
                el->last_cmd_was_kill = 0;
                el->yank_active = 0;
                undo_push(el);
                if (apply_word_case(el, 1)) {
                    refresh_line(el);
                } else {
                    undo_discard_last(el);
                }
            } else if (seq0 == '.') { /* M-. yank-last-arg */
                el->last_cmd_was_kill = 0;
                el->yank_active = 0;
                undo_push(el);
                if (yank_last_arg(el)) {
                    refresh_line(el);
                } else {
                    undo_discard_last(el);
                }
            } else if (seq0 == 'd' || seq0 == 'D') { /* M-d kill-word */
                el->yank_active = 0;
                undo_push(el);
                if (kill_word(el)) refresh_line(el);
            } else if (seq0 == 'f' || seq0 == 'F') { /* M-f */
                size_t old_cursor;
                el->last_cmd_was_kill = 0;
                el->yank_active = 0;
                undo_push(el);
                old_cursor = el->line.cursor;
                move_forward_word(el);
                if (el->line.cursor != old_cursor) {
                    refresh_line(el);
                } else {
                    undo_discard_last(el);
                }
            } else if (seq0 == 'b' || seq0 == 'B') { /* M-b */
                size_t old_cursor;
                el->last_cmd_was_kill = 0;
                el->yank_active = 0;
                undo_push(el);
                old_cursor = el->line.cursor;
                move_backward_word(el);
                if (el->line.cursor != old_cursor) {
                    refresh_line(el);
                } else {
                    undo_discard_last(el);
                }
            } else if (seq0 == '<') { /* M-< beginning-of-history */
                el->last_cmd_was_kill = 0;
                el->yank_active = 0;
                if (el->history) {
                    HistEvent ev;
                    if (!el->history_browsing) {
                        (void)history_save_current_input(el);
                        el->history_browsing = 1;
                    }
                    if (history(el->history, &ev, H_FIRST) == 0 && ev.str) {
                        line_load_history(el, ev.str);
                        refresh_line(el);
                    }
                }
            } else if (seq0 == '>') { /* M-> end-of-history */
                el->last_cmd_was_kill = 0;
                el->yank_active = 0;
                if (el->history) {
                    HistEvent ev;
                    if (history(el->history, &ev, H_LAST) == 0 && ev.str) {
                        if (!el->history_browsing) {
                            (void)history_save_current_input(el);
                            el->history_browsing = 1;
                        }
                        line_load_history(el, ev.str);
                    } else {
                        history_restore_current_input(el);
                        el->history_browsing = 0;
                    }
                    refresh_line(el);
                }
            } else if (seq0 == 'y' || seq0 == 'Y') { /* M-y yank-pop */
                el->last_cmd_was_kill = 0;
                undo_push(el);
                if (yank_pop(el)) refresh_line(el);
            } else if (seq0 == 0x08 || seq0 == 0x7F) { /* M-BS or M-DEL */
                el->yank_active = 0;
                undo_push(el);
                if (backward_kill_word(el)) refresh_line(el);
            } else {
                el->last_cmd_was_kill = 0;
                el->yank_active = 0;
            }
        } else if (c >= 32 && c < 127) {
            if (line_ensure_capacity(el, el->line.len + 2) == 0) {
                el->last_cmd_was_kill = 0;
                el->yank_active = 0;
                undo_push(el);
                if (el->overwrite_mode && el->line.cursor < el->line.len) {
                    el->line.buffer[el->line.cursor] = (char)c;
                    el->line.cursor++;
                    refresh_line(el);
                    continue;
                }
                memmove(el->line.buffer + el->line.cursor + 1,
                        el->line.buffer + el->line.cursor,
                        el->line.len - el->line.cursor + 1);
                el->line.buffer[el->line.cursor] = c;
                el->line.len++;
                el->line.cursor++;
                refresh_line(el);
            } else {
                fputc('\a', el->fout);
                fflush(el->fout);
            }
        }
    }

    terminal_set_orig(el);
    return NULL;
}
