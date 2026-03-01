#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <sys/ioctl.h>
#include "el.h"

#define EL_LINE_DEFAULT_CAP 1024U
#define EL_LINE_MAX_CAP (1024U * 1024U)

int line_ensure_capacity(EditLine *el, size_t needed) {
    char *new_buf;
    size_t new_cap;

    if (!el) return -1;
    if (needed <= el->line.cap) return 0;
    if (needed > EL_LINE_MAX_CAP) return -1;

    new_cap = el->line.cap ? el->line.cap : EL_LINE_DEFAULT_CAP;
    while (new_cap < needed) {
        if (new_cap >= (EL_LINE_MAX_CAP / 2U)) {
            new_cap = EL_LINE_MAX_CAP;
        } else {
            new_cap *= 2U;
        }
    }

    new_buf = realloc(el->line.buffer, new_cap);
    if (!new_buf) return -1;

    el->line.buffer = new_buf;
    el->line.cap = new_cap;
    return 0;
}

EditLine *el_init(const char *prog, FILE *fin, FILE *fout, FILE *ferr) {
    EditLine *el = calloc(1, sizeof(EditLine));
    if (!el) return NULL;

    el->prog = strdup(prog ? prog : "editline");
    if (!el->prog) {
        free(el);
        return NULL;
    }

    el->fin = fin;
    el->fout = fout;
    el->ferr = ferr;

    /* Initialize line buffer */
    el->line.cap = EL_LINE_DEFAULT_CAP;
    el->line.buffer = malloc(el->line.cap);
    if (!el->line.buffer) {
        free(el->prog);
        free(el);
        return NULL;
    }
    el->line.buffer[0] = '\0';
    el->line.len = 0;
    el->line.cursor = 0;
    el->editor_mode = ED_EMACS;

    return el;
}

void el_end(EditLine *el) {
    size_t i;

    if (!el) return;
    terminal_set_orig(el);
    if (el->line.buffer) free(el->line.buffer);
    if (el->render_cache) free(el->render_cache);
    for (i = 0; i < EL_KILL_RING_SIZE; i++) {
        if (el->kill_ring[i]) free(el->kill_ring[i]);
    }
    if (el->prog) free(el->prog);
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
    case EL_RPROMPT:
        el->rprompt = va_arg(ap, const char *);
        break;
    case EL_EDITOR: {
        const char *mode = va_arg(ap, const char *);
        if (mode && strcmp(mode, "vi") == 0) {
            el->editor_mode = ED_VI;
        } else {
            el->editor_mode = ED_EMACS;
        }
        break;
    }
    case EL_SIGNAL:
        el->signal_state.active = va_arg(ap, int) ? 1 : 0;
        break;
    case EL_CLIENTDATA:
        el->client_data = va_arg(ap, void *);
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
    case EL_RPROMPT:
        *(va_arg(ap, const char **)) = el->rprompt;
        break;
    case EL_EDITOR:
        *(va_arg(ap, const char **)) = (el->editor_mode == ED_VI) ? "vi" : "emacs";
        break;
    case EL_SIGNAL:
        *(va_arg(ap, int *)) = el->signal_state.active;
        break;
    case EL_CLIENTDATA:
        *(va_arg(ap, void **)) = el->client_data;
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

int el_resize(EditLine *el) {
    struct winsize ws;

    if (!el || !el->fout) return -1;

    (void)ioctl(fileno(el->fout), TIOCGWINSZ, &ws);

    fprintf(el->fout, "\r\033[K%s%s", el->prompt ? el->prompt : "", el->line.buffer);
    if (el->line.cursor < el->line.len) {
        int back = (int)(el->line.len - el->line.cursor);
        fprintf(el->fout, "\033[%dD", back);
    }
    fflush(el->fout);

    return 0;
}
