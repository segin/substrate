#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include "el.h"

static int is_word_char(unsigned char ch) {
    return isalnum(ch) || ch == '_';
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

static void refresh_line(EditLine *el) {
    /* Move cursor to start of line, clear to end of line, print prompt and buffer */
    fprintf(el->fout, "\r\033[K%s%s", el->prompt ? el->prompt : "", el->line.buffer);
    
    /* Move cursor to correct position */
    if (el->line.cursor < el->line.len) {
        int back = el->line.len - el->line.cursor;
        fprintf(el->fout, "\033[%dD", back);
    }
    fflush(el->fout);
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

const char *el_gets(EditLine *el, int *count) {
    if (count) *count = 0;
    if (terminal_set_raw(el) == -1) return NULL;

    el->line.len = 0;
    el->line.cursor = 0;
    el->line.buffer[0] = '\0';

    if (el->prompt) {
        fprintf(el->fout, "%s", el->prompt);
        fflush(el->fout);
    }

    int c;
    while (read(fileno(el->fin), &c, 1) == 1) {
        c &= 0xFF;

        if (c == '\n' || c == '\r') {
            el->last_cmd_was_kill = 0;
            fprintf(el->fout, "\r\n");
            terminal_set_orig(el);
            if (count) *count = el->line.len;
            return el->line.buffer;
        } else if (c == 0x7F || c == 0x08) { /* Backspace */
            if (el->line.cursor > 0) {
                el->last_cmd_was_kill = 0;
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
                terminal_set_orig(el);
                return NULL;
            }
            if (el->line.cursor < el->line.len) {
                el->last_cmd_was_kill = 0;
                memmove(el->line.buffer + el->line.cursor,
                        el->line.buffer + el->line.cursor + 1,
                        el->line.len - el->line.cursor);
                el->line.len--;
                refresh_line(el);
            } else if (el->completion) {
                el->completion(el, c);
                refresh_line(el);
            }
        } else if (c == 0x03) { /* ^C - Clear line */
            el->last_cmd_was_kill = 0;
            fprintf(el->fout, "^C\r\n");
            el_reset(el);
            if (el->prompt) {
                fprintf(el->fout, "%s", el->prompt);
                fflush(el->fout);
            }
        } else if (c == 0x0C) { /* ^L - Clear screen */
            el->last_cmd_was_kill = 0;
            fprintf(el->fout, "\033[H\033[2J");
            refresh_line(el);
        } else if (c == 0x01) { /* ^A */
            el->last_cmd_was_kill = 0;
            el->line.cursor = 0;
            refresh_line(el);
        } else if (c == 0x05) { /* ^E */
            el->last_cmd_was_kill = 0;
            el->line.cursor = el->line.len;
            refresh_line(el);
        } else if (c == 0x0B) { /* ^K kill-line */
            if (kill_line(el)) refresh_line(el);
        } else if (c == 0x15) { /* ^U backward-kill-line */
            if (backward_kill_line(el)) refresh_line(el);
        } else if (c == 0x1B) { /* ESC */
            char seq0;
            if (read(fileno(el->fin), &seq0, 1) == 1) {
                if (seq0 == '[') {
                    char seq1;
                    if (read(fileno(el->fin), &seq1, 1) != 1) continue;
                    if (seq1 == 'C') { /* Right */
                        el->last_cmd_was_kill = 0;
                        if (el->line.cursor < el->line.len) {
                            el->line.cursor++;
                            refresh_line(el);
                        }
                    } else if (seq1 == 'D') { /* Left */
                        el->last_cmd_was_kill = 0;
                        if (el->line.cursor > 0) {
                            el->line.cursor--;
                            refresh_line(el);
                        }
                    } else if (seq1 == 'H') { /* Home */
                        el->last_cmd_was_kill = 0;
                        el->line.cursor = 0;
                        refresh_line(el);
                    } else if (seq1 == 'F') { /* End */
                        el->last_cmd_was_kill = 0;
                        el->line.cursor = el->line.len;
                        refresh_line(el);
                    } else if (seq1 == 'A') { /* Up (History) */
                        el->last_cmd_was_kill = 0;
                        if (el->history) {
                            HistEvent ev;
                            if (history(el->history, &ev, H_PREV) == 0 && ev.str) {
                                line_load_history(el, ev.str);
                                refresh_line(el);
                            }
                        }
                    } else if (seq1 == 'B') { /* Down (History) */
                        el->last_cmd_was_kill = 0;
                        if (el->history) {
                            HistEvent ev;
                            if (history(el->history, &ev, H_NEXT) == 0 && ev.str) {
                                line_load_history(el, ev.str);
                                refresh_line(el);
                            } else {
                                el_reset(el);
                                refresh_line(el);
                            }
                        }
                    } else if (seq1 >= '0' && seq1 <= '9') {
                        char seq2;
                        if (read(fileno(el->fin), &seq2, 1) == 1 && seq2 == '~') {
                            el->last_cmd_was_kill = 0;
                            if (seq1 == '2') {
                                el->overwrite_mode = !el->overwrite_mode;
                            } else if (seq1 == '1') {
                                el->line.cursor = 0;
                                refresh_line(el);
                            } else if (seq1 == '4') {
                                el->line.cursor = el->line.len;
                                refresh_line(el);
                            }
                        }
                    }
                } else if (seq0 == 'd' || seq0 == 'D') { /* M-d kill-word */
                    if (kill_word(el)) refresh_line(el);
                } else if (seq0 == 'f' || seq0 == 'F') { /* M-f */
                    el->last_cmd_was_kill = 0;
                    move_forward_word(el);
                    refresh_line(el);
                } else if (seq0 == 'b' || seq0 == 'B') { /* M-b */
                    el->last_cmd_was_kill = 0;
                    move_backward_word(el);
                    refresh_line(el);
                } else if (seq0 == 0x08 || seq0 == 0x7F) { /* M-BS or M-DEL */
                    if (backward_kill_word(el)) refresh_line(el);
                } else {
                    el->last_cmd_was_kill = 0;
                }
            }
        } else if (c >= 32 && c < 127) {
            if (line_ensure_capacity(el, el->line.len + 2) == 0) {
                el->last_cmd_was_kill = 0;
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
