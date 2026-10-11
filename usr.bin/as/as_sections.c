#include "as_sections.h"

#include <ctype.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#ifndef SHT_NULL
#define SHT_NULL 0
#endif
#ifndef SHT_PROGBITS
#define SHT_PROGBITS 1
#endif
#ifndef SHT_SYMTAB
#define SHT_SYMTAB 2
#endif
#ifndef SHT_STRTAB
#define SHT_STRTAB 3
#endif
#ifndef SHT_RELA
#define SHT_RELA 4
#endif
#ifndef SHT_HASH
#define SHT_HASH 5
#endif
#ifndef SHT_DYNAMIC
#define SHT_DYNAMIC 6
#endif
#ifndef SHT_NOTE
#define SHT_NOTE 7
#endif
#ifndef SHT_NOBITS
#define SHT_NOBITS 8
#endif
#ifndef SHT_REL
#define SHT_REL 9
#endif
#ifndef SHF_WRITE
#define SHF_WRITE 0x1
#endif
#ifndef SHF_ALLOC
#define SHF_ALLOC 0x2
#endif
#ifndef SHF_EXECINSTR
#define SHF_EXECINSTR 0x4
#endif
#ifndef SHF_MERGE
#define SHF_MERGE 0x10
#endif
#ifndef SHF_STRINGS
#define SHF_STRINGS 0x20
#endif
#ifndef SHF_GROUP
#define SHF_GROUP 0x200
#endif
#ifndef SHF_TLS
#define SHF_TLS 0x400
#endif

typedef struct {
    as_section_state_t *out;
    char *errbuf;
    size_t errbuf_sz;
} sec_ctx_t;

static void set_err(sec_ctx_t *ctx, const char *fmt, ...) {
    va_list ap;

    if (ctx == NULL || ctx->errbuf == NULL || ctx->errbuf_sz == 0) {
        return;
    }
    va_start(ap, fmt);
    vsnprintf(ctx->errbuf, ctx->errbuf_sz, fmt, ap);
    va_end(ap);
}

static char *xstrdup(const char *s) {
    size_t n;
    char *p;

    if (s == NULL) {
        return NULL;
    }
    n = strlen(s) + 1;
    p = (char *)malloc(n);
    if (p == NULL) {
        return NULL;
    }
    memcpy(p, s, n);
    return p;
}

static int streq_ci(const char *a, const char *b) {
    size_t i;

    if (a == NULL || b == NULL) {
        return 0;
    }
    for (i = 0; a[i] != '\0' && b[i] != '\0'; ++i) {
        if (tolower((unsigned char)a[i]) != tolower((unsigned char)b[i])) {
            return 0;
        }
    }
    return a[i] == '\0' && b[i] == '\0';
}

static char *trim_copy(const char *s) {
    const char *b;
    const char *e;
    size_t n;
    char *out;

    if (s == NULL) {
        return NULL;
    }
    b = s;
    while (*b != '\0' && isspace((unsigned char)*b)) {
        b++;
    }
    e = b + strlen(b);
    while (e > b && isspace((unsigned char)e[-1])) {
        e--;
    }

    n = (size_t)(e - b);
    out = (char *)malloc(n + 1);
    if (out == NULL) {
        return NULL;
    }
    memcpy(out, b, n);
    out[n] = '\0';
    return out;
}

static char *unquote_copy(const char *s) {
    size_t n;
    char *tmp;

    tmp = trim_copy(s);
    if (tmp == NULL) {
        return NULL;
    }
    n = strlen(tmp);
    if (n >= 2 && ((tmp[0] == '"' && tmp[n - 1] == '"') || (tmp[0] == '\'' && tmp[n - 1] == '\''))) {
        memmove(tmp, tmp + 1, n - 2);
        tmp[n - 2] = '\0';
    }
    return tmp;
}

void as_section_state_init(as_section_state_t *s) {
    if (s == NULL) {
        return;
    }
    s->items = NULL;
    s->count = 0;
    s->cap = 0;
    s->current_index = 0;
    s->previous_index = 0;
    s->stack = NULL;
    s->stack_count = 0;
    s->stack_cap = 0;
}

void as_section_state_free(as_section_state_t *s) {
    size_t i;

    if (s == NULL) {
        return;
    }
    for (i = 0; i < s->count; ++i) {
        free(s->items[i].name);
        free(s->items[i].group);
    }
    free(s->items);
    free(s->stack);
    memset(s, 0, sizeof(*s));
}

const as_section_t *as_sections_find(const as_section_state_t *s, const char *name, unsigned subsection) {
    size_t i;

    if (s == NULL || name == NULL) {
        return NULL;
    }

    for (i = 0; i < s->count; ++i) {
        if (s->items[i].subsection == subsection && strcmp(s->items[i].name, name) == 0) {
            return &s->items[i];
        }
    }
    return NULL;
}

