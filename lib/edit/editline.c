#include <stdlib.h>
#include <stdarg.h>
#include "el.h"

EditLine *el_init(const char *prog, FILE *fin, FILE *fout, FILE *ferr) {
    EditLine *el = calloc(1, sizeof(EditLine));
    if (!el) return NULL;

    el->fin = fin;
    el->fout = fout;
    el->ferr = ferr;

    /* Initialize line buffer */
    el->line.cap = 1024;
    el->line.buffer = malloc(el->line.cap);
    if (!el->line.buffer) {
        free(el);
        return NULL;
    }
    el->line.buffer[0] = '\0';
    el->line.len = 0;
    el->line.cursor = 0;

    return el;
}

void el_end(EditLine *el) {
    if (!el) return;
    terminal_set_orig(el);
    if (el->line.buffer) free(el->line.buffer);
    free(el);
}

void el_reset(EditLine *el) {
    if (!el) return;
    el->line.len = 0;
    el->line.cursor = 0;
    el->line.buffer[0] = '\0';
}

int el_set(EditLine *el, int op, ...) {
    va_list ap;
    va_start(ap, op);

    switch (op) {
    case EL_PROMPT:
        el->prompt = va_arg(ap, const char *);
        break;
    case EL_HIST:
        va_arg(ap, void *); /* function pointer */
        el->history = va_arg(ap, History *);
        break;
    default:
        va_end(ap);
        return -1;
    }

    va_end(ap);
    return 0;
}

int el_get(EditLine *el, int op, ...) {
    va_list ap;
    va_start(ap, op);

    switch (op) {
    case EL_PROMPT:
        *(va_arg(ap, const char **)) = el->prompt;
        break;
    default:
        va_end(ap);
        return -1;
    }

    va_end(ap);
    return 0;
}

const LineInfo *el_line(EditLine *el) {
    el->line.info.buffer = el->line.buffer;
    el->line.info.cursor = el->line.buffer + el->line.cursor;
    el->line.info.lastchar = el->line.buffer + el->line.len;
    return &el->line.info;
}
