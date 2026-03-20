/*
 * keymap.c — Key map subsystem for the editline library.
 *
 * Provides:
 *   - 256-entry keymap tables mapping byte → action function or sub-keymap
 *   - Multi-byte key sequence support via chained keymaps
 *   - Default keymap initialization for emacs, vi-insert, vi-command
 *   - Key sequence notation parser (^A, \e, \M-, \C-, \e[A)
 *   - Keymap dispatch with timeout-based escape disambiguation
 */
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "el.h"

/* ------------------------------------------------------------------ */
/* Keymap allocation and freeing                                      */
/* ------------------------------------------------------------------ */

struct keymap_entry *keymap_alloc(void) {
    struct keymap_entry *km = calloc(256, sizeof(struct keymap_entry));
    return km; /* All entries KM_UNBOUND (type=0) */
}

static void keymap_free_recursive(struct keymap_entry *km) {
    int i;
    if (!km) return;
    for (i = 0; i < 256; i++) {
        if (km[i].type == KM_SUBMAP && km[i].val.submap)
            keymap_free_recursive(km[i].val.submap);
    }
    free(km);
}

void keymap_free(struct keymap_entry *km) {
    keymap_free_recursive(km);
}

/* ------------------------------------------------------------------ */
/* Binding helpers                                                    */
/* ------------------------------------------------------------------ */

void keymap_bind_func(struct keymap_entry *km, int ch, el_action_t func) {
    struct keymap_entry *entry;
    if (!km || ch < 0 || ch > 255) return;
    entry = &km[ch & 0xFF];
    /* If replacing a submap, free it */
    if (entry->type == KM_SUBMAP && entry->val.submap)
        keymap_free_recursive(entry->val.submap);
    entry->type = KM_FUNC;
    entry->val.func = func;
}

void keymap_bind_submap(struct keymap_entry *km, int ch, struct keymap_entry *sub) {
    struct keymap_entry *entry;
    if (!km || ch < 0 || ch > 255) return;
    entry = &km[ch & 0xFF];
    if (entry->type == KM_SUBMAP && entry->val.submap && entry->val.submap != sub)
        keymap_free_recursive(entry->val.submap);
    entry->type = KM_SUBMAP;
    entry->val.submap = sub;
}

/*
 * Ensure a sub-keymap exists at km[ch]; create one if needed.
 * Returns the sub-keymap pointer.
 */
static struct keymap_entry *keymap_ensure_submap(struct keymap_entry *km, int ch) {
    struct keymap_entry *entry = &km[ch & 0xFF];
    if (entry->type != KM_SUBMAP || !entry->val.submap) {
        struct keymap_entry *sub = keymap_alloc();
        if (!sub) return NULL;
        if (entry->type == KM_SUBMAP && entry->val.submap)
            keymap_free_recursive(entry->val.submap);
        entry->type = KM_SUBMAP;
        entry->val.submap = sub;
    }
    return entry->val.submap;
}

/* ------------------------------------------------------------------ */
/* Key sequence notation parser                                       */
/* ------------------------------------------------------------------ */

/*
 * Parse a key sequence string into raw bytes.
 * Understands: ^A (Ctrl-A), \e (ESC), \M-x (Meta = ESC + x),
 * \C-x (Ctrl-x), \e[A (arrow keys), literal characters.
 * Returns 0 on success, -1 on error.
 */
int keymap_parse_sequence(const char *str, unsigned char *out,
                          size_t outsz, size_t *outlen) {
    const char *p = str;
    size_t n = 0;

    if (!str || !out || !outlen) return -1;

    while (*p && n < outsz) {
        if (*p == '^' && *(p + 1)) {
            /* ^X = Ctrl-X */
            p++;
            out[n++] = (unsigned char)(*p & 0x1F);
            p++;
        } else if (*p == '\\') {
            p++;
            switch (*p) {
            case 'e': case 'E':
                out[n++] = 0x1B; /* ESC */
                p++;
                break;
            case 'M':
                if (*(p + 1) == '-' && *(p + 2)) {
                    /* \M-x = ESC + x */
                    p += 2;
                    out[n++] = 0x1B;
                    if (*p == '\\' && *(p + 1) == 'C' && *(p + 2) == '-' && *(p + 3)) {
                        /* \M-\C-x = ESC + Ctrl-x */
                        p += 3;
                        out[n++] = (unsigned char)(*p & 0x1F);
                        p++;
                    } else {
                        out[n++] = (unsigned char)*p;
                        p++;
                    }
                } else {
                    out[n++] = (unsigned char)*p;
                    p++;
                }
                break;
            case 'C':
                if (*(p + 1) == '-' && *(p + 2)) {
                    /* \C-x = Ctrl-x */
                    p += 2;
                    out[n++] = (unsigned char)(*p & 0x1F);
                    p++;
                } else {
                    out[n++] = (unsigned char)*p;
                    p++;
                }
                break;
            case 'n': out[n++] = '\n'; p++; break;
            case 'r': out[n++] = '\r'; p++; break;
            case 't': out[n++] = '\t'; p++; break;
            case '\\': out[n++] = '\\'; p++; break;
            case '[':
                out[n++] = '[';
                p++;
                break;
            default:
                if (*p >= '0' && *p <= '7') {
                    unsigned v = 0;
                    int i;
                    for (i = 0; i < 3 && *p >= '0' && *p <= '7'; i++, p++)
                        v = (v << 3) | (unsigned)(*p - '0');
                    out[n++] = (unsigned char)v;
                } else if (*p) {
                    out[n++] = (unsigned char)*p;
                    p++;
                }
                break;
            }
        } else {
            out[n++] = (unsigned char)*p;
            p++;
        }
    }

    *outlen = n;
    return 0;
}