static ssize_t find_section_index(const as_section_state_t *s, const char *name, unsigned subsection) {
    size_t i;

    for (i = 0; i < s->count; ++i) {
        if (s->items[i].subsection == subsection && strcmp(s->items[i].name, name) == 0) {
            return (ssize_t)i;
        }
    }
    return -1;
}

static ssize_t add_section(as_section_state_t *s, const char *name, unsigned subsection,
                           unsigned flags, unsigned type, unsigned align) {
    as_section_t *next;

    if (s->count == s->cap) {
        size_t ncap = s->cap == 0 ? 16 : s->cap * 2;
        next = (as_section_t *)realloc(s->items, ncap * sizeof(*next));
        if (next == NULL) {
            return -1;
        }
        s->items = next;
        s->cap = ncap;
    }

    memset(&s->items[s->count], 0, sizeof(s->items[s->count]));
    s->items[s->count].name = xstrdup(name);
    if (s->items[s->count].name == NULL) {
        return -1;
    }
    s->items[s->count].subsection = subsection;
    s->items[s->count].flags = flags;
    s->items[s->count].type = type;
    s->items[s->count].align = align;
    s->items[s->count].entsize = 0;
    s->count++;
    return (ssize_t)(s->count - 1);
}

static int ensure_builtins(as_section_state_t *s) {
    if (add_section(s, ".text", 0, SHF_ALLOC | SHF_EXECINSTR, SHT_PROGBITS, 16) < 0) {
        return -1;
    }
    if (add_section(s, ".data", 0, SHF_ALLOC | SHF_WRITE, SHT_PROGBITS, 4) < 0) {
        return -1;
    }
    if (add_section(s, ".bss", 0, SHF_ALLOC | SHF_WRITE, SHT_NOBITS, 4) < 0) {
        return -1;
    }
    if (add_section(s, ".rodata", 0, SHF_ALLOC, SHT_PROGBITS, 4) < 0) {
        return -1;
    }
    s->current_index = 0;
    s->previous_index = 0;
    return 0;
}

static int parse_u32_arg(const char *raw, unsigned *out) {
    long long v;

    /* An expression of numbers alone, whose value fits 32 bits unsigned. */
    if (raw == NULL || out == NULL ||
        as_expr_eval_string(raw, NULL, NULL, &v) != AS_EXPR_EVAL_OK ||
        v < 0 || v > (long long)UINT_MAX) {
        return -1;
    }
    *out = (unsigned)v;
    return 0;
}

static unsigned parse_flags_string(const char *raw) {
    char *f;
    size_t i;
    unsigned flags = 0;

    f = unquote_copy(raw);
    if (f == NULL) {
        return 0;
    }
    for (i = 0; f[i] != '\0'; ++i) {
        switch (f[i]) {
        case 'a':
            flags |= SHF_ALLOC;
            break;
        case 'w':
            flags |= SHF_WRITE;
            break;
        case 'x':
            flags |= SHF_EXECINSTR;
            break;
        case 'M':
            flags |= SHF_MERGE;
            break;
        case 'S':
            flags |= SHF_STRINGS;
            break;
        case 'g':
            flags |= SHF_GROUP;
            break;
        case 'T':
            flags |= SHF_TLS;
            break;
        default:
            break;
        }
    }
    free(f);
    return flags;
}

static unsigned parse_type_string(const char *raw) {
    char *t;
    char *s;
    unsigned type = SHT_PROGBITS;

    t = trim_copy(raw);
    if (t == NULL) {
        return SHT_PROGBITS;
    }
    s = t;
    while (*s == '@' || *s == '%') {
        s++;
    }

    if (streq_ci(s, "progbits")) {
        type = SHT_PROGBITS;
    } else if (streq_ci(s, "nobits")) {
        type = SHT_NOBITS;
    } else if (streq_ci(s, "note")) {
        type = SHT_NOTE;
    } else if (streq_ci(s, "rela")) {
        type = SHT_RELA;
    } else if (streq_ci(s, "rel")) {
        type = SHT_REL;
    }

    free(t);
    return type;
}

