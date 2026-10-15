/*
 * diag.c: recording and printing diagnostics, for every phase of cc.
 */

#include "cc_diag.h"

#include <stdio.h>
#include <string.h>

/**
 * Store an error in a diagnostic, whatever it held.
 *
 * @param d     the diagnostic
 * @param path  the source file, or NULL to keep the recorded one
 * @param line  the line, or 0
 * @param col   the column, or 0
 * @param msg   the message, cut to fit
 */
static void diag_store(cc_diag_t *d, const char *path, size_t line, size_t col, const char *msg) {
    if (path != NULL) {
        snprintf(d->path, sizeof(d->path), "%s", path);
    }
    d->line = line;
    d->col = col;
    snprintf(d->message, sizeof(d->message), "%.*s", (int)sizeof(d->message) - 1, msg != NULL ? msg : "");
}

/**
 * Record an error unless one is already recorded.
 *
 * @param d     the diagnostic; NULL is ignored
 * @param path  the source file, or NULL to keep the recorded one
 * @param line  the line, or 0 if unknown
 * @param col   the column, or 0 if unknown
 * @param msg   the message
 */
void cc_diag_set(cc_diag_t *d, const char *path, size_t line, size_t col, const char *msg) {
    if (d == NULL || d->message[0] != '\0') {
        return;
    }
    diag_store(d, path, line, col, msg);
}

/**
 * Record an error, replacing any recorded already.
 *
 * @param d     the diagnostic; NULL is ignored
 * @param path  the source file, or NULL to keep the recorded one
 * @param line  the line, or 0 if unknown
 * @param col   the column, or 0 if unknown
 * @param msg   the message
 */
void cc_diag_replace(cc_diag_t *d, const char *path, size_t line, size_t col, const char *msg) {
    if (d == NULL) {
        return;
    }
    diag_store(d, path, line, col, msg);
}

/**
 * Forget the recorded error and its position, keeping the path.
 *
 * @param d  the diagnostic; NULL is ignored
 */
void cc_diag_clear(cc_diag_t *d) {
    if (d == NULL) {
        return;
    }
    d->line = 0;
    d->col = 0;
    d->message[0] = '\0';
}

/**
 * Print a source line and a caret under a column, indented four spaces.
 * The line is read whole whatever its length; tabs before the column are
 * kept so that the caret lines up.
 *
 * @param path  the file
 * @param line  the line, from 1
 * @param col   the column, from 1 (0 is taken as 1)
 */
static void print_source_line(const char *path, size_t line, size_t col) {
    FILE *fp = fopen(path, "r");
    size_t cur = 1;
    size_t pos = 0;
    size_t caret = col > 0 ? col : 1;
    char tabs[256];
    int c;

    if (fp == NULL) {
        return;
    }
    while (cur < line && (c = getc(fp)) != EOF) {
        if (c == '\n') {
            cur++;
        }
    }
    if (cur != line) {
        fclose(fp);
        return;
    }
    /* the line itself; remember which of the first columns are tabs */
    memset(tabs, 0, sizeof(tabs));
    fputs("    ", stderr);
    while ((c = getc(fp)) != EOF && c != '\n') {
        if (c == '\r') {
            continue;
        }
        if (pos < sizeof(tabs)) {
            tabs[pos] = c == '\t';
        }
        pos++;
        fputc(c, stderr);
    }
    fputs("\n    ", stderr);
    for (cur = 1; cur < caret; ++cur) {
        fputc(cur - 1 < sizeof(tabs) && tabs[cur - 1] ? '\t' : ' ', stderr);
    }
    fputs("^\n", stderr);
    fclose(fp);
}

/**
 * Print one diagnostic to stderr.
 *
 * @param kind         "error" or "warning"
 * @param path         the source file, or NULL or ""
 * @param line         the line, or 0
 * @param col          the column, or 0
 * @param msg          the message
 * @param show_source  nonzero to follow it with the source line and caret
 */
void cc_diag_print(const char *kind, const char *path, size_t line, size_t col, const char *msg, int show_source) {
    int have_path = path != NULL && path[0] != '\0';
    const char *where = have_path ? path : "cc";

    if (line != 0) {
        fprintf(stderr, "%s:%zu:%zu: %s: %s\n", where, line, col, kind, msg);
        if (show_source && have_path) {
            print_source_line(path, line, col);
        }
    } else {
        fprintf(stderr, "%s: %s: %s\n", where, kind, msg);
    }
}

/**
 * Print the recorded error, count it, and clear it.
 *
 * @param d  the diagnostic; NULL is ignored
 */
void cc_diag_report(cc_diag_t *d) {
    if (d == NULL || d->message[0] == '\0') {
        return;
    }
    cc_diag_print("error", d->path, d->line, d->col, d->message, 1);
    d->error_count++;
    cc_diag_clear(d);
}