/* ------------------------------------------------------------------ */
/* Keymap dispatch                                                    */
/* ------------------------------------------------------------------ */

unsigned char keymap_dispatch(EditLine *el, struct keymap_entry *km, int ch) {
    struct keymap_entry *entry;

    if (!el || !km) return CC_NORM;

    entry = &km[ch & 0xFF];

    if (entry->type == KM_FUNC && entry->val.func)
        return entry->val.func(el, ch);

    if (entry->type == KM_SUBMAP && entry->val.submap) {
        char next;
        if (!el_read_esc_byte(el, &next, 80))
            return CC_NORM;  /* Timeout: standalone ESC */
        return keymap_dispatch(el, entry->val.submap, (unsigned char)next);
    }

    return CC_NORM;  /* Unbound key */
}

/* ------------------------------------------------------------------ */
/* Bind a multi-byte key sequence into a keymap                       */
/* ------------------------------------------------------------------ */

int keymap_bind_sequence(struct keymap_entry *km, const unsigned char *seq,
                         size_t seqlen, el_action_t func) {
    size_t i;
    struct keymap_entry *cur = km;

    if (!km || !seq || seqlen == 0) return -1;

    /* Walk/create sub-keymaps for all but the last byte */
    for (i = 0; i < seqlen - 1; i++) {
        cur = keymap_ensure_submap(cur, seq[i]);
        if (!cur) return -1;
    }

    /* Bind the final byte */
    if (func) {
        keymap_bind_func(cur, seq[seqlen - 1], func);
    } else {
        /* Unbind */
        struct keymap_entry *entry = &cur[seq[seqlen - 1] & 0xFF];
        if (entry->type == KM_SUBMAP && entry->val.submap)
            keymap_free_recursive(entry->val.submap);
        entry->type = KM_UNBOUND;
        entry->val.func = NULL;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Default keymap initialization (uses action registry from readline.c)*/
/* ------------------------------------------------------------------ */

void keymap_init_emacs(EditLine *el) {
    int count, i;
    const struct action_entry *actions = el_builtin_actions(&count);
    struct keymap_entry *km, *esc, *csi, *ss3;

    if (!el || !el->emacs_keymap) return;
    km = (struct keymap_entry *)el->emacs_keymap;

    /* Helper: find action by name */
    #define FIND(name) NULL
    /* We'll use a local lookup */
    el_action_t fn;

    #undef FIND
    #define BIND(key, aname) do { \
        for (i = 0; i < count; i++) { \
            if (strcmp(actions[i].name, aname) == 0) { \
                keymap_bind_func(km, key, actions[i].func); \
                break; \
            } \
        } \
    } while (0)

    /* Control characters */
    BIND('\r', "ed-newline");
    BIND('\n', "ed-newline");
    BIND(0x01, "ed-move-to-beg");      /* ^A */
    BIND(0x02, "ed-prev-char");        /* ^B */
    BIND(0x03, "ed-tty-sigint");       /* ^C */
    BIND(0x04, "ed-delete-next-char"); /* ^D */
    BIND(0x05, "ed-move-to-end");      /* ^E */
    BIND(0x06, "ed-next-char");        /* ^F */
    BIND(0x08, "em-delete-prev-char"); /* ^H / Backspace */
    BIND(0x09, "ed-complete");         /* ^I / Tab */
    BIND(0x0B, "ed-kill-line");        /* ^K */
    BIND(0x0C, "ed-clear-screen");     /* ^L */
    BIND(0x0E, "ed-next-history");     /* ^N */
    BIND(0x10, "ed-prev-history");     /* ^P */
    BIND(0x12, "em-search-prev");      /* ^R */
    BIND(0x13, "em-search-next");      /* ^S */
    BIND(0x14, "ed-transpose-chars");  /* ^T */
    BIND(0x15, "em-kill-region");      /* ^U */
    BIND(0x17, "em-delete-prev-word"); /* ^W */
    BIND(0x19, "em-yank");             /* ^Y */
    BIND(0x1F, "ed-undo");             /* ^_ */
    BIND(0x7F, "em-delete-prev-char"); /* DEL */

    /* Printable self-insert: 32-126 */
    for (i = 0; i < count; i++) {
        if (strcmp(actions[i].name, "ed-insert") == 0) {
            fn = actions[i].func;
            break;
        }
    }
    {
        int ch;
        for (ch = 32; ch < 127; ch++)
            keymap_bind_func(km, ch, fn);
    }

    /* ESC sub-keymap for Meta keys */
    esc = keymap_ensure_submap(km, 0x1B);
    if (!esc) return;

    #undef BIND
    #define BIND(key, aname) do { \
        for (i = 0; i < count; i++) { \
            if (strcmp(actions[i].name, aname) == 0) { \
                keymap_bind_func(esc, key, actions[i].func); \
                break; \
            } \
        } \
    } while (0)

    BIND('b', "em-prev-word");
    BIND('B', "em-prev-word");
    BIND('c', "em-capitalize-word");
    BIND('C', "em-capitalize-word");
    BIND('d', "em-kill-word");
    BIND('D', "em-kill-word");
    BIND('f', "em-next-word");
    BIND('F', "em-next-word");
    BIND('l', "em-lower-case-word");
    BIND('L', "em-lower-case-word");
    BIND('u', "em-upper-case-word");
    BIND('U', "em-upper-case-word");
    BIND('y', "em-yank-pop");
    BIND('Y', "em-yank-pop");
    BIND('.', "em-yank-last-arg");
    BIND('<', "em-beginning-of-history");
    BIND('>', "em-end-of-history");
    BIND(0x08, "em-backward-kill-word");
    BIND(0x7F, "em-backward-kill-word");

    /* CSI sub-keymap (ESC [ ...) */
    csi = keymap_ensure_submap(esc, '[');
    if (!csi) return;

    #undef BIND
    #define BIND(key, aname) do { \
        for (i = 0; i < count; i++) { \
            if (strcmp(actions[i].name, aname) == 0) { \
                keymap_bind_func(csi, key, actions[i].func); \
                break; \
            } \
        } \
    } while (0)

    BIND('A', "ed-prev-history");        /* Up */
    BIND('B', "ed-next-history");        /* Down */
    BIND('C', "ed-next-char");           /* Right */
    BIND('D', "ed-prev-char");           /* Left */
    BIND('H', "ed-move-to-beg");         /* Home */
    BIND('F', "ed-move-to-end");         /* End */

    /* CSI digit sequences handled by a dispatch function */
    for (i = 0; i < count; i++) {
        if (strcmp(actions[i].name, "em-csi-dispatch") == 0) {
            int d;
            for (d = '0'; d <= '9'; d++)
                keymap_bind_func(csi, d, actions[i].func);
            break;
        }
    }

    /* SS3 sub-keymap (ESC O ...) */
    ss3 = keymap_ensure_submap(esc, 'O');
    if (!ss3) return;

    #undef BIND
    #define BIND(key, aname) do { \
        for (i = 0; i < count; i++) { \
            if (strcmp(actions[i].name, aname) == 0) { \
                keymap_bind_func(ss3, key, actions[i].func); \
                break; \
            } \
        } \
    } while (0)

    BIND('A', "ed-prev-history");
    BIND('B', "ed-next-history");
    BIND('C', "ed-next-char");
    BIND('D', "ed-prev-char");
    BIND('H', "ed-move-to-beg");
    BIND('F', "ed-move-to-end");

    #undef BIND
}

