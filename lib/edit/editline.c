#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
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

    /* Query initial terminal dimensions and load termcap */
    terminal_get_size(el);
    terminal_init_caps(el);

    /* Allocate and initialize default keymaps */
    el->emacs_keymap = (el_keymap_t *)keymap_alloc();
    el->vi_insert_keymap = (el_keymap_t *)keymap_alloc();
    el->vi_command_keymap = (el_keymap_t *)keymap_alloc();
    el->vi_replace_keymap = (el_keymap_t *)keymap_alloc();
    if (!el->emacs_keymap || !el->vi_insert_keymap ||
        !el->vi_command_keymap || !el->vi_replace_keymap) {
        keymap_free((struct keymap_entry *)el->emacs_keymap);
        keymap_free((struct keymap_entry *)el->vi_insert_keymap);
        keymap_free((struct keymap_entry *)el->vi_command_keymap);
        keymap_free((struct keymap_entry *)el->vi_replace_keymap);
        free(el->line.buffer);
        free(el->prog);
        free(el);
        return NULL;
    }
    keymap_init_emacs(el);
    keymap_init_vi_insert(el);
    keymap_init_vi_command(el);
    keymap_init_vi_replace(el);

    return el;
}

void el_end(EditLine *el) {
    size_t i;

    if (!el) return;
    terminal_set_orig(el);
    terminal_free_caps(el);
    if (el->line.buffer) free(el->line.buffer);
    if (el->render_cache) free(el->render_cache);
    if (el->saved_input) free(el->saved_input);
    if (el->vi_repeat.insert_text) free(el->vi_repeat.insert_text);
    keymap_free((struct keymap_entry *)el->emacs_keymap);
    keymap_free((struct keymap_entry *)el->vi_insert_keymap);
    keymap_free((struct keymap_entry *)el->vi_command_keymap);
    keymap_free((struct keymap_entry *)el->vi_replace_keymap);
    for (i = 0; i < EL_KILL_RING_SIZE; i++) {
        if (el->kill_ring[i]) free(el->kill_ring[i]);
    }
    for (i = 0; i < el->undo_depth; i++) {
        if (el->undo_stack[i].buffer) free(el->undo_stack[i].buffer);
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
    case EL_BIND: {
        const char *key = va_arg(ap, const char *);
        const char *action = va_arg(ap, const char *);
        const char *alt = va_arg(ap, const char *);
        struct keymap_entry *km;
        unsigned char seq[32];
        size_t seqlen;
        const struct action_entry *ae;
        if (!key || !action) { va_end(ap); return -1; }
        /* "-a" flag selects vi alternate (command) keymap */
        if (alt && strcmp(alt, "-a") == 0)
            km = (struct keymap_entry *)el->vi_command_keymap;
        else
            km = (el->editor_mode == ED_VI)
                ? (struct keymap_entry *)el->vi_insert_keymap
                : (struct keymap_entry *)el->emacs_keymap;
        if (keymap_parse_sequence(key, seq, sizeof(seq), &seqlen) != 0 || seqlen == 0)
            { va_end(ap); return -1; }
        ae = el_find_action(el, action);
        if (!ae) { va_end(ap); return -1; }
        if (keymap_bind_sequence(km, seq, seqlen, ae->func) != 0)
            { va_end(ap); return -1; }
        break;
    }
    case EL_ADDFN: {
        const char *name = va_arg(ap, const char *);
        const char *help = va_arg(ap, const char *);
        el_action_t func = va_arg(ap, el_action_t);
        if (!name || !func || el->n_user_actions >= EL_MAX_USER_ACTIONS)
            { va_end(ap); return -1; }
        el->user_actions[el->n_user_actions].name = name;
        el->user_actions[el->n_user_actions].help = help;
        el->user_actions[el->n_user_actions].func = func;
        el->n_user_actions++;
        break;
    }
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
    if (!el || !el->fout) return -1;

    /* Re-query terminal dimensions */
    terminal_get_size(el);

    /* Force full redraw */
    terminal_printf(el, "\r\033[K%s%s", el->prompt ? el->prompt : "", el->line.buffer);
    if (el->line.cursor < el->line.len) {
        int back = (int)(el->line.len - el->line.cursor);
        terminal_printf(el, "\033[%dD", back);
    }
    terminal_flush(el);

    return 0;
}
