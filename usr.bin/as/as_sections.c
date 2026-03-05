#include "as_sections.h"

#include <ctype.h>
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
#ifndef SHF_GROUP
#define SHF_GROUP 0x200
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
    char *tmp;
    char *end;
    unsigned long v;

    tmp = trim_copy(raw);
    if (tmp == NULL) {
        return -1;
    }
    v = strtoul(tmp, &end, 0);
    if (end == tmp || *end != '\0') {
        free(tmp);
        return -1;
    }
    free(tmp);
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
        case 'g':
            flags |= SHF_GROUP;
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

static int process_section_like(sec_ctx_t *ctx, const as_stmt_t *st, int do_push) {
    const as_directive_t *d = &st->u.directive;
    as_section_state_t *s = ctx->out;
    char *name;
    unsigned flags;
    unsigned type;
    ssize_t existing = -1;

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

    free(name);
    return 0;
}

static int process_directive(sec_ctx_t *ctx, const as_stmt_t *st) {
    const as_directive_t *d = &st->u.directive;
    as_section_state_t *s = ctx->out;

    if (strcmp(d->name, ".text") == 0 || strcmp(d->name, ".data") == 0 || strcmp(d->name, ".bss") == 0 ||
        strcmp(d->name, ".rodata") == 0) {
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
        if (process_directive(&ctx, st) != 0) {
            set_err(&ctx, "%s:%u: malformed section directive %s", st->file, st->line, st->u.directive.name);
            return -1;
        }
    }

    return 0;
}