void keymap_init_vi_insert(EditLine *el) {
    int count, i;
    const struct action_entry *actions = el_builtin_actions(&count);
    struct keymap_entry *km;
    el_action_t fn;

    if (!el || !el->vi_insert_keymap) return;
    km = (struct keymap_entry *)el->vi_insert_keymap;

    #define BIND(key, aname) do { \
        for (i = 0; i < count; i++) { \
            if (strcmp(actions[i].name, aname) == 0) { \
                keymap_bind_func(km, key, actions[i].func); \
                break; \
            } \
        } \
    } while (0)

    BIND('\r', "ed-newline");
    BIND('\n', "ed-newline");
    BIND(0x1B, "vi-to-command-mode");
    BIND(0x7F, "em-delete-prev-char");
    BIND(0x08, "em-delete-prev-char");
    BIND(0x17, "em-delete-prev-word");
    BIND(0x15, "em-kill-region");
    BIND(0x04, "ed-delete-next-char");
    BIND(0x09, "ed-complete");

    /* Self-insert for printable chars */
    fn = NULL;
    for (i = 0; i < count; i++) {
        if (strcmp(actions[i].name, "ed-insert") == 0) {
            fn = actions[i].func;
            break;
        }
    }
    if (fn) {
        int ch;
        for (ch = 32; ch < 127; ch++)
            keymap_bind_func(km, ch, fn);
    }

    #undef BIND
}