static void infer_section_defaults(const char *name, unsigned *flags, unsigned *type) {
    unsigned f = SHF_ALLOC;
    unsigned t = SHT_PROGBITS;

    if (name != NULL) {
        if (strcmp(name, ".text") == 0 || strncmp(name, ".text.", 6) == 0) {
            f = SHF_ALLOC | SHF_EXECINSTR;
            t = SHT_PROGBITS;
        } else if (strcmp(name, ".data") == 0 || strncmp(name, ".data.", 6) == 0) {
            f = SHF_ALLOC | SHF_WRITE;
            t = SHT_PROGBITS;
        } else if (strcmp(name, ".bss") == 0 || strncmp(name, ".bss.", 5) == 0) {
            f = SHF_ALLOC | SHF_WRITE;
            t = SHT_NOBITS;
        } else if (strcmp(name, ".rodata") == 0 || strncmp(name, ".rodata.", 8) == 0) {
            f = SHF_ALLOC;
            t = SHT_PROGBITS;
        } else if (strncmp(name, ".discard", 8) == 0) {
            f = 0;
            t = SHT_PROGBITS;
        }
    }

    if (flags != NULL) {
        *flags = f;
    }
    if (type != NULL) {
        *type = t;
    }
}

static int switch_section(sec_ctx_t *ctx, const char *name, unsigned subsection,
                          unsigned flags, unsigned type, int update_attrs) {
    ssize_t idx;
    as_section_state_t *s = ctx->out;

    idx = find_section_index(s, name, subsection);
    if (idx < 0) {
        idx = add_section(s, name, subsection, flags, type, 1);
        if (idx < 0) {
            return -1;
        }
    }

    if (update_attrs) {
        s->items[idx].flags = flags;
        s->items[idx].type = type;
    }

    s->previous_index = s->current_index;
    s->current_index = (size_t)idx;
    return 0;
}

static int stack_push(as_section_state_t *s, size_t idx) {
    size_t *next;

    if (s->stack_count == s->stack_cap) {
        size_t ncap = s->stack_cap == 0 ? 8 : s->stack_cap * 2;
        next = (size_t *)realloc(s->stack, ncap * sizeof(*next));
        if (next == NULL) {
            return -1;
        }
        s->stack = next;
        s->stack_cap = ncap;
    }
    s->stack[s->stack_count++] = idx;
    return 0;
}

/*
 * What a directive handler returns for a subsection other than 0.  The
 * contents of subsection n of a section belong after those of every lower
 * one, wherever in the source they were written.  They are put there by
 * as_sections_gather_subsections, which takes the numbers away as it
 * does; a number that reaches here was not gathered, and to go on would
 * assemble out-of-line code in line.
 */
#define SEC_NO_SUBSECTIONS (-2)

static int process_section_like(sec_ctx_t *ctx, const as_stmt_t *st, int do_push) {
    const as_directive_t *d = &st->u.directive;
    as_section_state_t *s = ctx->out;
    char *name;
    char *group = NULL;
    unsigned flags;
    unsigned type;
    ssize_t existing = -1;
    long long subsection_number;

    if (d->arg_count < 1) {
        return -1;
    }

    if (do_push && stack_push(s, s->current_index) != 0) {
        return -1;
    }

    name = trim_copy(d->args[0]);
    if (name == NULL) {
        return -1;
    }

    existing = find_section_index(s, name, 0);
    /*
     * `.pushsection name, 1`: a number after the name is a subsection, not
     * a string of flags.  Read as flags it was no flags at all, and was
     * then written over those of the section: .text lost its "ax" and
     * every instruction in it with them.  Subsection 0 is the section,
     * entered with what it had; another is refused (SEC_NO_SUBSECTIONS).
     */
    if (do_push && d->arg_count == 2 && d->args[1] != NULL &&
        strchr(d->args[1], '"') == NULL &&
        as_expr_eval_string(d->args[1], NULL, NULL, &subsection_number) == AS_EXPR_EVAL_OK) {
        if (subsection_number != 0) {
            free(name);
            return SEC_NO_SUBSECTIONS;
        }
        if (existing >= 0) {
            flags = s->items[existing].flags;
            type = s->items[existing].type;
        } else {
            infer_section_defaults(name, &flags, &type);
        }
        if (switch_section(ctx, name, 0, flags, type, existing < 0) != 0) {
            free(name);
            return -1;
        }
        free(name);
        return 0;
    }
    if (d->arg_count >= 2) {
        flags = parse_flags_string(d->args[1]);
    } else if (existing >= 0) {
        flags = s->items[existing].flags;
    } else {
        infer_section_defaults(name, &flags, NULL);
    }

    if (d->arg_count >= 3) {
        type = parse_type_string(d->args[2]);
    } else if (existing >= 0) {
        type = s->items[existing].type;
    } else {
        infer_section_defaults(name, NULL, &type);
    }

    if (switch_section(ctx, name, 0, flags, type, 1) != 0) {
        free(name);
        return -1;
    }
    if ((flags & SHF_MERGE) != 0 && d->arg_count >= 4 &&
        parse_u32_arg(d->args[3], &s->items[s->current_index].entsize) == 0) {
        group = NULL;
    } else if (d->arg_count >= 4) {
        group = trim_copy(d->args[3]);
        if (group == NULL) {
            free(name);
            return -1;
        }
    } else if (existing >= 0 && s->items[existing].group != NULL) {
        group = trim_copy(s->items[existing].group);
        if (group == NULL) {
            free(name);
            return -1;
        }
    }
    free(s->items[s->current_index].group);
    s->items[s->current_index].group = group;
    if (d->arg_count >= 5) {
        char *f = trim_copy(d->args[4]);
        if (f == NULL) {
            free(name);
            return -1;
        }
        s->items[s->current_index].comdat = streq_ci(f, "comdat") ? 1 : 0;
        free(f);
    } else if (existing >= 0) {
        s->items[s->current_index].comdat = s->items[existing].comdat;
    }
    if (s->items[s->current_index].group != NULL || s->items[s->current_index].comdat) {
        s->items[s->current_index].flags |= SHF_GROUP;
    }
    if (s->items[s->current_index].entsize == 0 && existing >= 0) {
        s->items[s->current_index].entsize = s->items[existing].entsize;
    }

    free(name);
    return 0;
}

