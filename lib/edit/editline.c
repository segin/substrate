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
    el->editing_enabled = 1; /* editing on by default */

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
    case EL_PROMPT_ESC: {
        const char *(*fn)(EditLine *) =
            va_arg(ap, const char *(*)(EditLine *));
        char esc = (char)va_arg(ap, int);
        el->prompt_func = fn;
        el->prompt_esc_char = esc;
        break;
    }
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
    case EL_ECHOTC: {
        const char *cap = va_arg(ap, const char *);
        if (!cap) { va_end(ap); return -1; }
        /* Echo a termcap string to the terminal */
        terminal_puts(el, cap);
        terminal_flush(el);
        break;
    }
    case EL_SETTC: {
        /* Stub: accept and ignore termcap variable set */
        va_arg(ap, const char *); /* cap name */
        va_arg(ap, const char *); /* value */
        break;
    }
    case EL_REFRESH:
        /* Force screen refresh */
        terminal_printf(el, "\r\033[K%s%s", el->prompt ? el->prompt : "",
                        el->line.buffer);
        if (el->line.cursor < el->line.len) {
            int back = (int)(el->line.len - el->line.cursor);
            terminal_printf(el, "\033[%dD", back);
        }
        terminal_flush(el);
        break;
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
    case EL_SETFN: {
        const char *name = va_arg(ap, const char *);
        el_action_t func = va_arg(ap, el_action_t);
        int i;
        if (!name || !func) { va_end(ap); return -1; }
        /* Search user actions for a match and replace */
        for (i = 0; i < el->n_user_actions; i++) {
            if (strcmp(el->user_actions[i].name, name) == 0) {
                el->user_actions[i].func = func;
                va_end(ap);
                return 0;
            }
        }
        va_end(ap);
        return -1; /* name not found */
    }
    case EL_SETTY:
        /* Stub: accept and ignore tty mode settings */
        break;
    case EL_GETFP: {
        int which = va_arg(ap, int);
        FILE **fp = va_arg(ap, FILE **);
        if (!fp) { va_end(ap); return -1; }
        switch (which) {
        case 0: *fp = el->fin;  break;
        case 1: *fp = el->fout; break;
        case 2: *fp = el->ferr; break;
        default: va_end(ap); return -1;
        }
        break;
    }
    case EL_SETFP: {
        int which = va_arg(ap, int);
        FILE *fp = va_arg(ap, FILE *);
        if (!fp) { va_end(ap); return -1; }
        switch (which) {
        case 0: el->fin  = fp; break;
        case 1: el->fout = fp; break;
        case 2: el->ferr = fp; break;
        default: va_end(ap); return -1;
        }
        break;
    }
    case EL_EDITMODE:
        el->editing_enabled = va_arg(ap, int) ? 1 : 0;
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
    case EL_GETFP: {
        int which = va_arg(ap, int);
        FILE **fp = va_arg(ap, FILE **);
        if (!fp) { va_end(ap); return -1; }
        switch (which) {
        case 0: *fp = el->fin;  break;
        case 1: *fp = el->fout; break;
        case 2: *fp = el->ferr; break;
        default: va_end(ap); return -1;
        }
        break;
    }
    case EL_EDITMODE:
        *(va_arg(ap, int *)) = el->editing_enabled;
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

/*
 * el_source - read and execute .editrc commands.
 * If file is NULL, defaults to ~/.editrc.
 * Supports: bind, echotc, edit, settc, setty commands.
 * Supports per-program sections: "prog:command args".
 * Ignores comment lines (#) and blank lines.
 */