void keymap_init_vi_command(EditLine *el) {
    int count, i;
    const struct action_entry *actions = el_builtin_actions(&count);
    struct keymap_entry *km;

    if (!el || !el->vi_command_keymap) return;
    km = (struct keymap_entry *)el->vi_command_keymap;

    #define BIND(key, aname) do { \
        for (i = 0; i < count; i++) { \
            if (strcmp(actions[i].name, aname) == 0) { \
                keymap_bind_func(km, key, actions[i].func); \
                break; \
            } \
        } \
    } while (0)

    BIND('\r', "ed-newline");
    BIND('\n', "ed-newline");

    /* Vi digit accumulator: 1-9 always, 0 is special */
    for (i = 0; i < count; i++) {
        if (strcmp(actions[i].name, "vi-arg-digit") == 0) {
            int d;
            for (d = '1'; d <= '9'; d++)
                keymap_bind_func(km, d, actions[i].func);
            break;
        }
    }

    /* Motions */
    BIND('h', "vi-motion-h");
    BIND('l', "vi-motion-l");
    BIND('w', "vi-motion-w");
    BIND('W', "vi-motion-W");
    BIND('b', "vi-motion-b");
    BIND('B', "vi-motion-B");
    BIND('e', "vi-motion-e");
    BIND('E', "vi-motion-E");
    BIND('0', "vi-beginning-of-line");
    BIND('$', "vi-end-of-line");
    BIND('^', "vi-first-nonblank");
    BIND('f', "vi-find-char");
    BIND('F', "vi-find-char");
    BIND('t', "vi-find-char");
    BIND('T', "vi-find-char");
    BIND(';', "vi-repeat-find");
    BIND(',', "vi-reverse-find");

    /* Mode change */
    BIND('i', "vi-insert-mode");
    BIND('a', "vi-append-mode");
    BIND('I', "vi-insert-beg");
    BIND('A', "vi-append-end");

    /* Delete/change/yank */
    BIND('x', "vi-delete-char");
    BIND('X', "vi-backward-delete-char");
    BIND('r', "vi-replace-char");
    BIND('R', "vi-replace-mode");
    BIND('s', "vi-substitute-char");
    BIND('S', "vi-substitute-line");
    BIND('d', "vi-delete-motion");
    BIND('D', "vi-delete-to-end");
    BIND('c', "vi-change-motion");
    BIND('C', "vi-change-to-end");
    BIND('y', "vi-yank-motion");

    /* Paste */
    BIND('p', "vi-paste-after");
    BIND('P', "vi-paste-before");

    /* Undo / repeat */
    BIND('u', "ed-undo");
    BIND('.', "vi-dot-repeat");
    BIND('~', "vi-toggle-case");

    /* History */
    BIND('j', "vi-next-history");
    BIND('k', "vi-prev-history");
    BIND('/', "vi-search-forward");
    BIND('?', "vi-search-backward");
    BIND('n', "vi-search-next");
    BIND('N', "vi-search-prev");

    /* Misc */
    BIND('v', "vi-edit-external");
    BIND('#', "vi-comment-line");

    #undef BIND
}

void keymap_init_vi_replace(EditLine *el) {
    int count, i;
    const struct action_entry *actions = el_builtin_actions(&count);
    struct keymap_entry *km;
    el_action_t fn;

    if (!el || !el->vi_replace_keymap) return;
    km = (struct keymap_entry *)el->vi_replace_keymap;

    #define BIND(key, aname) do { \
        for (i = 0; i < count; i++) { \
            if (strcmp(actions[i].name, aname) == 0) { \
                keymap_bind_func(km, key, actions[i].func); \
                break; \
            } \
        } \
    } while (0)

    BIND('\r', "ed-newline");
    BIND('\n', "ed-newline");
    BIND(0x1B, "vi-to-command-mode");
    BIND(0x7F, "vi-replace-back");
    BIND(0x08, "vi-replace-back");

    /* Overwrite for printable chars */
    fn = NULL;
    for (i = 0; i < count; i++) {
        if (strcmp(actions[i].name, "vi-replace-insert") == 0) {
            fn = actions[i].func;
            break;
        }
    }
    if (fn) {
        int ch;
        for (ch = 32; ch < 127; ch++)
            keymap_bind_func(km, ch, fn);
    }

    #undef BIND
}