static int process_directive(sec_ctx_t *ctx, const as_stmt_t *st) {
    const as_directive_t *d = &st->u.directive;
    as_section_state_t *s = ctx->out;

    if (strcmp(d->name, ".text") == 0 || strcmp(d->name, ".data") == 0 || strcmp(d->name, ".bss") == 0 ||
        strcmp(d->name, ".rodata") == 0) {
        unsigned sub = 0;

        /* `.text 1`: a subsection. */
        if (d->arg_count >= 1 && d->args[0] != NULL && d->args[0][0] != '\0') {
            if (parse_u32_arg(d->args[0], &sub) != 0) {
                return -1;
            }
            if (sub != 0) {
                return SEC_NO_SUBSECTIONS;
            }
        }
        if (switch_section(ctx, d->name, 0, 0, 0, 0) != 0) {
            return -1;
        }
        return 0;
    }

    if (strcmp(d->name, ".section") == 0) {
        return process_section_like(ctx, st, 0);
    }

    if (strcmp(d->name, ".pushsection") == 0) {
        return process_section_like(ctx, st, 1);
    }

    if (strcmp(d->name, ".popsection") == 0) {
        size_t old;
        if (s->stack_count == 0) {
            return -1;
        }
        old = s->current_index;
        s->current_index = s->stack[--s->stack_count];
        s->previous_index = old;
        return 0;
    }

    if (strcmp(d->name, ".previous") == 0) {
        size_t tmp = s->current_index;
        s->current_index = s->previous_index;
        s->previous_index = tmp;
        return 0;
    }

    if (strcmp(d->name, ".subsection") == 0) {
        unsigned sub = 0;
        as_section_t *cur;
        if (d->arg_count < 1 || parse_u32_arg(d->args[0], &sub) != 0) {
            return -1;
        }
        if (sub != 0) {
            return SEC_NO_SUBSECTIONS;
        }
        cur = &s->items[s->current_index];
        if (switch_section(ctx, cur->name, sub, cur->flags, cur->type, 1) != 0) {
            return -1;
        }
        if (s->items[s->current_index].align < cur->align) {
            s->items[s->current_index].align = cur->align;
        }
        return 0;
    }

    if (strcmp(d->name, ".group") == 0) {
        as_section_t *cur = &s->items[s->current_index];
        if (d->arg_count >= 1) {
            char *g = trim_copy(d->args[0]);
            if (g == NULL) {
                return -1;
            }
            free(cur->group);
            cur->group = g;
        }
        if (d->arg_count >= 2) {
            char *f = trim_copy(d->args[1]);
            if (f == NULL) {
                return -1;
            }
            if (streq_ci(f, "comdat")) {
                cur->comdat = 1;
                cur->flags |= SHF_GROUP;
            }
            free(f);
        }
        return 0;
    }

    if (strcmp(d->name, ".align") == 0 || strcmp(d->name, ".balign") == 0 || strcmp(d->name, ".p2align") == 0) {
        unsigned a = 0;
        as_section_t *cur = &s->items[s->current_index];
        if (d->arg_count < 1 || parse_u32_arg(d->args[0], &a) != 0) {
            return -1;
        }
        if (strcmp(d->name, ".p2align") == 0) {
            if (a >= 31) {
                return -1;
            }
            a = 1u << a;
        }
        if (a > cur->align) {
            cur->align = a;
        }
        return 0;
    }

    return 0;
}

