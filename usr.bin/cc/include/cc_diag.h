#ifndef CC_DIAG_H
#define CC_DIAG_H

/*
 * Diagnostics: recording an error in a cc_diag_t and printing errors and
 * warnings as "file:line:col: error: message", with the source line
 * and a caret under the column for errors.
 */

#include "cc_frontend.h"

#include <stddef.h>

/**
 * Record an error unless one is already recorded: the first error is
 * the one reported.
 *
 * @param d     the diagnostic; NULL is allowed and ignored
 * @param path  the source file, or NULL to leave the recorded path as is
 * @param line  the line, or 0 if unknown
 * @param col   the column, or 0 if unknown
 * @param msg   the message; one longer than the buffer is cut to fit
 */
void cc_diag_set(cc_diag_t *d, const char *path, size_t line, size_t col, const char *msg);

/**
 * Record an error, replacing any recorded already.
 *
 * @param d     the diagnostic; NULL is allowed and ignored
 * @param path  the source file, or NULL to leave the recorded path as is
 * @param line  the line, or 0 if unknown
 * @param col   the column, or 0 if unknown
 * @param msg   the message; one longer than the buffer is cut to fit
 */
void cc_diag_replace(cc_diag_t *d, const char *path, size_t line, size_t col, const char *msg);

/**
 * Forget the recorded error and its position.  The path is kept: it
 * names the file being compiled, which later errors share.
 *
 * @param d  the diagnostic; NULL is allowed and ignored
 */
void cc_diag_clear(cc_diag_t *d);

/**
 * Print one diagnostic to stderr: "path:line:col: kind: msg", leaving out
 * what is unknown, and "cc" in place of a missing path.
 *
 * @param kind         "error" or "warning"
 * @param path         the source file, or NULL or ""
 * @param line         the line, or 0
 * @param col          the column, or 0
 * @param msg          the message
 * @param show_source  nonzero to follow it with the source line and a
 *                     caret under the column, when the file can be read
 */
void cc_diag_print(const char *kind, const char *path, size_t line, size_t col, const char *msg, int show_source);

/**
 * Print the recorded error, if there is one, count it in
 * d->error_count, and clear it.
 *
 * @param d  the diagnostic; NULL is allowed and ignored
 */
void cc_diag_report(cc_diag_t *d);

#endif