int el_source(EditLine *el, const char *file)
{
    FILE *fp;
    char path[1024];
    char buf[1024];
    int lineno = 0;
    int errors = 0;

    if (!el) return -1;

    if (!file) {
        const char *home = getenv("HOME");
        if (!home) return -1;
        if (snprintf(path, sizeof(path), "%s/.editrc", home) < 0)
            return -1;
        file = path;
    }

    fp = fopen(file, "r");
    if (!fp) return -1;

    while (fgets(buf, (int)sizeof(buf), fp)) {
        char *line, *cmd, *args;
        size_t len;

        lineno++;
        line = buf;

        /* Strip trailing newline */
        len = strlen(line);
        if (len > 0 && line[len - 1] == '\n')
            line[--len] = '\0';
        if (len > 0 && line[len - 1] == '\r')
            line[--len] = '\0';

        /* Skip blank lines and comments */
        while (*line == ' ' || *line == '\t') line++;
        if (*line == '\0' || *line == '#')
            continue;

        /* Check for per-program section: "prog:command" */
        cmd = strchr(line, ':');
        if (cmd) {
            size_t plen = (size_t)(cmd - line);
            /* If prog doesn't match, skip this line */
            if (strncmp(line, el->prog, plen) != 0 ||
                el->prog[plen] != '\0') {
                continue;
            }
            line = cmd + 1;
            while (*line == ' ' || *line == '\t') line++;
        }

        /* Parse command word */
        cmd = line;
        args = line;
        while (*args && *args != ' ' && *args != '\t') args++;
        if (*args) {
            *args++ = '\0';
            while (*args == ' ' || *args == '\t') args++;
        }

        if (strcmp(cmd, "bind") == 0) {
            /*
             * bind [-a] [-e] [-v] [-s] [key [command]]
             */
            const char *key_str = NULL;
            const char *action_str = NULL;
            const char *alt_flag = NULL;
            char *p = args;

            /* Parse flags */
            while (*p == '-') {
                if (p[1] == 'a') { alt_flag = "-a"; }
                else if (p[1] == 'e') { el->editor_mode = ED_EMACS; }
                else if (p[1] == 'v') { el->editor_mode = ED_VI; }
                /* -s flag: ignored for now */
                p += 2;
                while (*p == ' ' || *p == '\t') p++;
            }

            /* Parse key string (may be quoted) */
            if (*p) {
                if (*p == '"') {
                    key_str = ++p;
                    while (*p && *p != '"') p++;
                    if (*p == '"') *p++ = '\0';
                } else {
                    key_str = p;
                    while (*p && *p != ' ' && *p != '\t') p++;
                    if (*p) *p++ = '\0';
                }
                while (*p == ' ' || *p == '\t') p++;
            }

            /* Parse action name */
            if (*p) {
                action_str = p;
                /* Trim trailing whitespace */
                len = strlen(action_str);
                while (len > 0 && (action_str[len-1] == ' ' ||
                       action_str[len-1] == '\t'))
                    ((char *)action_str)[--len] = '\0';
            }

            if (key_str && action_str) {
                if (el_set(el, EL_BIND, key_str, action_str, alt_flag) != 0) {
                    if (el->ferr)
                        fprintf(el->ferr, "%s:%d: bind failed\n",
                                file, lineno);
                    errors++;
                }
            }
        } else if (strcmp(cmd, "echotc") == 0) {
            el_set(el, EL_ECHOTC, args);
        } else if (strcmp(cmd, "edit") == 0) {
            if (strcmp(args, "off") == 0)
                el->editing_enabled = 0;
            else
                el->editing_enabled = 1;
        } else if (strcmp(cmd, "settc") == 0) {
            char *val = args;
            while (*val && *val != ' ' && *val != '\t') val++;
            if (*val) {
                *val++ = '\0';
                while (*val == ' ' || *val == '\t') val++;
            }
            el_set(el, EL_SETTC, args, val);
        } else if (strcmp(cmd, "setty") == 0) {
            el_set(el, EL_SETTY);
        } else {
            if (el->ferr)
                fprintf(el->ferr, "%s:%d: unknown command: %s\n",
                        file, lineno, cmd);
            errors++;
        }
    }

    fclose(fp);
    return errors ? -1 : 0;
}