/*
 * Subsections.
 *
 * `.text 1`, `.subsection 1` and `.pushsection name, 1` put what follows
 * in a numbered part of the section, and the section is its parts in
 * ascending order of number whatever the order they were written in.  It
 * is how code is put out of line: written in the middle of a function,
 * assembled after it.
 *
 * Nothing after the parser knows of them.  The statements are put in the
 * order they are to be assembled in, here, once: those of a section's
 * lowest subsection -- 0, unless a negative one is written -- stay where
 * they are, and those of every other are moved to the end of
 * the source, each run of them behind a `.section` that names its
 * section, the runs in order of section and then of number.  A section
 * being its name to everything downstream, what is appended to it last
 * lies last in it.
 */
typedef struct {
    size_t sec;
    int sub;
} sub_where_t;

typedef struct {
    sub_where_t cur;
    sub_where_t prev;
} sub_frame_t;

typedef struct {
    char **names; /* unquoted, in order of first appearance */
    char **spell; /* as first written, for a `.section` to be made of */
    int *low;     /* the lowest subsection of each that is entered, */
    int *high;    /* and the highest */
    size_t count;
    size_t cap;
    sub_frame_t at;
    sub_frame_t *stack;
    size_t stack_count;
    size_t stack_cap;
} sub_track_t;

typedef struct {
    sub_where_t where;
    size_t seq;
    as_stmt_t st;
} sub_moved_t;

typedef struct {
    as_stmt_t *items;
    size_t count;
    size_t cap;
} sub_stmt_vec_t;

static size_t sub_intern(sub_track_t *t, const char *raw) {
    char *name = unquote_copy(raw);
    size_t i;

    if (name == NULL) {
        return (size_t)-1;
    }
    for (i = 0; i < t->count; ++i) {
        if (strcmp(t->names[i], name) == 0) {
            free(name);
            return i;
        }
    }
    if (t->count == t->cap) {
        size_t ncap = t->cap == 0 ? 8 : t->cap * 2;
        char **names = (char **)realloc(t->names, ncap * sizeof(*names));
        char **spell;

        if (names == NULL) {
            free(name);
            return (size_t)-1;
        }
        t->names = names;
        spell = (char **)realloc(t->spell, ncap * sizeof(*spell));
        if (spell == NULL) {
            free(name);
            return (size_t)-1;
        }
        t->spell = spell;
        {
            int *low = (int *)realloc(t->low, ncap * sizeof(*low));
            int *high;

            if (low == NULL) {
                free(name);
                return (size_t)-1;
            }
            t->low = low;
            high = (int *)realloc(t->high, ncap * sizeof(*high));
            if (high == NULL) {
                free(name);
                return (size_t)-1;
            }
            t->high = high;
        }
        t->cap = ncap;
    }
    t->spell[t->count] = trim_copy(raw);
    if (t->spell[t->count] == NULL) {
        free(name);
        return (size_t)-1;
    }
    t->names[t->count] = name;
    t->low[t->count] = INT_MAX;
    t->high[t->count] = INT_MIN;
    return t->count++;
}

static void sub_track_free(sub_track_t *t) {
    size_t i;

    for (i = 0; i < t->count; ++i) {
        free(t->names[i]);
        free(t->spell[i]);
    }
    free(t->names);
    free(t->spell);
    free(t->low);
    free(t->high);
    free(t->stack);
    memset(t, 0, sizeof(*t));
}

static int sub_track_start(sub_track_t *t) {
    t->stack_count = 0;
    t->at.cur.sec = sub_intern(t, ".text");
    t->at.cur.sub = 0;
    t->at.prev = t->at.cur;
    return t->at.cur.sec == (size_t)-1 ? -1 : 0;
}

/* The number of a subsection.  GNU as takes any, keeps 32 bits of it and
 * orders them as signed, so that -1 is before 0. */
static int sub_number(const char *raw, int *out) {
    long long v;

    if (raw == NULL || as_expr_eval_string(raw, NULL, NULL, &v) != AS_EXPR_EVAL_OK) {
        return -1;
    }
    *out = (int)(unsigned)(unsigned long long)v;
    return 0;
}

static int sub_has_arg(const as_directive_t *d, size_t i) {
    return d->arg_count > i && d->args[i] != NULL && d->args[i][0] != '\0';
}

/* Whether the second argument of a .pushsection is a subsection's number
 * and not a string of flags. */
static int sub_pushsection_has_number(const as_directive_t *d) {
    long long v;

    return d->arg_count == 2 && sub_has_arg(d, 1) && strchr(d->args[1], '"') == NULL &&
           as_expr_eval_string(d->args[1], NULL, NULL, &v) == AS_EXPR_EVAL_OK;
}

/*
 * Follow a directive that says where what comes next is assembled.  1 if
 * it is one, 0 if not, -1 for a number that is none or for no memory.
 */
