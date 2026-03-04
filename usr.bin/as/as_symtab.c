#include "as_symtab.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    as_symtab_t *tab;
    char *errbuf;
    size_t errbuf_sz;
} sym_ctx_t;

static void set_err(sym_ctx_t *ctx, const char *fmt, ...) {
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

void as_symtab_init(as_symtab_t *tab) {
    if (tab == NULL) {
        return;
    }
    tab->items = NULL;
    tab->count = 0;
    tab->cap = 0;
}

void as_symtab_free(as_symtab_t *tab) {
    size_t i;

    if (tab == NULL) {
        return;
    }
    for (i = 0; i < tab->count; ++i) {
        as_symbol_t *s = &tab->items[i];
        free(s->name);
        free(s->def_file);
        free(s->version);
    }
    free(tab->items);
    tab->items = NULL;
    tab->count = 0;
    tab->cap = 0;
}

static as_symbol_t *find_symbol(as_symtab_t *tab, const char *name) {
    size_t i;

    for (i = 0; i < tab->count; ++i) {
        if (strcmp(tab->items[i].name, name) == 0) {
            return &tab->items[i];
        }
    }
    return NULL;
}

static as_symbol_t *get_or_create_symbol(sym_ctx_t *ctx, const char *name) {
    as_symbol_t *sym;
    as_symbol_t *next;

    sym = find_symbol(ctx->tab, name);
    if (sym != NULL) {
        return sym;
    }

    if (ctx->tab->count == ctx->tab->cap) {
        size_t ncap = ctx->tab->cap == 0 ? 32 : ctx->tab->cap * 2;
        next = (as_symbol_t *)realloc(ctx->tab->items, ncap * sizeof(*next));
        if (next == NULL) {
            return NULL;
        }
        ctx->tab->items = next;
        ctx->tab->cap = ncap;
    }

    sym = &ctx->tab->items[ctx->tab->count++];
    memset(sym, 0, sizeof(*sym));
    sym->name = xstrdup(name);
    sym->bind = AS_SYM_BIND_LOCAL;
    sym->type = AS_SYM_TYPE_NOTYPE;
    sym->visibility = AS_SYM_VIS_DEFAULT;
    if (sym->name == NULL) {
        return NULL;
    }
    return sym;
}

static int parse_u64_arg(const char *s, unsigned long long *out) {
    char *tmp;
    char *end;
    unsigned long long v;

    tmp = trim_copy(s);
    if (tmp == NULL) {
        return -1;
    }

    if (tmp[0] == '\0') {
        free(tmp);
        return -1;
    }

    end = NULL;
    v = strtoull(tmp, &end, 0);
    if (end == tmp || *end != '\0') {
        free(tmp);
        return -1;
    }

    free(tmp);
    *out = v;
    return 0;
}

static int parse_size_arg(const char *expr, const char *sym_name, unsigned long long *out) {
    char *tmp;
    char *dash;

    if (expr == NULL || out == NULL) {
        return -1;
    }
    if (parse_u64_arg(expr, out) == 0) {
        return 0;
    }

    tmp = trim_copy(expr);
    if (tmp == NULL) {
        return -1;
    }

    /*
     * Accept common symbolic forms emitted by C compilers:
     *   .size foo, .-foo
     *   .size foo, .Lend-foo
     * We record size as 0 here when symbolic.
     */
    if (sym_name != NULL) {
        if (strncmp(tmp, ".-", 2) == 0 && strcmp(tmp + 2, sym_name) == 0) {
            free(tmp);
            *out = 0;
            return 0;
        }
        dash = strrchr(tmp, '-');
        if (dash != NULL) {
            char *rhs = trim_copy(dash + 1);
            if (rhs == NULL) {
                free(tmp);
                return -1;
            }
            if (strcmp(rhs, sym_name) == 0) {
                free(rhs);
                free(tmp);
                *out = 0;
                return 0;
            }
            free(rhs);
        }
    }

    free(tmp);
    return -1;
}

static int symbol_set_definition(as_symbol_t *sym, const char *file, unsigned line) {
    if (!sym->defined) {
        sym->def_file = xstrdup(file);
        if (sym->def_file == NULL) {
            return -1;
        }
        sym->def_line = line;
    }
    sym->defined = 1;
    return 0;
}

static int apply_visibility(sym_ctx_t *ctx, const as_directive_t *d, as_sym_visibility_t v) {
    size_t i;

    for (i = 0; i < d->arg_count; ++i) {
        char *name = trim_copy(d->args[i]);
        as_symbol_t *sym;
        if (name == NULL) {
            return -1;
        }
        sym = get_or_create_symbol(ctx, name);
        free(name);
        if (sym == NULL) {
            return -1;
        }
        sym->visibility = v;
    }
    return 0;
}

static as_sym_type_t parse_type_name(const char *raw) {
    char *tmp;
    char *s;
    as_sym_type_t t = AS_SYM_TYPE_NOTYPE;

    tmp = trim_copy(raw);
    if (tmp == NULL) {
        return AS_SYM_TYPE_NOTYPE;
    }
    s = tmp;
    while (*s == '@' || *s == '%' || *s == '#') {
        s++;
    }

    if (strcmp(s, "function") == 0 || strcmp(s, "func") == 0) {
        t = AS_SYM_TYPE_FUNCTION;
    } else if (strcmp(s, "object") == 0) {
        t = AS_SYM_TYPE_OBJECT;
    } else if (strcmp(s, "tls_object") == 0) {
        t = AS_SYM_TYPE_TLS_OBJECT;
    } else if (strcmp(s, "common") == 0) {
        t = AS_SYM_TYPE_COMMON;
    } else if (strcmp(s, "notype") == 0) {
        t = AS_SYM_TYPE_NOTYPE;
    }

    free(tmp);
    return t;
}

static int handle_directive(sym_ctx_t *ctx, const as_stmt_t *st) {
    const as_directive_t *d = &st->u.directive;
    size_t i;

    if (strcmp(d->name, ".globl") == 0 || strcmp(d->name, ".global") == 0) {
        for (i = 0; i < d->arg_count; ++i) {
            char *name = trim_copy(d->args[i]);
            as_symbol_t *sym;
            if (name == NULL) {
                return -1;
            }
            sym = get_or_create_symbol(ctx, name);
            free(name);
            if (sym == NULL) {
                return -1;
            }
            sym->bind = AS_SYM_BIND_GLOBAL;
        }
        return 0;
    }

    if (strcmp(d->name, ".local") == 0) {
        for (i = 0; i < d->arg_count; ++i) {
            char *name = trim_copy(d->args[i]);
            as_symbol_t *sym;
            if (name == NULL) {
                return -1;
            }
            sym = get_or_create_symbol(ctx, name);
            free(name);
            if (sym == NULL) {
                return -1;
            }
            sym->bind = AS_SYM_BIND_LOCAL;
        }
        return 0;
    }

    if (strcmp(d->name, ".weak") == 0) {
        for (i = 0; i < d->arg_count; ++i) {
            char *name = trim_copy(d->args[i]);
            as_symbol_t *sym;
            if (name == NULL) {
                return -1;
            }
            sym = get_or_create_symbol(ctx, name);
            free(name);
            if (sym == NULL) {
                return -1;
            }
            sym->bind = AS_SYM_BIND_WEAK;
        }
        return 0;
    }

    if (strcmp(d->name, ".comm") == 0 || strcmp(d->name, ".lcomm") == 0) {
        char *name;
        as_symbol_t *sym;
        unsigned long long nsize = 0;
        unsigned long long nalign = 0;

        if (d->arg_count < 2) {
            return -1;
        }
        name = trim_copy(d->args[0]);
        if (name == NULL) {
            return -1;
        }
        sym = get_or_create_symbol(ctx, name);
        free(name);
        if (sym == NULL) {
            return -1;
        }
        if (parse_u64_arg(d->args[1], &nsize) != 0) {
            return -1;
        }
        if (d->arg_count >= 3 && parse_u64_arg(d->args[2], &nalign) != 0) {
            return -1;
        }

        sym->is_common = 1;
        sym->defined = 1;
        sym->type = AS_SYM_TYPE_COMMON;
        sym->common_size = nsize;
        sym->common_align = nalign;
        sym->bind = (strcmp(d->name, ".lcomm") == 0) ? AS_SYM_BIND_LOCAL : AS_SYM_BIND_GLOBAL;
        return 0;
    }

    if (strcmp(d->name, ".type") == 0) {
        char *name;
        as_symbol_t *sym;

        if (d->arg_count < 2) {
            return -1;
        }
        name = trim_copy(d->args[0]);
        if (name == NULL) {
            return -1;
        }
        sym = get_or_create_symbol(ctx, name);
        free(name);
        if (sym == NULL) {
            return -1;
        }
        sym->type = parse_type_name(d->args[1]);
        return 0;
    }

    if (strcmp(d->name, ".size") == 0) {
        char *name;
        as_symbol_t *sym;
        unsigned long long nsize = 0;

        if (d->arg_count < 2) {
            return -1;
        }
        name = trim_copy(d->args[0]);
        if (name == NULL) {
            return -1;
        }
        sym = get_or_create_symbol(ctx, name);
        if (sym == NULL) {
            free(name);
            return -1;
        }
        if (parse_size_arg(d->args[1], name, &nsize) != 0) {
            free(name);
            return -1;
        }
        free(name);
        sym->size = nsize;
        return 0;
    }

    if (strcmp(d->name, ".hidden") == 0) {
        return apply_visibility(ctx, d, AS_SYM_VIS_HIDDEN);
    }
    if (strcmp(d->name, ".protected") == 0) {
        return apply_visibility(ctx, d, AS_SYM_VIS_PROTECTED);
    }
    if (strcmp(d->name, ".internal") == 0) {
        return apply_visibility(ctx, d, AS_SYM_VIS_INTERNAL);
    }

    if (strcmp(d->name, ".symver") == 0) {
        char *name;
        char *ver;
        as_symbol_t *sym;

        if (d->arg_count < 2) {
            return -1;
        }
        name = trim_copy(d->args[0]);
        ver = trim_copy(d->args[1]);
        if (name == NULL || ver == NULL) {
            free(name);
            free(ver);
            return -1;
        }
        sym = get_or_create_symbol(ctx, name);
        free(name);
        if (sym == NULL) {
            free(ver);
            return -1;
        }
        free(sym->version);
        sym->version = ver;
        return 0;
    }

    return 0;
}

static int visit_expr_ref(sym_ctx_t *ctx, const as_expr_t *e, const char *file, unsigned line) {
    as_symbol_t *sym;

    if (e == NULL) {
        return 0;
    }

    if (e->kind == AS_EXPR_SYMBOL && e->symbol != NULL) {
        sym = get_or_create_symbol(ctx, e->symbol);
        if (sym == NULL) {
            return -1;
        }
        sym->reference_count++;
        if (!sym->defined || (sym->def_file != NULL && strcmp(sym->def_file, file) == 0 && sym->def_line > line)) {
            sym->forward_ref_count++;
        }
    }

    if (visit_expr_ref(ctx, e->lhs, file, line) != 0) {
        return -1;
    }
    if (visit_expr_ref(ctx, e->rhs, file, line) != 0) {
        return -1;
    }
    return 0;
}

static int collect_instruction_refs(sym_ctx_t *ctx, const as_stmt_t *st) {
    const as_instruction_t *in = &st->u.instr;
    size_t i;

    for (i = 0; i < in->operand_count; ++i) {
        const as_operand_t *op = &in->operands[i];

        switch (op->kind) {
        case AS_OPERAND_IMMEDIATE:
        case AS_OPERAND_LABEL_REF:
            if (visit_expr_ref(ctx, op->u.expr, st->file, st->line) != 0) {
                return -1;
            }
            break;
        case AS_OPERAND_MEMORY:
            if (visit_expr_ref(ctx, op->u.mem.disp, st->file, st->line) != 0) {
                return -1;
            }
            break;
        case AS_OPERAND_SHIFTED_REGISTER:
            if (visit_expr_ref(ctx, op->u.shifted.amount_expr, st->file, st->line) != 0) {
                return -1;
            }
            break;
        default:
            break;
        }
    }
    return 0;
}

int as_symtab_build(const as_parse_result_t *parsed, as_symtab_t *tab,
                    char *errbuf, size_t errbuf_sz) {
    sym_ctx_t ctx;
    size_t i;

    if (parsed == NULL || tab == NULL) {
        return -1;
    }

    memset(&ctx, 0, sizeof(ctx));
    ctx.tab = tab;
    ctx.errbuf = errbuf;
    ctx.errbuf_sz = errbuf_sz;
    if (errbuf != NULL && errbuf_sz > 0) {
        errbuf[0] = '\0';
    }

    /* Pass 1: definitions and symbol directives. */
    for (i = 0; i < parsed->count; ++i) {
        const as_stmt_t *st = &parsed->items[i];
        size_t j;

        for (j = 0; j < st->label_count; ++j) {
            const as_label_def_t *l = &st->labels[j];
            as_symbol_t *sym = get_or_create_symbol(&ctx, l->name);
            if (sym == NULL || symbol_set_definition(sym, l->file, l->line) != 0) {
                set_err(&ctx, "%s:%u: out of memory", st->file, st->line);
                return -1;
            }
        }

        if (st->kind == AS_STMT_DIRECTIVE) {
            if (handle_directive(&ctx, st) != 0) {
                set_err(&ctx, "%s:%u: malformed directive %s", st->file, st->line, st->u.directive.name);
                return -1;
            }
        }
    }

    /* Pass 2: references and forward-reference tracking. */
    for (i = 0; i < parsed->count; ++i) {
        const as_stmt_t *st = &parsed->items[i];
        if (st->kind == AS_STMT_INSTRUCTION) {
            if (collect_instruction_refs(&ctx, st) != 0) {
                set_err(&ctx, "%s:%u: out of memory", st->file, st->line);
                return -1;
            }
        }
    }

    for (i = 0; i < tab->count; ++i) {
        as_symbol_t *sym = &tab->items[i];
        sym->unresolved = (!sym->defined && !sym->is_common && sym->reference_count > 0) ? 1 : 0;
    }

    return 0;
}
