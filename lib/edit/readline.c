#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include "el.h"

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
            fprintf(el->fout, "\r\n");
            terminal_set_orig(el);
            if (count) *count = el->line.len;
            return el->line.buffer;
        } else if (c == 0x7F || c == 0x08) { /* Backspace */
            if (el->line.cursor > 0) {
                memmove(el->line.buffer + el->line.cursor - 1,
                        el->line.buffer + el->line.cursor,
                        el->line.len - el->line.cursor + 1);
                el->line.len--;
                el->line.cursor--;
                refresh_line(el);
            }
        } else if (c == 0x04) { /* ^D - EOF */
            if (el->line.len == 0) {
                terminal_set_orig(el);
                return NULL;
            }
            /* If not empty, maybe delete-char? Standard editline often treats ^D as delete-char if not empty */
            if (el->line.cursor < el->line.len) {
                memmove(el->line.buffer + el->line.cursor,
                        el->line.buffer + el->line.cursor + 1,
                        el->line.len - el->line.cursor);
                el->line.len--;
                refresh_line(el);
            }
        } else if (c == 0x03) { /* ^C - Clear line */
            fprintf(el->fout, "^C\r\n");
            el_reset(el);
            if (el->prompt) {
                fprintf(el->fout, "%s", el->prompt);
                fflush(el->fout);
            }
        } else if (c == 0x0C) { /* ^L - Clear screen */
            fprintf(el->fout, "\033[H\033[2J");
            refresh_line(el);
        } else if (c == 0x1B) { /* ESC */
            char seq0;
            char seq1;
            if (read(fileno(el->fin), &seq0, 1) == 1 &&
                read(fileno(el->fin), &seq1, 1) == 1) {
                if (seq0 == '[') {
                    if (seq1 == 'C') { /* Right */
                        if (el->line.cursor < el->line.len) {
                            el->line.cursor++;
                            refresh_line(el);
                        }
                    } else if (seq1 == 'D') { /* Left */
                        if (el->line.cursor > 0) {
                            el->line.cursor--;
                            refresh_line(el);
                        }
                    } else if (seq1 == 'A') { /* Up (History) */
                        if (el->history) {
                            HistEvent ev;
                            if (history(el->history, &ev, H_PREV) == 0 && ev.str) {
                                line_load_history(el, ev.str);
                                refresh_line(el);
                            }
                        }
                    } else if (seq1 == 'B') { /* Down (History) */
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
                            if (seq1 == '2') {
                                el->overwrite_mode = !el->overwrite_mode;
                            }
                        }
                    }
                }
            }
        } else if (c >= 32 && c < 127) {
            if (line_ensure_capacity(el, el->line.len + 2) == 0) {
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