static int sub_track_apply(sub_track_t *t, const as_directive_t *d) {
    sub_where_t next;

    if (d->name == NULL) {
        return 0;
    }
    if (strcmp(d->name, ".previous") == 0) {
        sub_where_t tmp = t->at.cur;

        t->at.cur = t->at.prev;
        t->at.prev = tmp;
        return 1;
    }
    if (strcmp(d->name, ".popsection") == 0) {
        if (t->stack_count != 0) {
            t->at = t->stack[--t->stack_count];
        }
        return 1;
    }
    if (strcmp(d->name, ".subsection") == 0) {
        next.sec = t->at.cur.sec;
        if (d->arg_count < 1 || sub_number(d->args[0], &next.sub) != 0) {
            return -1;
        }
    } else if (strcmp(d->name, ".text") == 0 || strcmp(d->name, ".data") == 0 || strcmp(d->name, ".bss") == 0 ||
               strcmp(d->name, ".rodata") == 0) {
        next.sec = sub_intern(t, d->name);
        next.sub = 0;
        if (next.sec == (size_t)-1 || (sub_has_arg(d, 0) && sub_number(d->args[0], &next.sub) != 0)) {
            return -1;
        }
    } else if (strcmp(d->name, ".section") == 0 || strcmp(d->name, ".pushsection") == 0) {
        int push = d->name[1] == 'p';

        if (!sub_has_arg(d, 0)) {
            return 0;
        }
        next.sec = sub_intern(t, d->args[0]);
        next.sub = 0;
        /* Not a negative one here: GNU as does not read it as a number. */
        if (next.sec == (size_t)-1 ||
            (push && sub_pushsection_has_number(d) && (sub_number(d->args[1], &next.sub) != 0 || next.sub < 0))) {
            return -1;
        }
        if (push) {
            if (t->stack_count == t->stack_cap) {
                size_t ncap = t->stack_cap == 0 ? 8 : t->stack_cap * 2;
                sub_frame_t *stack = (sub_frame_t *)realloc(t->stack, ncap * sizeof(*stack));

                if (stack == NULL) {
                    return -1;
                }
                t->stack = stack;
                t->stack_cap = ncap;
            }
            t->stack[t->stack_count++] = t->at;
        }
    } else {
        return 0;
    }
    t->at.prev = t->at.cur;
    t->at.cur = next;
    return 1;
}

static const char *sub_mode_directive(const as_stmt_t *st) {
    static const char *const modes[] = {".code16", ".code32", ".code64"};
    size_t i;

    if (st->kind != AS_STMT_DIRECTIVE || st->u.directive.name == NULL) {
        return NULL;
    }
    for (i = 0; i < sizeof(modes) / sizeof(modes[0]); ++i) {
        if (strcmp(st->u.directive.name, modes[i]) == 0) {
            return modes[i];
        }
    }
    return NULL;
}

static int sub_vec_push(sub_stmt_vec_t *v, const as_stmt_t *st) {
    if (v->count == v->cap) {
        size_t ncap = v->cap == 0 ? 64 : v->cap * 2;
        as_stmt_t *items = (as_stmt_t *)realloc(v->items, ncap * sizeof(*items));

        if (items == NULL) {
            return -1;
        }
        v->items = items;
        v->cap = ncap;
    }
    v->items[v->count++] = *st;
    return 0;
}

/* A directive of one argument or none, at the place of the statement
 * `at`, into `out`. */
static int sub_make_directive(as_stmt_t *out, const as_stmt_t *at, const char *name, const char *arg) {
    memset(out, 0, sizeof(*out));
    out->kind = AS_STMT_DIRECTIVE;
    out->line = at->line;
    out->file = at->file != NULL ? xstrdup(at->file) : NULL;
    out->u.directive.name = xstrdup(name);
    if ((at->file != NULL && out->file == NULL) || out->u.directive.name == NULL) {
        goto fail;
    }
    if (arg != NULL) {
        out->u.directive.args = (char **)calloc(1, sizeof(char *));
        if (out->u.directive.args == NULL) {
            goto fail;
        }
        out->u.directive.arg_cap = 1;
        out->u.directive.args[0] = xstrdup(arg);
        if (out->u.directive.args[0] == NULL) {
            goto fail;
        }
        out->u.directive.arg_count = 1;
    }
    return 0;
fail:
    free(out->file);
    free(out->u.directive.name);
    free(out->u.directive.args);
    memset(out, 0, sizeof(*out));
    return -1;
}

static void sub_drop_args(as_directive_t *d, size_t keep) {
    while (d->arg_count > keep) {
        free(d->args[--d->arg_count]);
        d->args[d->arg_count] = NULL;
    }
}

static int sub_moved_cmp(const void *a, const void *b) {
    const sub_moved_t *x = (const sub_moved_t *)a;
    const sub_moved_t *y = (const sub_moved_t *)b;

    if (x->where.sec != y->where.sec) {
        return x->where.sec < y->where.sec ? -1 : 1;
    }
    if (x->where.sub != y->where.sub) {
        return x->where.sub < y->where.sub ? -1 : 1;
    }
    return x->seq < y->seq ? -1 : (x->seq > y->seq ? 1 : 0);
}

int as_sections_gather_subsections(as_parse_result_t *parsed, unsigned default_code_bits,
                                   char *errbuf, size_t errbuf_sz) {
    sub_track_t t;
    sub_stmt_vec_t main_v;
    sub_moved_t *moved = NULL;
    size_t moved_count = 0;
    size_t moved_cap = 0;
    const char *mode = default_code_bits == 64 ? ".code64" : (default_code_bits == 16 ? ".code16" : ".code32");
    int any_numbered = 0;
    int any_mode = 0;
    int need_header = 1;
    size_t i;

    if (parsed == NULL) {
        return -1;
    }
    memset(&t, 0, sizeof(t));
    memset(&main_v, 0, sizeof(main_v));
    if (sub_track_start(&t) != 0) {
        goto oom;
    }
    t.low[t.at.cur.sec] = t.high[t.at.cur.sec] = 0;
    /* Most sources have none, and are left as they are. */
    for (i = 0; i < parsed->count; ++i) {
        const as_stmt_t *st = &parsed->items[i];

        if (sub_mode_directive(st) != NULL) {
            any_mode = 1;
        }
        if (st->kind != AS_STMT_DIRECTIVE) {
            continue;
        }
        if (sub_track_apply(&t, &st->u.directive) < 0) {
            if (errbuf != NULL && errbuf_sz != 0) {
                snprintf(errbuf, errbuf_sz, "%s:%u: %s: not the number of a subsection",
                         st->file != NULL ? st->file : "<input>", st->line, st->u.directive.name);
            }
            sub_track_free(&t);
            return -1;
        }
        if (t.at.cur.sub < t.low[t.at.cur.sec]) {
            t.low[t.at.cur.sec] = t.at.cur.sub;
        }
        if (t.at.cur.sub > t.high[t.at.cur.sec]) {
            t.high[t.at.cur.sec] = t.at.cur.sub;
        }
    }
    for (i = 0; i < t.count; ++i) {
        if (t.low[i] < t.high[i]) {
            any_numbered = 1;
        }
    }
    if (!any_numbered) {
        sub_track_free(&t);
        return 0;
    }

/* The lowest subsection of a section is the one that stays. */
#define SUB_IS_MOVED(t) ((t).at.cur.sub != (t).low[(t).at.cur.sec])

#define SUB_MOVE(stmt)                                                                        \
    do {                                                                                       \
        if (moved_count == moved_cap) {                                                        \
            size_t ncap = moved_cap == 0 ? 64 : moved_cap * 2;                                 \
            sub_moved_t *next = (sub_moved_t *)realloc(moved, ncap * sizeof(*next));           \
            if (next == NULL) {                                                                \
                goto oom;                                                                      \
            }                                                                                  \
            moved = next;                                                                      \
            moved_cap = ncap;                                                                  \
        }                                                                                      \
        moved[moved_count].where = t.at.cur;                                                   \
        moved[moved_count].seq = moved_count;                                                  \
        moved[moved_count].st = (stmt);                                                        \
        moved_count++;                                                                         \
    } while (0)

    if (sub_track_start(&t) != 0) {
        goto oom;
    }
    for (i = 0; i < parsed->count; ++i) {
        parsed->items[i].source_seq = i + 1;
    }
    for (i = 0; i < parsed->count; ++i) {
        as_stmt_t *st = &parsed->items[i];
        as_directive_t *d = &st->u.directive;
        int control = 0;
        as_stmt_t made;

        if (st->kind == AS_STMT_DIRECTIVE && d->name != NULL &&
            (strcmp(d->name, ".previous") == 0 || strcmp(d->name, ".popsection") == 0 ||
             strcmp(d->name, ".subsection") == 0 || strcmp(d->name, ".text") == 0 || strcmp(d->name, ".data") == 0 ||
             strcmp(d->name, ".bss") == 0 || strcmp(d->name, ".rodata") == 0 ||
             ((strcmp(d->name, ".section") == 0 || strcmp(d->name, ".pushsection") == 0) && sub_has_arg(d, 0)))) {
            control = 1;
        }
        if (!control && !SUB_IS_MOVED(t)) {
            if (sub_mode_directive(st) != NULL) {
                mode = sub_mode_directive(st);
            }
            if (sub_vec_push(&main_v, st) != 0) {
                goto oom;
            }
            continue;
        }
        if (SUB_IS_MOVED(t) && (!control || st->label_count != 0)) {
            /* A run of a numbered subsection opens with the section it
             * is of and the mode it was written in. */
            if (need_header) {
                if (sub_make_directive(&made, st, ".section", t.spell[t.at.cur.sec]) != 0) {
                    goto oom;
                }
                SUB_MOVE(made);
                if (any_mode) {
                    if (sub_make_directive(&made, st, mode, NULL) != 0) {
                        goto oom;
                    }
                    SUB_MOVE(made);
                }
                need_header = 0;
            }
        }
        if (!control) {
            /* A change of mode is for all that follows it in the source,
             * here and where the rest of the statements stay. */
            if (sub_mode_directive(st) != NULL) {
                mode = sub_mode_directive(st);
                if (sub_make_directive(&made, st, mode, NULL) != 0 || sub_vec_push(&main_v, &made) != 0) {
                    goto oom;
                }
            }
            SUB_MOVE(*st);
            continue;
        }
        /* The labels on a directive that leaves a numbered subsection
         * are in the subsection it leaves. */
        if (SUB_IS_MOVED(t) && st->label_count != 0) {
            memset(&made, 0, sizeof(made));
            made.kind = AS_STMT_LABEL_ONLY;
            made.source_seq = st->source_seq;
            made.line = st->line;
            made.file = st->file != NULL ? xstrdup(st->file) : NULL;
            if (st->file != NULL && made.file == NULL) {
                goto oom;
            }
            made.labels = st->labels;
            made.label_count = st->label_count;
            made.label_cap = st->label_cap;
            st->labels = NULL;
            st->label_count = 0;
            st->label_cap = 0;
            SUB_MOVE(made);
        }
        if (sub_track_apply(&t, d) < 0) {
            goto oom;
        }
        need_header = 1;
        /* The directive stays, without its number: what is left of it
         * is the change of section, which is all the rest know of. */
        if (strcmp(d->name, ".subsection") == 0) {
            char *name = xstrdup(".section");
            char *arg = xstrdup(t.spell[t.at.cur.sec]);

            if (name == NULL || arg == NULL || d->arg_count < 1) {
                free(name);
                free(arg);
                goto oom;
            }
            sub_drop_args(d, 1);
            free(d->args[0]);
            d->args[0] = arg;
            free(d->name);
            d->name = name;
        } else if (strcmp(d->name, ".pushsection") == 0) {
            if (sub_pushsection_has_number(d)) {
                sub_drop_args(d, 1);
            }
        } else if (strcmp(d->name, ".section") != 0) {
            sub_drop_args(d, 0);
        }
        if (sub_vec_push(&main_v, st) != 0) {
            goto oom;
        }
    }
#undef SUB_MOVE
#undef SUB_IS_MOVED

    qsort(moved, moved_count, sizeof(*moved), sub_moved_cmp);
    for (i = 0; i < moved_count; ++i) {
        if (sub_vec_push(&main_v, &moved[i].st) != 0) {
            goto oom;
        }
    }
    free(moved);
    free(parsed->items);
    parsed->items = main_v.items;
    parsed->count = main_v.count;
    parsed->cap = main_v.cap;
    sub_track_free(&t);
    return 0;

oom:
    /* Statements are in two places or in neither by now; the assembler
     * is about to exit, and is not to free any of them twice. */
    parsed->count = 0;
    free(moved);
    free(main_v.items);
    sub_track_free(&t);
    if (errbuf != NULL && errbuf_sz != 0) {
        snprintf(errbuf, errbuf_sz, "out of memory gathering subsections");
    }
    return -1;
}

int as_sections_build(const as_parse_result_t *parsed, as_section_state_t *out,
                      char *errbuf, size_t errbuf_sz) {
    sec_ctx_t ctx;
    size_t i;

    if (parsed == NULL || out == NULL) {
        return -1;
    }

    memset(&ctx, 0, sizeof(ctx));
    ctx.out = out;
    ctx.errbuf = errbuf;
    ctx.errbuf_sz = errbuf_sz;
    if (errbuf != NULL && errbuf_sz > 0) {
        errbuf[0] = '\0';
    }

    if (ensure_builtins(out) != 0) {
        set_err(&ctx, "section state init failed");
        return -1;
    }

    for (i = 0; i < parsed->count; ++i) {
        const as_stmt_t *st = &parsed->items[i];
        if (st->kind != AS_STMT_DIRECTIVE) {
            continue;
        }
        switch (process_directive(&ctx, st)) {
        case 0:
            break;
        case SEC_NO_SUBSECTIONS:
            set_err(&ctx, "%s:%u: %s: subsections other than 0 are not supported",
                    st->file, st->line, st->u.directive.name);
            return -1;
        default:
            set_err(&ctx, "%s:%u: malformed section directive %s", st->file, st->line, st->u.directive.name);
            return -1;
        }
    }

    return 0;
}
